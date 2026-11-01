#!/usr/bin/env python3
"""Fábricas de classes (IwClassFactoryAdd, 0x280c20): nome de classe -> função que cria o objeto.

São as classes que o motor sabe criar a partir dos dados (.group.bin serializado e .itx): o hash do nome da
classe gravado no arquivo é procurado nesta lista. Para portar mapas, é a lista do que um grupo pode conter.
Saída: out/factories.json  ({nome: {"create": offset, "register": offset, "class": classe do vtable}})
Uso: factories.py   (PYLIB e IMG_CACHE como no emu.py)
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emu  # noqa: E402
import s3eimg as S  # noqa: E402

ADD = 0x280C20


def main():
    db = json.load(open(os.path.join(HERE, 'out', 'db.json')))
    img = S.load(cache=os.environ.get('IMG_CACHE'))
    ctx = emu.Ctx(img, db)
    out = {}
    for o, f in sorted(ctx.F.items()):
        if ADD not in f['calls'] or f['mode'] != 'thumb':
            continue
        for a, t, rs in emu.run(ctx, o, {ADD}):
            n = ctx.desc(rs[0])
            fn = rs[1]
            if not n.startswith('str:'):
                continue
            name = n[5:-1]
            cr = fn[1] & ~1 if isinstance(fn, tuple) and fn[0] == 'addr' else None
            cls = None
            if cr in ctx.F:
                for g in ctx.F[cr]['globals']:
                    for d in (8, 0):
                        if g + d in ctx.vt:
                            cls = ctx.vt[g + d]
                # a fábrica costuma só chamar o construtor: olha também nele
                if cls is None:
                    for c in ctx.F[cr]['calls']:
                        for g in ctx.F.get(c, {}).get('globals', []):
                            if g + 8 in ctx.vt:
                                cls = ctx.vt[g + 8]
            out[name] = {'create': cr, 'register': o, 'site': a, 'class': cls}
    json.dump(out, open(os.path.join(HERE, 'out', 'factories.json'), 'w'), indent=1)
    print('fábricas', len(out), 'sem função', sum(1 for v in out.values() if v['create'] is None))


if __name__ == '__main__':
    main()
