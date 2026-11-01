#!/usr/bin/env python3
"""Emulação simples (só valores conhecidos) de uma função Thumb, para ver os argumentos de cada chamada.

Resolve: literais (ldr pc), endereços relativos ao pc (add rX, pc), GOT/ponteiros relocados (ldr [rB, rI]),
mov/movs/movw/movt, add/sub com imediato, objetos de `operator new` e o vtable gravado neles.
Não segue desvios: percorre a função em ordem linear (bom para construtores, registros e tabelas).

Uso:
  emu.py <função> [alvo...]       ex.: emu.py 0x1c7098 0xf9c4a   (cada chamada a 0xf9c4a com r0..r3)
  emu.py <função>                  todas as chamadas
Valores: 'str:...' string, 'vt:Classe' vtable, 'obj#n(Classe)' objeto criado com new, 'g:0x..' global,
         'fn:0x..' ponteiro de função, números em hex.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get('PYLIB'):
    sys.path.insert(0, os.environ['PYLIB'])
import s3eimg as S  # noqa: E402
from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs  # noqa: E402
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG, ARM_REG_PC, ARM_REG_SP  # noqa: E402

R0 = 66        # ARM_REG_R0 no capstone 5
NEW = 0x37D3B8


class Ctx:
    def __init__(self, img, db):
        self.img = img
        self.relocs = set(S.relocs(img))
        self.imports = S.import_slots(img)
        self.vt = {}
        for c in db['classes'].values():
            for v in c['vtables']:
                self.vt[v['addr']] = c['name']
        self.F = {int(k): v for k, v in db['functions'].items()}

    def desc(self, v):
        if v is None:
            return '?'
        if isinstance(v, tuple):
            k = v[0]
            if k == 'addr':
                a = v[1]
                if a in self.vt:
                    return 'vt:' + self.vt[a]
                if a - 8 in self.vt:
                    return 'vt-8?:' + self.vt[a - 8]
                if S.TEXT_START <= (a & ~1) < S.TEXT_END:
                    return 'fn:0x%x' % (a & ~1)
                s = S.cstr(self.img, a) if S.RODATA_START <= a < S.DATA_START else None
                if s is not None:
                    return 'str:' + repr(s)
                return 'g:0x%x' % a
            if k == 'obj':
                return 'obj#%d(%s)' % (v[1], v[2] or '?')
            if k == 'lit':
                return '0x%x' % v[1]
            if k == 'imp':
                return 'imp:' + v[1]
        if isinstance(v, int):
            return '0x%x' % v if v > 9 else str(v)
        return str(v)


def run(ctx, o, targets=None, stores=None):
    """stores: lista que recebe (endereço da instrução, endereço gravado, valor) das gravações em endereço conhecido"""
    img = ctx.img
    f = ctx.F[o]
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.detail = True
    regs = {}
    objs = {}
    out = []

    def reg(r):
        return r - R0 if R0 <= r <= R0 + 12 else {ARM_REG_SP: 13}.get(r, None)
    for ins in md.disasm(img[o:f['end']], o):
        ops = ins.operands
        mn = ins.mnemonic.split('.')[0]
        if mn in ('bl', 'blx') and ops and ops[0].type == ARM_OP_IMM:
            t = ops[0].imm & ~1 if mn == 'blx' else ops[0].imm
            if targets is None or t in targets:
                out.append((ins.address, t, [regs.get(i) for i in range(4)]))
            for i in (1, 2, 3, 12):
                regs.pop(i, None)
            if t == NEW:
                n = len(objs)
                objs[n] = None
                regs[0] = ('obj', n, None)
            else:
                regs.pop(0, None)
            continue
        if not ops or ops[0].type != ARM_OP_REG:
            continue
        d = reg(ops[0].reg)
        if d is None:
            continue
        if mn in ('mov', 'movs', 'movw') and len(ops) == 2:
            if ops[1].type == ARM_OP_IMM:
                regs[d] = ops[1].imm & 0xFFFFFFFF
            elif ops[1].type == ARM_OP_REG and reg(ops[1].reg) in regs:
                regs[d] = regs[reg(ops[1].reg)]
            else:
                regs.pop(d, None)
            continue
        if mn == 'movt' and isinstance(regs.get(d), int):
            regs[d] = (regs[d] & 0xFFFF) | (ops[1].imm << 16)
            continue
        if mn == 'ldr' and len(ops) == 2 and ops[1].type == ARM_OP_MEM:
            m = ops[1].mem
            if m.base == ARM_REG_PC:
                lit = ((ins.address + 4) & ~3) + m.disp
                regs[d] = ('lit', int.from_bytes(img[lit:lit + 4], 'little'))
                continue
            b = regs.get(reg(m.base))
            i = regs.get(reg(m.index)) if m.index else 0
            if isinstance(i, tuple) and i[0] == 'lit':
                i = i[1]
            if isinstance(b, tuple) and b[0] == 'addr' and isinstance(i, int):
                slot = (b[1] + i + (m.disp if not m.index else 0)) & 0xFFFFFFFF
                if slot in ctx.imports:
                    regs[d] = ('imp', ctx.imports[slot])
                    continue
                if slot in ctx.relocs:
                    regs[d] = ('addr', S.off(int.from_bytes(img[slot:slot + 4], 'little')))
                    continue
            regs.pop(d, None)
            continue
        if mn in ('add', 'adds', 'sub', 'subs'):
            src = regs.get(reg(ops[1].reg)) if len(ops) == 3 and ops[1].type == ARM_OP_REG else regs.get(d)
            last = ops[-1]
            if last.type == ARM_OP_REG and last.reg == ARM_REG_PC and len(ops) == 2:
                if isinstance(src, tuple) and src[0] == 'lit':
                    regs[d] = ('addr', (src[1] + ins.address + 4) & 0xFFFFFFFF)
                    continue
            if last.type == ARM_OP_IMM and isinstance(src, tuple) and src[0] == 'addr':
                k = last.imm if mn.startswith('add') else -last.imm
                regs[d] = ('addr', src[1] + k)
                continue
            if last.type == ARM_OP_IMM and isinstance(src, int):
                regs[d] = (src + (last.imm if mn.startswith('add') else -last.imm)) & 0xFFFFFFFF
                continue
            regs.pop(d, None)
            continue
        if mn == 'str' and len(ops) == 2 and ops[1].type == ARM_OP_MEM:
            m = ops[1].mem
            b = regs.get(reg(m.base))
            v = regs.get(d)
            if isinstance(b, tuple) and b[0] == 'addr' and stores is not None:
                stores.append((ins.address, b[1] + m.disp, v))
            if isinstance(b, tuple) and b[0] == 'obj' and m.disp == 0 and isinstance(v, tuple) and v[0] == 'addr':
                name = ctx.vt.get(v[1])
                if name and objs.get(b[1]) is None:
                    objs[b[1]] = name
                    for r, x in list(regs.items()):
                        if isinstance(x, tuple) and x[0] == 'obj' and x[1] == b[1]:
                            regs[r] = ('obj', b[1], name)
            continue
        if mn in ('cmp', 'cmn', 'tst', 'strb', 'strh', 'strd', 'stm', 'push', 'b', 'bx', 'cbz', 'cbnz', 'it', 'ite',
                  'itt', 'itte', 'ittt'):
            continue
        regs.pop(d, None)
    # o vtable de um objeto pode ser gravado depois da chamada: refaz as descrições
    res = []
    for a, t, rs in out:
        rs2 = []
        for v in rs:
            if isinstance(v, tuple) and v[0] == 'obj':
                v = ('obj', v[1], objs.get(v[1]))
            rs2.append(v)
        res.append((a, t, rs2))
    return res


def main():
    db = json.load(open(os.path.join(HERE, 'out', 'db.json')))
    img = S.load(cache=os.environ.get('IMG_CACHE'))
    ctx = Ctx(img, db)
    try:
        sym = {int(k, 16): v.get('name') for k, v in json.load(open(os.path.join(HERE, 'out', 'sym_full.json'))).items()}
    except OSError:
        sym = {}
    o = int(sys.argv[1], 16)
    targets = {int(x, 16) for x in sys.argv[2:]} or None
    for a, t, rs in run(ctx, o, targets):
        print('0x%x  %-40s %s' % (a, sym.get(t) or ('0x%x' % t), '  '.join('r%d=%s' % (i, ctx.desc(v)) for i, v in enumerate(rs))))


if __name__ == '__main__':
    main()
