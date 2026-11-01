#!/usr/bin/env python3
"""Extrai a reflexão do jogo (CIsReflectClass<T>): para cada classe refletida, os campos com nome, offset e tipo.

O registro de cada classe é uma função assim (ex.: CWave em 0x24af74):
    r4 = CIsReflectClass<T>::Get()            (função com o vtable de CIsReflectClass<T>)
    para cada campo: p = new(0x1c); CIsReflectPropertySpec<tipo>(p, r4, "m_Nome", offset, [sp]=tam, [sp+4]=nº)
                     AddProperty(r4, p)       (0x84348)
Os nomes dos campos são os mesmos dos arquivos de dados (.group.bin: CWave m_ZombieCount...) e o offset é o do
objeto em memória — serve para Frida (ex.: [CWave+0x58] = m_ZombieCount) e para os patches.

Uso: reflect.py [db.json] -> out/reflect.json e imprime um resumo.
"""
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get('PYLIB'):
    sys.path.insert(0, os.environ['PYLIB'])
import s3eimg as S  # noqa: E402
from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs  # noqa: E402
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG, ARM_REG_PC, ARM_REG_SP  # noqa: E402


def main():
    out = os.path.join(HERE, 'out')
    dbp = sys.argv[1] if len(sys.argv) > 1 else os.path.join(out, 'db.json')
    cache = os.environ.get('IMG_CACHE')
    img = S.load(cache=cache) if cache else S.load()
    db = json.load(open(dbp))
    F = {int(k): v for k, v in db['functions'].items()}
    vt = {}
    for c in db['classes'].values():
        for v in c['vtables']:
            vt[v['addr']] = c['name']

    def vt_of(f):
        for g in f['globals']:
            for d in (8, 0):
                if g + d in vt:
                    return vt[g + d]
        return None
    # construtores de propriedade: função curta cujo vtable é CIsReflectPropertySpec<...> (ou outra spec)
    ctor = {}
    getter = {}
    for o, f in F.items():
        n = vt_of(f)
        if not n or f['size'] > 400:
            continue
        m = re.match(r'CIsReflect(\w*)PropertySpec<(.*)>$', n) or re.match(r'(CIsReflect\w*Property\w*)<?(.*?)>?$', n)
        if n.startswith('CIsReflectClass<'):
            getter[o] = n[len('CIsReflectClass<'):-1]
        elif 'Reflect' in n and 'Property' in n:
            ctor[o] = n
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.detail = True
    ADD = 0x84348          # CIsReflectClassBase::AddProperty(cls, spec)
    NEW = 0x37D3B8         # operator new
    classes = {}
    for o, f in sorted(F.items()):
        if f['mode'] != 'thumb' or ADD not in f['calls']:
            continue
        regs, stack, objs = {}, {}, {}
        cls, fields, parent = None, [], None
        for ins in md.disasm(img[o:f['end']], o):
            ops = ins.operands
            mn = ins.mnemonic
            if mn in ('bl', 'blx') and ops and ops[0].type == ARM_OP_IMM:
                t = ops[0].imm & ~1 if mn == 'blx' else ops[0].imm
                if t in getter:
                    if cls is None:
                        cls = getter[t]
                    elif parent is None:
                        parent = getter[t]
                elif t in ctor:
                    name, offs = regs.get(2), regs.get(3)
                    fields.append({'name': name if isinstance(name, str) else None,
                                   'offset': offs if isinstance(offs, int) else None,
                                   'type': ctor[t].replace('CIsReflectPropertySpec<', '').rstrip('>').strip(),
                                   'size': stack.get(0), 'count': stack.get(4)})
                elif t == ADD:
                    ob = regs.get(1)
                    if isinstance(ob, tuple) and ob[0] == 'obj' and 'vt' in objs[ob[1]]:
                        d = objs[ob[1]]
                        fields.append({'name': d.get(8) if isinstance(d.get(8), str) else None,
                                       'offset': d.get(0x14) if isinstance(d.get(0x14), int) else None,
                                       'type': d['vt'].replace('CIsReflectPropertySpec<', '').rstrip('>').strip(),
                                       'size': None, 'count': d.get(0x10) if isinstance(d.get(0x10), int) else None})
                for r in (1, 2, 3, 12, 14):
                    regs.pop(r, None)
                if t == NEW:
                    objs[len(objs)] = {}
                    regs[0] = ('obj', len(objs) - 1)
                else:
                    regs.pop(0, None)
                continue
            if not ops or ops[0].type != ARM_OP_REG:
                continue
            d = ops[0].reg - 66 if 66 <= ops[0].reg <= 78 else ops[0].reg   # ARM_REG_R0 = 66
            if mn in ('movs', 'mov', 'mov.w', 'movw') and len(ops) == 2:
                if ops[1].type == ARM_OP_IMM:
                    regs[d] = ops[1].imm
                elif ops[1].type == ARM_OP_REG and ops[1].reg - 66 in regs:
                    regs[d] = regs[ops[1].reg - 66]
                else:
                    regs.pop(d, None)
                continue
            if mn in ('ldr', 'ldr.w') and len(ops) == 2 and ops[1].type == ARM_OP_MEM and ops[1].mem.base == ARM_REG_PC:
                lit = ((ins.address + 4) & ~3) + ops[1].mem.disp
                regs[d] = ('lit', int.from_bytes(img[lit:lit + 4], 'little'))
                continue
            if mn == 'add' and len(ops) == 2 and ops[1].type == ARM_OP_REG and ops[1].reg == ARM_REG_PC:
                v = regs.get(d)
                if isinstance(v, tuple) and v[0] == 'lit':
                    a = (v[1] + ins.address + 4) & 0xFFFFFFFF
                    regs[d] = S.cstr(img, a) or ('addr', a)
                continue
            if mn in ('adds', 'add') and len(ops) == 2 and ops[1].type == ARM_OP_IMM:
                v = regs.get(d)
                if isinstance(v, tuple) and v[0] == 'addr':
                    regs[d] = ('addr', v[1] + ops[1].imm)
                elif isinstance(v, int):
                    regs[d] = v + ops[1].imm
                else:
                    regs.pop(d, None)
                continue
            if mn in ('str', 'str.w') and len(ops) == 2 and ops[1].type == ARM_OP_MEM:
                m = ops[1].mem
                if m.base == ARM_REG_SP:
                    stack[m.disp] = regs.get(d)
                else:
                    b = regs.get(m.base - 66)
                    if isinstance(b, tuple) and b[0] == 'obj':
                        objs[b[1]][m.disp] = regs.get(d)
                continue
            if mn == 'stm.w' or mn == 'stm':
                b = regs.get(d)
                if isinstance(b, tuple) and b[0] == 'obj':
                    for k, op in enumerate(ops[1:]):
                        v = regs.get(op.reg - 66)
                        objs[b[1]][4 * k] = v
                        if k == 0 and isinstance(v, tuple) and v[0] == 'addr' and v[1] in vt:
                            objs[b[1]]['vt'] = vt[v[1]]
                continue
            if mn.startswith(('str', 'cmp', 'tst', 'cmn', 'b', 'it', 'push')):
                continue
            regs.pop(d, None)
        for fl in fields:
            if isinstance(fl.get('size'), tuple):
                fl['size'] = None
        if not fields:
            continue
        key = cls or ('?@%x' % o)
        c = classes.setdefault(key, {'class': cls, 'register': o, 'parent': parent, 'fields': []})
        c['fields'].extend(fields)
    for c in classes.values():
        c['fields'].sort(key=lambda x: (x['offset'] is None, x['offset'] or 0))
    json.dump({'ctors': {str(k): v for k, v in ctor.items()}, 'classes': classes},
              open(os.path.join(out, 'reflect.json'), 'w'), indent=1)
    types = collections.Counter(fl['type'] for c in classes.values() for fl in c['fields'])
    print('classes', len(classes), 'fields', sum(len(c['fields']) for c in classes.values()))
    print(types.most_common(40))


if __name__ == '__main__':
    main()
