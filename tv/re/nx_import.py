#!/usr/bin/env python3
"""Importa os endereços citados no port de Switch (BOZ-NX-main, mesmo jogo 1.0.11) para known.json (seção
"nx") e gera docs/re/boz-nx.md. No BOZ-NX, "RVA" = endereço - 0x4a000000, ou seja offset = RVA + 0x35090.
Uso: nx_import.py [pasta do BOZ-NX]"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(os.path.dirname(HERE))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(PORT, 'BOZ-NX-main')
DELTA = 0x35090

hits = []
for root, _, files in os.walk(SRC):
    for fn in files:
        if not fn.endswith(('.c', '.h', '.py', '.S')):
            continue
        p = os.path.join(root, fn)
        lines = open(p, errors='replace').read().split('\n')
        for i, l in enumerate(lines):
            for m in re.finditer(r'(?:RVA[_A-Z]*\s*(?:0x)?|rva\s*==\s*|RVAs?\s+)(0x[0-9a-fA-F]{4,})|#define\s+(RVA_\w+|SPLIT_\w*RVA)\s+(0x[0-9a-fA-F]+)', l):
                v = m.group(1) or m.group(3)
                name = m.group(2)
                # contexto: o comentário em volta (até 6 linhas acima)
                ctx = ' '.join(x.strip(' /*') for x in lines[max(0, i - 4):i + 2])
                hits.append((int(v, 16) + DELTA, name, os.path.relpath(p, SRC), i + 1, re.sub(r'\s+', ' ', ctx)[:400]))
            for m in re.finditer(r'0x([0-9a-f]{5,6}):\'(\w+)\'', l):
                hits.append((int(m.group(1), 16) + DELTA, 'vtable ' + m.group(2), os.path.relpath(p, SRC), i + 1, ''))
seen = {}
for h in hits:
    seen.setdefault(h[0], h)
kp = os.path.join(HERE, 'known.json')
kn = json.load(open(kp))
kn['nx'] = {'0x%x' % o: [n or '', '%s:%d' % (f, ln), ctx] for o, (o_, n, f, ln, ctx) in sorted(seen.items())}
json.dump(kn, open(kp, 'w'), ensure_ascii=False, indent=2)
print('endereços do BOZ-NX:', len(seen))
