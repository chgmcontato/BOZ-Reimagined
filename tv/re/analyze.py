#!/usr/bin/env python3
"""Análise estática do código do jogo (boz.s3e) -> banco de dados JSON para os mapas em docs/re/.

Uso: analyze.py [--s3e build/base.apk|boz.s3e] [--out build/re/out]
Precisa do capstone (pip install --target <pasta> capstone; passar a pasta em PYLIB=...).

O que sai em out/db.json:
  imports    stub do PLT -> nome s3e/gl
  classes    RTTI (nome, typeinfo, bases, vtables com os métodos)
  functions  início, modo (thumb/arm), tamanho, chamadas, quem chama, strings, globais, hashes, nome
Endereços sempre em offset na imagem (ver s3eimg.py).
"""
import argparse
import collections
import json
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get('PYLIB'):
    sys.path.insert(0, os.environ['PYLIB'])
import s3eimg as S  # noqa: E402
from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, Cs  # noqa: E402
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG, ARM_REG_PC  # noqa: E402

PORT = S.PORT


def demangle(names):
    if not names:
        return {}
    p = subprocess.run(['c++filt'], input='\n'.join('_ZTS' + n for n in names), capture_output=True, text=True)
    out = {}
    for n, d in zip(names, p.stdout.splitlines()):
        out[n] = d.replace('typeinfo name for ', '')
    return out


class Image:
    def __init__(self, img):
        self.img = img
        self.n = len(img)
        self.relocs = set(S.relocs(img))
        self.imports = S.import_slots(img)

    def u32(self, o):
        return struct.unpack_from('<I', self.img, o)[0]

    def ptr(self, o):
        """ponteiro relocado gravado em o -> offset do alvo (ou None)"""
        if o in self.relocs:
            return S.off(self.u32(o))
        return None


# ---------------------------------------------------------------- RTTI e vtables

def find_rtti(I):
    img = I.img
    by_target = collections.defaultdict(list)
    for r in I.relocs:
        by_target[S.off(I.u32(r))].append(r)
    names = {}
    for m in re.finditer(rb'(?<=\x00)((\d{1,3})([A-Za-z_][A-Za-z0-9_]*)|N[A-Za-z0-9_]+E|St\d+[A-Za-z_][A-Za-z0-9_]*)\x00',
                         img[S.RODATA_START:S.DATA_START]):
        o = S.RODATA_START + m.start()
        s = m.group(1).decode()
        if m.group(2) and len(m.group(3)) < int(m.group(2)):
            continue
        for r in by_target.get(o, []):
            if r - 4 in I.relocs:
                names[r - 4] = s       # typeinfo = [vptr][nome]...
    kinds = collections.Counter(I.u32(t) for t in names)
    # descobre qual vptr é qual: si = 1 base em +8; vmi = flags,nº,(base,offs)...; class = sem base
    tis = set(names)
    kind_of = {}
    for vp, _ in kinds.most_common():
        sample = [t for t in names if I.u32(t) == vp][:200]
        if sum(I.ptr(t + 8) in tis for t in sample) >= 0.8 * len(sample):
            kind_of[vp] = 'si'
        elif sum(I.u32(t + 12) < 16 and I.ptr(t + 16) in tis for t in sample) >= 0.8 * len(sample):
            kind_of[vp] = 'vmi'
        else:
            kind_of[vp] = 'class'
    dm = demangle(sorted(set(names.values())))
    classes = {}
    for t, raw in names.items():
        k = kind_of.get(I.u32(t), '?')
        bases = []
        if k == 'si':
            bases.append({'ti': I.ptr(t + 8), 'offset': 0})
        elif k == 'vmi':
            for i in range(I.u32(t + 12)):
                b = I.ptr(t + 16 + 8 * i)
                of = struct.unpack_from('<i', img, t + 20 + 8 * i)[0]
                bases.append({'ti': b, 'offset': of >> 8, 'flags': of & 0xFF})
        classes[t] = {'ti': t, 'mangled': raw, 'name': dm.get(raw, raw), 'kind': k, 'bases': bases, 'vtables': []}
    for c in classes.values():
        for b in c['bases']:
            b['name'] = classes[b['ti']]['name'] if b['ti'] in classes else None
    return classes, by_target


def is_code_ptr(I, o):
    return S.TEXT_START - 0x1300 <= o < S.TEXT_END


def find_vtables(I, classes, by_target):
    """vtable = [offset_to_top][typeinfo][métodos...]; o ponto de entrada é o slot depois do typeinfo"""
    owned = {}
    for t, c in classes.items():
        for r in by_target.get(t, []):
            if r - 4 < S.DATA_START or r in classes or (r - 4) in classes:
                continue
            ott = struct.unpack_from('<i', I.img, r - 4)[0]
            if ott > 0 or ott < -0x10000 or (r - 4) in I.relocs:
                continue
            ms = []
            o = r + 4
            while o + 4 <= I.n:
                p = I.ptr(o)
                if p is None:
                    if I.u32(o) == 0 and ms and False:
                        pass
                    break
                if not is_code_ptr(I, p & ~1):
                    break
                if o != r + 4 and any(o == rr + 0 for rr in by_target.get(-1, [])):
                    break
                ms.append(p)
                o += 4
            vt = {'addr': r + 4, 'offset_to_top': ott, 'methods': ms}
            c['vtables'].append(vt)
            owned[r + 4] = t
    # um vtable não pode invadir o próximo: corta no início de outro
    starts = sorted(owned)
    for t, c in classes.items():
        for vt in c['vtables']:
            nxt = [s for s in starts if s > vt['addr']]
            if nxt:
                lim = (nxt[0] - 8 - vt['addr']) // 4
                vt['methods'] = vt['methods'][:max(lim, 0)]
    return owned


# ---------------------------------------------------------------- funções

def thumb_bl(img, a):
    h1, h2 = struct.unpack_from('<HH', img, a)
    if (h1 & 0xF800) != 0xF000 or (h2 & 0xC000) != 0xC000:
        return None
    s = (h1 >> 10) & 1
    j1, j2 = (h2 >> 13) & 1, (h2 >> 11) & 1
    i1, i2 = 1 - (j1 ^ s), 1 - (j2 ^ s)
    imm = (s << 24) | (i1 << 23) | (i2 << 22) | ((h1 & 0x3FF) << 12) | ((h2 & 0x7FF) << 1)
    if s:
        imm -= 1 << 25
    if h2 & 0x1000:
        return a + 4 + imm, 'thumb'
    return ((a + 4) & ~3) + imm, 'arm'


def arm_bl(img, a):
    w = struct.unpack_from('<I', img, a)[0]
    if (w >> 25) & 7 != 5:
        return None
    cond = w >> 28
    imm = w & 0xFFFFFF
    if imm & 0x800000:
        imm -= 1 << 24
    t = a + 8 + imm * 4
    if cond == 0xF:
        return t + ((w >> 24) & 1) * 2, 'thumb'
    if w & (1 << 24):
        return t, 'arm'
    return None


def is_thumb_prologue(img, a):
    h = struct.unpack_from('<H', img, a)[0]
    if (h & 0xFF00) == 0xB500 or (h & 0xFE00) == 0xB400 and h & 0x100:
        return True
    if h == 0xE92D:                                        # push.w {..}
        h2 = struct.unpack_from('<H', img, a + 2)[0]
        return bool(h2 & 0x4000)
    return False


def is_arm_prologue(img, a):
    w = struct.unpack_from('<I', img, a)[0]
    return (w & 0xFFFF0000) == 0xE92D0000 and w & 0x4000 or w == 0xE52DE004 or w == 0xE1A0C00D


def discover(I, classes, stubs):
    img = I.img
    starts = {}                     # offset -> modo
    why = collections.defaultdict(set)

    def add(o, mode, reason):
        if S.TEXT_START <= o < S.TEXT_END:
            starts.setdefault(o, mode)
            why[o].add(reason)

    add(0x1EAAD4, 'thumb', 'main')
    for c in classes.values():
        for vt in c['vtables']:
            for p in vt['methods']:
                add(p & ~1, 'thumb' if p & 1 else 'arm', 'vtable')
    # ponteiros de código nos dados (tabelas de funções, callbacks)
    for r in I.relocs:
        p = S.off(I.u32(r))
        if S.TEXT_START <= (p & ~1) < S.TEXT_END:
            if p & 1:
                add(p & ~1, 'thumb', 'dataptr')
            elif p % 4 == 0 and is_arm_prologue(img, p):
                add(p, 'arm', 'dataptr')
    # chamadas diretas
    tcalls = collections.Counter()
    for a in range(S.TEXT_START, S.TEXT_END - 4, 2):
        r = thumb_bl(img, a)
        if r:
            tcalls[r] += 1
    acalls = collections.Counter()
    for a in range(S.TEXT_START & ~3, S.TEXT_END - 4, 4):
        r = arm_bl(img, a)
        if r:
            acalls[r] += 1
    for (t, mode), n in tcalls.items():
        if not S.TEXT_START <= t < S.TEXT_END:
            continue
        ok = is_thumb_prologue(img, t) if mode == 'thumb' else is_arm_prologue(img, t)
        if ok or n >= 3:
            add(t, mode, 'call')
    for (t, mode), n in acalls.items():
        if not S.TEXT_START <= t < S.TEXT_END:
            continue
        ok = is_thumb_prologue(img, t) if mode == 'thumb' else is_arm_prologue(img, t)
        if ok or n >= 2:
            add(t, mode, 'call')
    return starts, why


class Analyzer:
    def __init__(self, I, starts, stubs, hashes):
        self.I = I
        self.starts = starts
        self.order = sorted(starts)
        self.stubs = stubs                    # stub -> nome
        self.hashes = hashes
        self.mdt = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.mda = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        self.mdt.detail = self.mda.detail = True

    def run(self, o, end):
        I = self.I
        mode = self.starts[o]
        md = self.mdt if mode == 'thumb' else self.mda
        pcadd = 4 if mode == 'thumb' else 8
        end = min(end, o + 0x4000)
        regs = {}
        f = {'start': o, 'mode': mode, 'calls': [], 'strings': [], 'globals': [], 'hashes': [], 'imports': [],
             'funcrefs': [], 'consts': []}
        last = o
        for ins in md.disasm(I.img[o:end], o):
            last = ins.address + ins.size
            mn = ins.mnemonic
            ops = ins.operands
            # chamadas
            if mn in ('bl', 'blx') and ops and ops[0].type == ARM_OP_IMM:
                t = ops[0].imm
                if mn == 'blx':
                    t = t & ~1
                if t in self.stubs:
                    f['imports'].append(self.stubs[t])
                else:
                    f['calls'].append(t)
                continue
            if mn in ('pop', 'pop.w', 'ldm', 'ldmia.w', 'ldmia') and any(op.type == ARM_OP_REG and op.reg == ARM_REG_PC for op in ops):
                pass
            if not ops:
                continue
            d = ops[0].reg if ops[0].type == ARM_OP_REG else None
            if mn.startswith('ldr') and len(ops) == 2 and ops[1].type == ARM_OP_MEM and ops[1].mem.base == ARM_REG_PC and mn in ('ldr', 'ldr.w'):
                lit = ((ins.address + pcadd) & ~3) + ops[1].mem.disp
                if 0 <= lit < I.n - 4:
                    v = I.u32(lit)
                    regs[d] = ('lit', v)
                    if v in self.hashes:
                        f['hashes'].append(v)
                    elif 0x10000 <= v and v not in (0xFFFFFFFF,):
                        f['consts'].append(v)
                continue
            if mn in ('movw', 'mov.w', 'movs', 'mov') and len(ops) == 2 and ops[1].type == ARM_OP_IMM:
                regs[d] = ('lit', ops[1].imm & 0xFFFFFFFF)
                continue
            if mn == 'movt' and d in regs and regs[d][0] == 'lit':
                v = (regs[d][1] & 0xFFFF) | (ops[1].imm << 16)
                regs[d] = ('lit', v)
                if v in self.hashes:
                    f['hashes'].append(v)
                continue
            if mn in ('add', 'add.w') and d is not None:
                regops = [op.reg for op in ops[1:] if op.type == ARM_OP_REG]
                if ARM_REG_PC in regops:
                    other = [r for r in regops if r != ARM_REG_PC]
                    src = other[0] if other else d
                    if src in regs and regs[src][0] == 'lit':
                        a = (regs[src][1] + ins.address + pcadd) & 0xFFFFFFFF
                        regs[d] = ('addr', a)
                        self.ref(f, a)
                        continue
                regs.pop(d, None)
                continue
            if mn.startswith('ldr') and len(ops) == 2 and ops[1].type == ARM_OP_MEM and d is not None:
                m = ops[1].mem
                base = regs.get(m.base)
                idx = regs.get(m.index) if m.index else None
                if base and base[0] == 'addr':
                    slot = None
                    if idx and idx[0] == 'lit':
                        slot = base[1] + idx[1]
                    elif not m.index:
                        slot = base[1] + m.disp
                    if slot is not None and 0 <= slot < I.n:
                        slot &= 0xFFFFFFFF
                        if slot in I.imports:
                            f['imports'].append(I.imports[slot])
                            regs.pop(d, None)
                            continue
                        p = I.ptr(slot)
                        if p is not None:
                            regs[d] = ('addr', p)
                            self.ref(f, p, via=slot)
                            continue
                regs.pop(d, None)
                continue
            if d is not None and mn not in ('cmp', 'cmn', 'tst', 'teq', 'str', 'strb', 'strh', 'str.w', 'strd',
                                            'push', 'stm', 'stmdb', 'b', 'bx', 'cbz', 'cbnz', 'it', 'ite', 'itt'):
                regs.pop(d, None)
        f['end'] = last
        f['size'] = last - o
        for k in ('calls', 'strings', 'globals', 'hashes', 'imports', 'funcrefs', 'consts'):
            seen, out = set(), []
            for x in f[k]:
                key = json.dumps(x, sort_keys=True) if isinstance(x, dict) else x
                if key not in seen:
                    seen.add(key)
                    out.append(x)
            f[k] = out
        return f

    def ref(self, f, a, via=None):
        I = self.I
        if S.TEXT_START <= (a & ~1) < S.TEXT_END:
            f['funcrefs'].append(a)
            return
        if S.RODATA_START <= a < S.DATA_START:
            s = S.cstr(I.img, a)
            if s is not None and len(s) >= 2:
                f['strings'].append(s)
                return
        if 0 <= a < S.BSS_END:
            f['globals'].append(a)


def fill_gaps(I, funcs, starts, why):
    """alvos de chamadas decodificadas que caem fora de qualquer função, e prólogos nas lacunas"""
    img = I.img
    order = sorted(funcs)
    import bisect
    ends = [funcs[o]['end'] for o in order]

    def covered(a):
        i = bisect.bisect_right(order, a) - 1
        return i >= 0 and order[i] <= a < ends[i]
    added = 0
    for f in list(funcs.values()):
        for t in f['calls']:
            if t not in starts and S.TEXT_START <= t < S.TEXT_END:
                # o modo vem do tipo de chamada: blx de thumb -> arm; bl em thumb -> thumb
                starts[t] = f['mode'] if t % 4 or f['mode'] == 'thumb' else f['mode']
                why[t].add('call2')
                added += 1
    for i, o in enumerate(order):
        a = ends[i]
        lim = order[i + 1] if i + 1 < len(order) else S.TEXT_END
        mode = funcs[o]['mode']
        step = 2 if mode == 'thumb' else 4
        a = (a + step - 1) // step * step
        while a < lim - 4:
            if (mode == 'thumb' and is_thumb_prologue(img, a)) or (mode == 'arm' and is_arm_prologue(img, a)):
                if a not in starts:
                    starts[a] = mode
                    why[a].add('gap')
                    added += 1
                break
            a += step
    return added


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--s3e', default=S.DEFAULT_S3E)
    ap.add_argument('--out', default=os.path.join(HERE, 'out'))
    ap.add_argument('--cache')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    img = S.load(a.s3e, a.cache)
    I = Image(img)
    stubs = {}
    for s, g in S.plt_stubs(img):
        if g in I.imports:
            stubs[s] = I.imports[g]
    print('imports', len(stubs), file=sys.stderr)
    classes, by_target = find_rtti(I)
    vt_owner = find_vtables(I, classes, by_target)
    print('classes', len(classes), 'vtables', len(vt_owner), file=sys.stderr)
    hashes = {}
    hp = os.path.join(PORT, 'maps', 'tools', 'hashes.json')
    if os.path.exists(hp):
        hashes = {int(k): v for k, v in json.load(open(hp)).items()}
    starts, why = discover(I, classes, stubs)
    print('functions', len(starts), file=sys.stderr)
    for it in range(4):
        A = Analyzer(I, starts, stubs, hashes)
        funcs = {}
        order = A.order
        for i, o in enumerate(order):
            end = order[i + 1] if i + 1 < len(order) else S.TEXT_END
            f = A.run(o, end)
            f['why'] = sorted(why[o])
            funcs[o] = f
        added = fill_gaps(I, funcs, starts, why)
        print('pass', it, 'functions', len(funcs), 'added', added, file=sys.stderr)
        if not added:
            break
    callers = collections.defaultdict(set)
    for o, f in funcs.items():
        for t in f['calls']:
            callers[t].add(o)
    for o, f in funcs.items():
        f['callers'] = sorted(callers.get(o, ()))
    # nomes dos métodos virtuais
    vnames = collections.defaultdict(list)
    for t, c in classes.items():
        for vi, vt in enumerate(c['vtables']):
            for k, p in enumerate(vt['methods']):
                vnames[p & ~1].append('%s::vf%d%s' % (c['name'], k, '' if vi == 0 and vt['offset_to_top'] == 0 else '@%d' % (-vt['offset_to_top'])))
    for o, f in funcs.items():
        f['vnames'] = vnames.get(o, [])
    db = {
        'imports': {str(k): v for k, v in sorted(stubs.items())},
        'classes': {str(k): v for k, v in sorted(classes.items())},
        'functions': {str(k): v for k, v in sorted(funcs.items())},
    }
    json.dump(db, open(os.path.join(a.out, 'db.json'), 'w'))
    print('ok', os.path.join(a.out, 'db.json'), file=sys.stderr)


if __name__ == '__main__':
    main()
