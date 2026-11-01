#!/usr/bin/env python3
"""Consulta rápida dos símbolos do boz.s3e (gerados por analyze.py + reflect.py + gendocs.py).

Uso:
  sym.py 0x1cbe58            função que contém o endereço (offset na imagem), com classe, módulo, strings, chamadas
  sym.py -r 0x4a1e7ac8       idem, a partir de um endereço em execução (logcat/Frida: 0x4a000000 + offset - 0x35090)
  sym.py CWave               procura por nome de função/classe (regex, sem diferenciar maiúsculas)
  sym.py -s grenade_icon     funções que citam a string (regex)
  sym.py -c 0x205eb0         quem chama (árvore até 2 níveis)
  sym.py -f CWave            campos refletidos da classe (offset, nome, tipo)
  sym.py -v CWave            vtable(s) da classe com o endereço de cada método
  sym.py -m jogo-pickups     funções do módulo
  sym.py -p 0x205eb0         pseudocódigo C (Ghidra) da função, de out/decomp/<módulo>.c (ver decompile.sh)
  sym.py -d 0x205eb0 [n]     desmonta n instruções (precisa do capstone em PYLIB e do img.bin em IMG_CACHE ou do base.apk)
"""
import bisect
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import s3eimg as S  # noqa: E402

OUT = os.path.join(HERE, 'out')


def load():
    db = json.load(open(os.path.join(OUT, 'db.json')))
    sym = json.load(open(os.path.join(OUT, 'sym_full.json')))
    F = {int(k): v for k, v in db['functions'].items()}
    return db, F, {int(k, 16): v for k, v in sym.items()}


def h(x):
    return '0x%x' % x


def func_at(F, ks, a):
    i = bisect.bisect_right(ks, a) - 1
    return ks[i] if i >= 0 else None


def show(F, sym, o, verbose=True):
    f, s = F[o], sym.get(o, {})
    print('%s  %s  [%s, %s, %s]  %d bytes  (execução %s)' % (
        h(o), s.get('name'), s.get('module'), s.get('conf'), f['mode'], f['size'], h(S.rt(o) + (f['mode'] == 'thumb'))))
    if s.get('class'):
        print('   classe:', s['class'])
    for k in ('note', 'hint'):
        if s.get(k):
            print('   %s: %s' % (k, s[k]))
    for a, lab, note in s.get('sites', []):
        print('   ponto %s: %s — %s' % (h(a), lab, note))
    if not verbose:
        return
    if f['strings']:
        print('   strings:', ' | '.join(f['strings'][:20]))
    if f['imports']:
        print('   importações:', ', '.join(sorted(set(f['imports']))))
    if f['calls']:
        print('   chama:', ', '.join('%s %s' % (h(t), sym.get(t, {}).get('name', '?')) for t in f['calls'][:25]))
    if f['callers']:
        print('   chamada por:', ', '.join('%s %s' % (h(t), sym.get(t, {}).get('name', '?')) for t in f['callers'][:25]))
    if f['globals']:
        print('   globais:', ', '.join(h(g) for g in f['globals'][:15]))
    if f['hashes']:
        hs = json.load(open(os.path.join(S.PORT, 'maps', 'tools', 'hashes.json')))
        print('   hashes:', ', '.join('%s=%s' % (h(x), hs.get(str(x), '?')) for x in f['hashes'][:15]))


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    db, F, sym = load()
    ks = sorted(F)
    op = a[0]
    if op in ('-r',):
        o = int(a[1], 16) - S.PTR_BIAS
        print('offset na imagem:', h(o))
        a = [h(o)]
        op = a[0]
    if re.match(r'^(0x)?[0-9a-fA-F]{4,}$', op) and not op.startswith('C'):
        x = int(op, 16)
        o = func_at(F, ks, x)
        if o is None or x >= F[o]['end'] + 0x100:
            print('fora do código conhecido; dados?', h(x))
            return
        if x != o:
            print('(%s está em %s+%s)' % (h(x), h(o), h(x - o)))
        show(F, sym, o)
    elif op == '-s':
        rx = re.compile(a[1], re.I)
        for o in ks:
            if any(rx.search(s) for s in F[o]['strings']):
                show(F, sym, o, False)
                print('   strings:', ' | '.join(s for s in F[o]['strings'] if rx.search(s))[:200])
    elif op == '-c':
        o = func_at(F, ks, int(a[1], 16))

        def up(t, d):
            for c in F[t]['callers']:
                print('  ' * d + '<- %s %s' % (h(c), sym.get(c, {}).get('name')))
                if d < 2:
                    up(c, d + 1)
        show(F, sym, o, False)
        up(o, 1)
    elif op == '-f':
        rf = json.load(open(os.path.join(OUT, 'reflect.json')))['classes']
        for n, c in rf.items():
            if re.search(a[1], n, re.I):
                print('%s (registro %s)' % (n, h(c['register'])))
                for fl in c['fields']:
                    print('   +%-6s %-32s %s' % (h(fl['offset']) if fl['offset'] is not None else '?', fl['name'], fl['type']))
    elif op == '-v':
        for t, c in db['classes'].items():
            if re.fullmatch(a[1], c['name']):
                print('%s typeinfo %s bases %s' % (c['name'], h(int(t)), [b['name'] for b in c['bases']]))
                for v in c['vtables']:
                    print('  vtable %s (offset_to_top %d)' % (h(v['addr']), v['offset_to_top']))
                    for k, p in enumerate(v['methods']):
                        print('    vf%-3d +%-5s %s %s' % (k, h(4 * k), h(p & ~1), sym.get(p & ~1, {}).get('name')))
    elif op == '-m':
        for o in ks:
            if sym.get(o, {}).get('module') == a[1]:
                show(F, sym, o, False)
    elif op == '-p':
        o = func_at(F, ks, int(a[1], 16))
        mod = sym.get(o, {}).get('module')
        fp = os.path.join(OUT, 'decomp', '%s.c' % mod)
        if not os.path.exists(fp):
            print('sem pseudocódigo: rode build/re/decompile.sh')
            return
        txt = open(fp).read()
        i = txt.find('/* 0x%x ' % o)
        if i < 0:
            print('função %s não está em %s' % (h(o), fp))
            return
        j = txt.find('\n/* 0x', i + 1)
        print(txt[i:j if j > 0 else None])
    elif op == '-d':
        if os.environ.get('PYLIB'):
            sys.path.insert(0, os.environ['PYLIB'])
        from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, Cs
        img = S.load(cache=os.environ.get('IMG_CACHE'))
        x = int(a[1], 16)
        n = int(a[2]) if len(a) > 2 else 40
        o = func_at(F, ks, x)
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if F[o]['mode'] == 'thumb' else CS_MODE_ARM)
        for i, ins in enumerate(md.disasm(img[x:x + n * 4], x)):
            if i >= n:
                break
            extra = ''
            if ins.mnemonic in ('bl', 'blx') and ins.op_str.startswith('#'):
                t = int(ins.op_str[1:], 16)
                extra = '   ; ' + str(sym.get(t, {}).get('name') or db['imports'].get(str(t), ''))
            print('%s  %-8s %s%s' % (h(ins.address), ins.mnemonic, ins.op_str, extra))
    else:
        rx = re.compile(op, re.I)
        for o in ks:
            s = sym.get(o, {})
            if rx.search(s.get('name') or '') or rx.search(s.get('class') or ''):
                show(F, sym, o, False)


if __name__ == '__main__':
    main()
