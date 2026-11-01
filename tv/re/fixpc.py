#!/usr/bin/env python3
"""Pós-processa o pseudocódigo do Ghidra (out/decomp_all/*.c -> out/decomp/*.c): resolve o endereçamento
relativo ao pc que a análise deixa cru, `DAT_00206094 + 0x205fca` (literal + pc), trocando pelo nome do alvo:
string ("texto"), função, vtable/typeinfo, singleton ou global conhecido. Também troca `*(DAT_x + pc)` de
entradas da GOT pelo símbolo para onde o ponteiro aponta.
Uso: fixpc.py [entrada] [saída]
"""
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import s3eimg as S  # noqa: E402

OUT = os.path.join(HERE, 'out')


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(OUT, 'decomp_all')
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(OUT, 'decomp')
    os.makedirs(dst, exist_ok=True)
    img = S.load(cache=os.environ.get('IMG_CACHE', os.path.join(OUT, 'img.bin')))
    relocs = set(S.relocs(img))
    imports = S.import_slots(img)
    names = {}
    for k, v in json.load(open(os.path.join(OUT, 'symbols.json'))).items():
        names[int(k, 16)] = v['name']
    for k, v in json.load(open(os.path.join(OUT, 'globals.json'))).items():
        names[int(k, 16)] = ('p_' if v['kind'] == 'singleton-ptr' else 'g_') + v['class']
    db = json.load(open(os.path.join(OUT, 'db.json')))
    for c in db['classes'].values():
        for v in c['vtables']:
            names[v['addr']] = 'vtable<%s>' % c['name']
            names[v['addr'] - 8] = 'vtable<%s>-8' % c['name']
    kn = json.load(open(os.path.join(HERE, 'known.json')))
    for k, (n, _) in kn.get('data', {}).items():
        names[int(k, 16)] = n

    def u32(o):
        return struct.unpack_from('<I', img, o)[0] if 0 <= o <= len(img) - 4 else None

    def describe(a):
        a &= 0xFFFFFFFF
        if a in names:
            return names[a]
        if (a & ~1) in names:
            return names[a & ~1]
        if S.RODATA_START <= a < S.DATA_START:
            s = S.cstr(img, a)
            if s is not None:
                return json.dumps(s[:80])
        if a in imports:
            return 'GOT<%s>' % imports[a]
        if a in relocs:
            p = S.off(u32(a))
            t = describe(p) if p != a else None
            if t:
                return 'GOT->%s' % t
        return None
    rx = re.compile(r'(\*\(\w+ \*\))?\(?_?DAT_([0-9a-f]{8}) \+ (0x[0-9a-f]+)\)?')
    stats = [0, 0]
    for fn in sorted(os.listdir(src)):
        if not fn.endswith('.c'):
            continue
        txt = open(os.path.join(src, fn)).read()

        def rep(m):
            lit = u32(int(m.group(2), 16))
            if lit is None:
                return m.group(0)
            a = (lit + int(m.group(3), 16)) & 0xFFFFFFFF
            d = describe(a)
            stats[1] += 1
            if not d:
                return m.group(0) + ' /* 0x%x */' % a
            stats[0] += 1
            if m.group(1):                         # leitura do conteúdo: GOT -> alvo
                p = u32(a)
                if a in imports:
                    return imports[a]
                if a in relocs and p is not None:
                    d2 = describe(S.off(p))
                    if d2:
                        return '&' + d2 if not d2.startswith('"') else d2
                return '*(%s)' % d
            return d if d.startswith('"') else '&' + d
        txt = rx.sub(rep, txt)
        open(os.path.join(dst, fn), 'w').write(txt)
    print('referências pc resolvidas %d de %d' % tuple(stats))


if __name__ == '__main__':
    main()
