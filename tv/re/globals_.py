#!/usr/bin/env python3
"""Instâncias globais: ponteiros dos singletons (CIsSingleton<T>) e objetos estáticos com vtable gravado na bss
(tabelas de componentes CIsComponentTable<T>, CIsReflectClass<T>, sistemas...).
Saída: out/globals.json {offset: {"class": nome, "kind": "singleton-ptr"|"static", "by": função}}
Uso: globals_.py  (PYLIB, IMG_CACHE)
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emu  # noqa: E402
import s3eimg as S  # noqa: E402

FILE_END = 0x452BAC


def main():
    db = json.load(open(os.path.join(HERE, 'out', 'db.json')))
    img = S.load(cache=os.environ.get('IMG_CACHE'))
    ctx = emu.Ctx(img, db)
    out = {}
    # singletons: o destrutor de CIsSingleton<T> zera o ponteiro da instância (único global na bss que ele usa)
    for c in db['classes'].values():
        if c['name'].startswith('CIsSingleton<') and c['vtables']:
            p = c['vtables'][0]['methods'][0] & ~1
            bss = [g for g in ctx.F.get(p, {}).get('globals', []) if g >= FILE_END]
            if len(bss) == 1:
                out[bss[0]] = {'class': c['name'][13:-1], 'kind': 'singleton-ptr', 'by': p}
    # objetos estáticos: funções que gravam um vtable num endereço da bss
    for o, f in ctx.F.items():
        if f['mode'] != 'thumb' or not any(g >= FILE_END for g in f['globals']):
            continue
        if not any(g + 8 in ctx.vt or g in ctx.vt for g in f['globals']):
            continue
        st = []
        emu.run(ctx, o, set(), st)
        for a, where, v in st:
            if where >= FILE_END and isinstance(v, tuple) and v[0] == 'addr' and v[1] in ctx.vt:
                out.setdefault(where, {'class': ctx.vt[v[1]], 'kind': 'static', 'by': o})
    # tabelas de componentes e sistemas estáticos: o global da bss mais usado pelos métodos do próprio vtable
    import collections
    for c in db['classes'].values():
        n = c['name']
        if not n.startswith(('CIsComponentTable<', 'CIsSystemStatic<', 'CIsObjectPool<')) or not c['vtables']:
            continue
        cnt = collections.Counter()
        for p in c['vtables'][0]['methods']:
            for g in ctx.F.get(p & ~1, {}).get('globals', []):
                if g >= FILE_END and g not in out:
                    cnt[g] += 1
        if cnt:
            top = cnt.most_common(2)
            g, k = top[0]
            if len(top) == 1 or k > top[1][1]:
                out[g] = {'class': n, 'kind': 'static?', 'by': c['vtables'][0]['methods'][0] & ~1}
    json.dump({'0x%x' % k: v for k, v in sorted(out.items())}, open(os.path.join(HERE, 'out', 'globals.json'), 'w'), indent=1)
    print('globais', len(out), 'singletons', sum(1 for v in out.values() if v['kind'] == 'singleton-ptr'))


if __name__ == '__main__':
    main()
