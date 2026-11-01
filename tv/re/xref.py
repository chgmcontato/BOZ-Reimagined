#!/usr/bin/env python3
"""Índices cruzados: strings -> funções, parâmetros do console.bin -> funções, eventos (SUBJECT_*), sons (S_*),
e tabelas de strings nos dados (enums: comandos de entrada, tipos...).

Saída: docs/re/parametros.md, docs/re/eventos-e-sons.md, docs/re/tabelas.md, out/strings.csv
Uso: xref.py   (depois de analyze.py/gendocs.py; IMG_CACHE=img.bin opcional)
"""
import collections
import csv
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import s3eimg as S  # noqa: E402

OUT = os.path.join(HERE, 'out')
DOCS = os.path.join(S.PORT, 'docs', 're')


def h(x):
    return '0x%x' % x


def esc(s):
    return s.replace('|', '\\|').replace('\n', '\\n').replace('`', "'")


def main():
    img = S.load(cache=os.environ.get('IMG_CACHE'))
    db = json.load(open(os.path.join(OUT, 'db.json')))
    sym = {int(k, 16): v for k, v in json.load(open(os.path.join(OUT, 'sym_full.json'))).items()}
    F = {int(k): v for k, v in db['functions'].items()}

    def nm(o):
        return sym.get(o, {}).get('name') or 'sub_%x' % o

    def mod(o):
        return sym.get(o, {}).get('module', '?')
    users = collections.defaultdict(set)
    for o, f in F.items():
        for s in f['strings']:
            users[s].add(o)
    with open(os.path.join(OUT, 'strings.csv'), 'w', newline='') as fp:
        w = csv.writer(fp)
        w.writerow(['string', 'funções'])
        for s in sorted(users):
            w.writerow([s, ' '.join('%s:%s' % (h(o), nm(o)) for o in sorted(users[s]))])

    # ---------- parâmetros do console
    params = []
    cp = os.path.join(S.PORT, 'maps', 'docs', 'console-params.txt')
    for line in open(cp, encoding='latin1'):
        if ' = ' in line:
            k, v = line.rstrip('\n').split(' = ', 1)
            params.append((k, v))
    L = ['# Parâmetros do console.bin e onde o código os lê', '',
         'Valores de `maps/docs/console-params.txt` (OBB atual). O código lê cada parâmetro pelo nome (string); a '
         'coluna "funções" mostra quem cita o nome — é ali que o valor é usado. Parâmetros sem função: lidos por '
         'tabela/hash ou sem uso.', '',
         '| parâmetro | valor | funções |', '|---|---|---|']
    nouse = 0
    for k, v in sorted(params, key=lambda x: x[0].lower()):
        us = sorted(users.get(k, ()))
        if not us:
            nouse += 1
        L.append('| `%s` | %s | %s |' % (esc(k), esc(v[:60]), ', '.join('`%s` %s' % (h(o), esc(nm(o))) for o in us[:6])))
    L.insert(4, '%d parâmetros; %d sem referência direta no código.' % (len(params), nouse))
    L.insert(5, '')
    open(os.path.join(DOCS, 'parametros.md'), 'w').write('\n'.join(L) + '\n')

    # ---------- eventos e sons
    L = ['# Eventos (SUBJECT_*), sons (S_*) e outras chaves', '',
         'Strings citadas pelo código, com as funções que as usam. Os `SUBJECT_*` são mensagens do sistema '
         'CIsSubject/CIsObserver (broadcast entre componentes); os `S_*` são apelidos de som.', '']
    cats = [('SUBJECT_* / ON_SUBJECT_* (eventos)', r'^(ON_)?SUBJECT_'), ('S_* (sons)', r'^S_[A-Z0-9_]+$'),
            ('ig_* / páginas e elementos de UI', r'^(ig_|fe_|hud_|HUD_)'),
            ('Arquivos e caminhos', r'\.(group|bin|swf|tga|png|ogg|wav|raw|dat|itx|mtl|gfx)\b|/')]
    for title, rx in cats:
        ks = sorted(s for s in users if re.search(rx, s))
        L += ['## %s (%d)' % (title, len(ks)), '', '| string | funções |', '|---|---|']
        for s in ks:
            us = sorted(users[s])
            L.append('| `%s` | %s |' % (esc(s[:80]), ', '.join('`%s` %s' % (h(o), esc(nm(o))) for o in us[:6]) +
                                       (' +%d' % (len(us) - 6) if len(us) > 6 else '')))
        L.append('')
    open(os.path.join(DOCS, 'eventos-e-sons.md'), 'w').write('\n'.join(L) + '\n')

    # ---------- tabelas de strings nos dados (enums)
    relocs = S.relocs(img)
    rs = set(relocs)

    def strp(a):
        if a not in rs:
            return None
        v = struct.unpack_from('<I', img, a)[0] - S.PTR_BIAS
        if S.RODATA_START <= v < S.DATA_START:
            return S.cstr(img, v)
        return None
    gref = collections.defaultdict(set)
    for o, f in F.items():
        for g in f['globals']:
            gref[g].add(o)
    tables = []
    a = S.DATA_START
    end = len(img) - 4
    while a < end:
        s = strp(a)
        if s is None:
            a += 4
            continue
        items = []
        b = a
        while b < end:
            s = strp(b)
            if s is None:
                break
            items.append(s)
            b += 4
        if len(items) >= 4:
            tables.append((a, items))
        a = b + 4
    # tabelas com passo maior (struct {char*, valor...}) ficam de fora; aqui só arrays de char*
    L = ['# Tabelas de strings nos dados (enums, nomes por índice)', '',
         'Arrays de ponteiros para string (>= 4 seguidos) na área de dados. O índice é o valor do enum — por '
         'exemplo, os comandos de entrada (`giveweapon`=0 … `toggleFreeMode`=17) usados pelo tratador '
         '`CInputManager::OnCommand` (0x1dbe2c). "usada por" = funções que carregam o endereço da tabela.', '']
    for a, items in tables:
        us = sorted(gref.get(a, ()))
        L.append('## `%s` (%d itens)%s' % (h(a), len(items), ' — usada por ' + ', '.join(
            '`%s` %s' % (h(o), esc(nm(o))) for o in us[:5]) if us else ''))
        L.append('')
        L.append(', '.join('%d `%s`' % (i, esc(x[:40])) for i, x in enumerate(items[:300])) + (' …' if len(items) > 300 else ''))
        L.append('')
    open(os.path.join(DOCS, 'tabelas.md'), 'w').write('\n'.join(L) + '\n')
    print('strings', len(users), 'params', len(params), 'sem uso', nouse, 'tabelas', len(tables))


if __name__ == '__main__':
    main()
