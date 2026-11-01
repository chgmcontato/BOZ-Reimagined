#!/usr/bin/env python3
"""Gera os símbolos (nome, classe, módulo de cada função) e a documentação em docs/re/.

Entrada: out/db.json (analyze.py), out/reflect.json (reflect.py), known.json (nomes à mão).
Saída:   out/symbols.json, out/symbols.csv, out/flags.r2 (rizin/radare2), out/symbols.js (Frida)
         docs/re/README.md, docs/re/<módulo>.md, docs/re/classes.md, docs/re/importacoes.md

Como cada função ganha classe/módulo (campo "conf"):
    known    known.json
    vtable   está no vtable da classe (a classe "dona" é a menos derivada que a contém)
    reflect  registro de reflexão / Get() da classe
    ctor     usa o vtable da classe (construtor/destrutor/fábrica)
    string   cita o nome da classe como string
    vizinho  entre duas funções da mesma classe (o compilador emite cada .cpp contíguo)
    modulo   entre duas funções do mesmo módulo
    chamadas maioria dos módulos de quem chama / é chamado
    faixa    faixa de endereço (modules.RANGES)
"""
import bisect
import collections
import csv
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import modules as M  # noqa: E402
import s3eimg as S  # noqa: E402

PORT = S.PORT
OUT = os.path.join(HERE, 'out')
DOCS = os.path.join(PORT, 'docs', 're')


def h(x):
    return '0x%x' % x


def load():
    db = json.load(open(os.path.join(OUT, 'db.json')))
    rf = json.load(open(os.path.join(OUT, 'reflect.json')))
    kn = json.load(open(os.path.join(HERE, 'known.json')))
    F = {int(k): v for k, v in db['functions'].items()}
    C = {int(k): v for k, v in db['classes'].items()}
    imports = {int(k): v for k, v in db['imports'].items()}
    return F, C, imports, rf, kn


# slots do vtable de CIwManaged (Marmalade), conferidos pelo tamanho/efeito das implementações:
# vf9 grava IwHashString(nome) em +4; vf10 devolve a string do nome da classe; vf5 é a maior em CIwTexture/
# CIwMaterial/CIwModel (lê o arquivo); vf6 resolve ponteiros depois da carga
MANAGED_SLOTS = {0: 'dtor', 1: 'dtor_delete', 5: 'Serialise', 6: 'Resolve', 9: 'SetName', 10: 'GetClassName'}


def class_depth(C):
    memo = {}

    def d(t, seen=()):
        if t in memo:
            return memo[t]
        c = C.get(t)
        if not c or not c['bases'] or t in seen:
            memo[t] = 0
            return 0
        memo[t] = 1 + max(d(b['ti'], seen + (t,)) for b in c['bases'] if b['ti'] is not None) if any(
            b['ti'] is not None for b in c['bases']) else 0
        return memo[t]
    for t in C:
        d(t)
    return memo


def assign(F, C, rf, kn):
    depth = class_depth(C)
    byname = {c['name']: t for t, c in C.items()}
    vt_addr = {}
    for t, c in C.items():
        for v in c['vtables']:
            vt_addr[v['addr']] = t
    sym = {}

    def put(o, cls, conf, name=None, note=None):
        if o in sym and sym[o].get('conf') in ('known', 'vtable', 'reflect'):
            return
        sym[o] = {'class': cls, 'conf': conf, 'name': name, 'note': note}

    # vtable: dono = classe menos derivada que tem o método; o slot vem do vtable primário dela
    owners = collections.defaultdict(list)
    for t, c in C.items():
        for vi, v in enumerate(c['vtables']):
            for k, p in enumerate(v['methods']):
                owners[p & ~1].append((depth.get(t, 0), vi != 0 or v['offset_to_top'] != 0, k, c['name'], t))
    anc = {}

    def ancestors(t, seen=()):
        if t in anc:
            return anc[t]
        out = set()
        for b in C.get(t, {}).get('bases', []):
            if b['ti'] in C and b['ti'] not in seen:
                out.add(C[b['ti']]['name'])
                out |= ancestors(b['ti'], seen + (t,))
        anc[t] = out
        return out
    for o, lst in owners.items():
        if o not in F:
            continue
        lst.sort()
        dep, sec, k, n, t = lst[0]
        base = M.unwrap(n) if n.startswith('CIsReflectClass') else n
        slot = None
        if not sec and ('CIwManaged' in ancestors(t) or n == 'CIwManaged'):
            slot = MANAGED_SLOTS.get(k)
        put(o, n, 'vtable', '%s::%s' % (base, slot) if slot else '%s::vf%d' % (base, k),
            'também em %d vtable(s)' % (len(lst) - 1) if len(lst) > 1 else None)
    # fábricas (IwClassFactoryAdd): função que cria o objeto a partir do nome
    fp = os.path.join(OUT, 'factories.json')
    if os.path.exists(fp):
        for name, d in json.load(open(fp)).items():
            if d.get('create') in F and d['create'] not in sym:
                put(d['create'], d.get('class') or name, 'reflect', '%s::Create' % name, 'fábrica (IwClassFactoryAdd)')
    # reflexão
    for cname, c in rf['classes'].items():
        if c.get('class'):
            put(c['register'], cname, 'reflect', '%s::Reflect' % cname)
    for o, n in ((int(k), v) for k, v in rf.get('ctors', {}).items()):
        put(o, n, 'reflect', '%s::%s' % (n, n.split('<')[0]))
    # dicas fracas (não definem a classe, só o nome/nota): grava um vtable (construtor/destrutor ou objeto criado
    # dentro da função) e strings com nome de classe (no jogo, quase sempre GetComponent("CIs...") por nome)
    hint = {}
    for o, f in F.items():
        hits = []
        for g in f['globals']:
            for d in (8, 0):
                if g + d in vt_addr:
                    hits.append(C[vt_addr[g + d]]['name'])
        cs = sorted(set(x for x in f['strings'] if x in byname))
        if hits or cs:
            hint[o] = {'vt': sorted(set(hits)), 'cls': cs}
        if o in sym or not hits:
            continue
        for n in set(hits):
            if n.startswith('CIsReflectClass<') and len(set(hits)) == 1:
                put(o, n, 'reflect', 'CIsReflectClass<%s>::Get' % M.unwrap(n))
    # known.json
    for k, (n, note) in kn['functions'].items():
        o = int(k, 16)
        cls = n.split('::')[0].rstrip('?') if '::' in n else None
        old = sym.get(o) or {}
        sym[o] = {'class': cls if cls in byname else old.get('class'), 'conf': 'known', 'name': n,
                  'note': note, 'auto': old.get('name'), 'sites': old.get('sites', [])}
    # sites (pontos dentro de funções)
    order0 = sorted(F)
    nxs = {k: ('BOZ-NX ' + (v[0] or v[1]), v[2][:160]) for k, v in kn.get('nx', {}).items()}
    for k, (lab, note) in list(kn.get('sites', {}).items()) + list(nxs.items()):
        a = int(k, 16)
        i = bisect.bisect_right(order0, a) - 1
        if i < 0:
            continue
        o = order0[i]
        sym.setdefault(o, {'class': None, 'conf': None, 'name': None, 'note': None})
        sym[o].setdefault('sites', []).append([a, lab, note])
    # módulo das funções com classe
    for o, s in sym.items():
        s['module'] = M.module_of_class(s['class']) if s.get('class') else None
    order = sorted(F)
    # vizinhos: TU contígua
    labeled = [o for o in order if o in sym and sym[o].get('class')]
    for i, o in enumerate(order):
        if o in sym and sym[o].get('module'):
            continue
        j = bisect.bisect_left(labeled, o)
        a = labeled[j - 1] if j > 0 else None
        b = labeled[j] if j < len(labeled) else None
        if a is None or b is None:
            continue
        ca, cb = sym[a]['class'], sym[b]['class']
        ma, mb = sym[a]['module'], sym[b]['module']
        ent = sym.setdefault(o, {'class': None, 'conf': None, 'name': None, 'note': None})
        if M.unwrap(ca) == M.unwrap(cb):
            ent.update({'class': M.unwrap(ca), 'module': ma, 'conf': 'vizinho'})
        elif ma == mb:
            ent.update({'module': ma, 'conf': 'modulo', 'between': (M.unwrap(ca), M.unwrap(cb))})
    # chamadas: maioria
    callers = collections.defaultdict(set)
    for o, f in F.items():
        for t in f['calls']:
            callers[t].add(o)
    for _ in range(2):
        for o in order:
            ent = sym.get(o)
            if ent and ent.get('module'):
                continue
            votes = collections.Counter()
            for x in list(F[o]['calls']) + list(callers.get(o, ())):
                e = sym.get(x)
                if e and e.get('module') and e.get('conf') not in ('chamadas', 'faixa'):
                    votes[e['module']] += 1
            fallback = M.module_of_addr(o)
            if votes:
                m, n = votes.most_common(1)[0]
                tot = sum(votes.values())
                if n * 2 > tot and (M.LAYER.get(m) == M.LAYER.get(fallback) or n >= 3):
                    sym.setdefault(o, {'class': None, 'name': None, 'note': None}).update({'module': m, 'conf': 'chamadas'})
    for o in order:
        ent = sym.setdefault(o, {'class': None, 'name': None, 'note': None, 'conf': None})
        if not ent.get('module'):
            ent.update({'module': M.module_of_addr(o), 'conf': 'faixa'})
        # o jogo-outros da faixa vira o módulo dos vizinhos mais próximos quando há classe perto
    for o in order:
        ent = sym[o]
        f = F[o]
        if not ent.get('name'):
            if len(f['imports']) == 1 and not f['calls'] and f['size'] <= 24:
                ent['name'] = 'j_' + f['imports'][0]
            elif ent.get('class') and ent['conf'] == 'vizinho':
                c = M.unwrap(ent['class'])
                hv = hint.get(o, {}).get('vt', [])
                ent['name'] = ('%s::ctor_dtor_%x' if c in hv else '%s::sub_%x') % (c, o)
            else:
                ent['name'] = 'sub_%x' % o
        hn = hint.get(o)
        if hn:
            parts = []
            if hn['vt']:
                parts.append('vtable de ' + ', '.join(hn['vt'][:3]))
            if hn['cls']:
                parts.append('cita ' + ', '.join(hn['cls'][:3]))
            ent['hint'] = '; '.join(parts)
        if ent.get('between') and not ent.get('note'):
            ent['hint'] = ((ent.get('hint') or '') + ' entre %s e %s' % ent['between']).strip()
    return sym


def md_escape(s):
    return s.replace('|', '\\|').replace('\n', '\\n').replace('`', "'")


def short(lst, n=6, w=40):
    out = [md_escape(x if len(x) <= w else x[:w] + '…') for x in lst[:n]]
    return ', '.join('`%s`' % x for x in out) + (' …' if len(lst) > n else '')


def write_outputs(F, C, imports, rf, kn, sym):
    os.makedirs(DOCS, exist_ok=True)
    # ---------------- símbolos
    js = {}
    for o in sorted(F):
        s = sym[o]
        f = F[o]
        js[h(o)] = {'name': s['name'], 'class': s.get('class'), 'module': s['module'], 'conf': s['conf'],
                    'mode': f['mode'], 'size': f['size'], 'note': s.get('note'), 'hint': s.get('hint')}
    for st, n in imports.items():
        js[h(st)] = {'name': 'imp_' + n, 'class': None, 'module': 'lib-c', 'conf': 'import', 'mode': 'arm', 'size': 12, 'note': 'stub do PLT'}
    json.dump(js, open(os.path.join(OUT, 'symbols.json'), 'w'), indent=0)
    with open(os.path.join(OUT, 'symbols.csv'), 'w', newline='') as fp:
        w = csv.writer(fp)
        w.writerow(['offset', 'runtime', 'name', 'class', 'module', 'conf', 'mode', 'size', 'callers', 'calls', 'strings', 'hint'])
        for o in sorted(F):
            s, f = sym[o], F[o]
            w.writerow([h(o), h(S.rt(o)), s['name'], s.get('class') or '', s['module'], s['conf'], f['mode'], f['size'],
                        len(f['callers']), len(f['calls']), ' | '.join(f['strings'][:8]), s.get('hint') or ''])
    with open(os.path.join(OUT, 'flags.r2'), 'w') as fp:
        fp.write('# rizin/radare2: abrir a imagem crua (img.bin, offset 0 = imagem) e rodar ". flags.r2"\n')
        for o in sorted(F):
            n = re.sub(r'[^A-Za-z0-9_.]', '_', sym[o]['name'])
            fp.write('af %s @ %s\n' % (n, h(o)) if False else 'f fcn.%s %d @ %s\n' % (n, F[o]['size'], h(o)))
            if F[o]['mode'] == 'thumb':
                fp.write('ahb 16 @ %s\n' % h(o))
        for st, n in imports.items():
            fp.write('f imp.%s 12 @ %s\n' % (n, h(st)))
    with open(os.path.join(OUT, 'symbols.js'), 'w') as fp:
        fp.write('// gerado por build/re/gendocs.py — offset na imagem (somar a `base` do run.py; +1 se thumb)\n')
        fp.write('const SYM = {\n')
        for o in sorted(F):
            if sym[o]['conf'] in ('known', 'vtable', 'reflect') or '::ctor_dtor' in sym[o]['name']:
                fp.write('  %s: [%d, %s],\n' % (json.dumps(sym[o]['name']), o, 'true' if F[o]['mode'] == 'thumb' else 'false'))
        fp.write('};\nfunction symAddr(n) { const s = SYM[n]; return s ? base.add(s[0] + (s[1] ? 1 : 0)) : null; }\n')

    # ---------------- docs por módulo
    by_mod = collections.defaultdict(list)
    for o in sorted(F):
        by_mod[sym[o]['module']].append(o)
    cls_mod = collections.defaultdict(list)
    for t, c in C.items():
        cls_mod[M.module_of_class(c['name']) or 'desconhecido'].append(t)
    rclasses = rf['classes']
    vt_index = {}
    for t, c in C.items():
        for v in c['vtables']:
            vt_index[v['addr']] = c['name']

    known_data = kn.get('data', {})
    for mod, layer, title in M.MODULES:
        fs = by_mod.get(mod, [])
        cs = sorted(cls_mod.get(mod, []), key=lambda t: C[t]['name'])
        if not fs and not cs:
            continue
        L = ['# %s' % title, '', 'Módulo `%s` (camada %s). Gerado por `build/re/gendocs.py` — não editar à mão; '
             'para corrigir nomes, edite `build/re/known.json` e regere.' % (mod, layer), '']
        size = sum(F[o]['size'] for o in fs)
        rng = collections.Counter(o >> 16 for o in fs)
        L.append('- Funções: **%d** (%d KB de código); confiança: %s' % (
            len(fs), size // 1024, ', '.join('%s %d' % kv for kv in collections.Counter(sym[o]['conf'] for o in fs).most_common())))
        if fs:
            spans = []
            for o in fs:
                if spans and o - spans[-1][1] < 0x2000:
                    spans[-1][1] = F[o]['end']
                else:
                    spans.append([o, F[o]['end']])
            big = sorted(spans, key=lambda s: -(s[1] - s[0]))[:8]
            L.append('- Principais faixas: ' + ', '.join('`%s-%s`' % (h(a), h(b)) for a, b in sorted(big)))
        L.append('- Classes com RTTI: %d' % len(cs))
        L.append('')
        # classes
        if cs:
            L += ['## Classes', '']
            for t in cs:
                c = C[t]
                bases = ', '.join(b['name'] or '?' for b in c['bases'])
                L.append('### `%s`%s' % (c['name'], ' : ' + bases if bases else ''))
                L.append('')
                L.append('- typeinfo `%s`' % h(t))
                for vi, v in enumerate(c['vtables']):
                    ms = v['methods']
                    own = [(k, p & ~1) for k, p in enumerate(ms) if sym.get(p & ~1, {}).get('class') == c['name']]
                    L.append('- vtable `%s`%s: %d métodos; próprios (não herdados): %s' % (
                        h(v['addr']), ' (secundário, -%d)' % -v['offset_to_top'] if v['offset_to_top'] else '', len(ms),
                        ', '.join('vf%d(+%s) `%s`' % (k, h(4 * k), h(p)) for k, p in own[:40]) + (' …' if len(own) > 40 else '') if own else '—'))
                rc = rclasses.get(M.unwrap(c['name'])) if c['name'] == M.unwrap(c['name']) else None
                if rc:
                    L.append('- reflexão (registro `%s`): %s' % (h(rc['register']), ', '.join(
                        '`+%s %s` (%s)' % (h(fl['offset']) if fl['offset'] is not None else '?', fl['name'], fl['type'][:30])
                        for fl in rc['fields'])))
                L.append('')
        # funções relevantes
        inter = [o for o in fs if sym[o]['conf'] == 'known' or sym[o].get('sites') or F[o]['strings'] or (sym[o]['conf'] in ('vtable', 'reflect')
                                                                                   and F[o]['size'] >= 64)]
        L += ['## Funções relevantes', '',
              'Com nome à mão, com strings, ou métodos com ≥ 64 bytes. Lista completa: `build/re/out/symbols.csv` '
              '(filtrar pela coluna module = `%s`).' % mod, '',
              '| offset | nome | tam | quem chama | strings / notas |', '|---|---|---|---|---|']
        lim = 1500
        for o in inter[:lim]:
            s, f = sym[o], F[o]
            info = short(f['strings'], 5)
            if s.get('hint'):
                info = '_%s_ %s' % (md_escape(s['hint']), info)
            if s.get('note'):
                info = '**%s** %s' % (md_escape(s['note']), info)
            for a, lab, note in s.get('sites', []):
                info = '**`%s` %s**: %s; %s' % (h(a), md_escape(lab), md_escape(note), info)
            if f['imports']:
                info += ' imp: ' + ', '.join(sorted(set(f['imports']))[:4])
            L.append('| `%s` | %s | %d | %d | %s |' % (h(o), md_escape(s['name']), f['size'], len(f['callers']), info))
        if len(inter) > lim:
            L.append('| … | +%d | | | |' % (len(inter) - lim))
        open(os.path.join(DOCS, mod + '.md'), 'w').write('\n'.join(L) + '\n')

    # ---------------- classes.md (hierarquia)
    kids = collections.defaultdict(list)
    roots = []
    for t, c in C.items():
        if c['bases'] and c['bases'][0]['ti'] in C:
            kids[c['bases'][0]['ti']].append(t)
        else:
            roots.append(t)
    L = ['# Hierarquia de classes (RTTI)', '',
         'Todas as %d classes com typeinfo na imagem, pela base primária. `[módulo]` = documento em docs/re/. '
         'Modelos de reflexão/pool (CIsReflectClass<…> etc.) omitidos daqui; ficam nos módulos.' % len(C), '', '```']

    def walk(t, ind):
        c = C[t]
        if M.TEMPLATE.match(c['name']) and not kids.get(t):
            return
        extra = ''
        if len(c['bases']) > 1:
            extra = '  (+ ' + ', '.join(b['name'] or '?' for b in c['bases'][1:]) + ')'
        L.append('%s%s  [%s]%s' % ('  ' * ind, c['name'], M.module_of_class(c['name']) or '?', extra))
        for k in sorted(kids.get(t, []), key=lambda x: C[x]['name']):
            walk(k, ind + 1)
    for t in sorted(roots, key=lambda x: C[x]['name']):
        walk(t, 0)
    L.append('```')
    open(os.path.join(DOCS, 'classes.md'), 'w').write('\n'.join(L) + '\n')

    # ---------------- importacoes.md
    users = collections.defaultdict(set)
    for o, f in F.items():
        for i in f['imports']:
            users[i].add(o)
    L = ['# Importações do loader (s3e / GLES / EGL)', '',
         'O código chama as funções do `libs3e_android.so` por stubs do PLT (ARM, 0x35158..0x36380) que saltam pela GOT '
         '(0x4460bc..). Tabela de ligação no fim do cabeçalho (0x34730). `usos` = funções que chamam.', '',
         '| stub | importação | usos | exemplos |', '|---|---|---|---|']
    for st, n in sorted(imports.items(), key=lambda x: x[1]):
        us = sorted(users.get(n, ()))
        L.append('| `%s` | %s | %d | %s |' % (h(st), n, len(us), ', '.join('`%s`' % sym[u]['name'] for u in us[:4])))
    open(os.path.join(DOCS, 'importacoes.md'), 'w').write('\n'.join(L) + '\n')

    # ---------------- reflexao.md
    L = ['# Reflexão: campos e offsets das classes de dados', '',
         'Extraído de `CIsReflectClass<T>` (build/re/reflect.py). Os nomes são os mesmos dos `.group.bin` (ex.: '
         '`CWave m_ZombieCount`) e o offset é o do objeto em memória — use para Frida e patches. '
         'O offset é relativo ao início do objeto (inclui o ponteiro de vtable em +0).', '']
    for cname in sorted(rclasses):
        c = rclasses[cname]
        L.append('## %s' % cname)
        L.append('')
        L.append('Registro em `%s`; módulo `%s`.' % (h(c['register']), M.module_of_class(cname) or '?'))
        L.append('')
        L.append('| offset | campo | tipo | nº |')
        L.append('|---|---|---|---|')
        for fl in c['fields']:
            L.append('| %s | `%s` | %s | %s |' % (h(fl['offset']) if fl['offset'] is not None else '?', fl['name'],
                                                  md_escape(fl['type']), fl.get('count') or ''))
        L.append('')
    open(os.path.join(DOCS, 'reflexao.md'), 'w').write('\n'.join(L) + '\n')

    # ---------------- boz-nx.md
    if kn.get('nx'):
        order1 = sorted(F)
        L = ['# Endereços citados no port de Switch (BOZ-NX-main)', '',
             'O BOZ-NX roda o mesmo código ARM 1.0.11 na Switch e comenta vários pontos do jogo. Lá, `RVA` = '
             'endereço - 0x4a000000; aqui, offset = RVA + 0x35090. Importado por `build/re/nx_import.py`.', '',
             '| offset | função (este mapa) | onde no BOZ-NX | contexto |', '|---|---|---|---|']
        for k, (n, where, ctx) in sorted(kn['nx'].items(), key=lambda x: int(x[0], 16)):
            a = int(k, 16)
            i = bisect.bisect_right(order1, a) - 1
            fn = sym[order1[i]]['name'] if i >= 0 and a < F[order1[i]]['end'] + 4 else '(dados)'
            L.append('| `%s` | %s | %s %s | %s |' % (k, md_escape(fn), where, md_escape(n), md_escape(ctx[:300])))
        open(os.path.join(DOCS, 'boz-nx.md'), 'w').write('\n'.join(L) + '\n')

    # ---------------- modulos.md (tabela geral)
    L = ['# Módulos do código', '', 'Gerado por gendocs.py. Camadas: **jogo** (código do BOZ), **is** (framework Is da '
         'Ideaworks), **marmalade** (SDK), **terceiros**.', '',
         '| módulo | camada | funções | KB | classes | descrição |', '|---|---|---|---|---|---|']
    for mod, layer, title in M.MODULES:
        fs = by_mod.get(mod, [])
        if not fs and not cls_mod.get(mod):
            continue
        L.append('| [%s](%s.md) | %s | %d | %d | %d | %s |' % (mod, mod, layer, len(fs), sum(F[o]['size'] for o in fs) // 1024,
                                                             len(cls_mod.get(mod, [])), title))
    open(os.path.join(DOCS, 'modulos.md'), 'w').write('\n'.join(L) + '\n')

    # ---------------- globais.md
    gp = os.path.join(OUT, 'globals.json')
    if os.path.exists(gp):
        gl = {int(k, 16): v for k, v in json.load(open(gp)).items()}
        L = ['# Variáveis globais e instâncias (bss)', '',
             'A bss vai do fim do arquivo (0x452bac) até 0x4dce58. Endereços em offset na imagem: no Frida, '
             '`base.add(offset)` (run.py). Para um singleton, o global guarda um PONTEIRO para a instância '
             '(`base.add(0x4958d0).readPointer()` = CLevelManager); os demais são o próprio objeto.', '',
             '## Nomes dados à mão (known.json)', '', '| offset | nome | nota |', '|---|---|---|']
        for k, (n, note) in sorted(kn.get('data', {}).items(), key=lambda x: int(x[0], 16)):
            L.append('| `%s` | %s | %s |' % (k, md_escape(n), md_escape(note)))
        for kind, title in (('singleton-ptr', 'Singletons (ponteiro para a instância)'),
                            ('static', 'Objetos estáticos (vtable gravado na bss)'),
                            ('static?', 'Tabelas de componentes / sistemas (provável)')):
            rows = [(a, v) for a, v in sorted(gl.items()) if v['kind'] == kind]
            L += ['', '## %s (%d)' % (title, len(rows)), '', '| offset | classe | quem grava/usa |', '|---|---|---|']
            for a, v in rows:
                L.append('| `%s` | `%s` | `%s` %s |' % (h(a), md_escape(v['class']), h(v['by']),
                                                       md_escape(sym.get(v['by'], {}).get('name') or '')))
        open(os.path.join(DOCS, 'globais.md'), 'w').write('\n'.join(L) + '\n')

    # ---------------- fabricas.md
    fp = os.path.join(OUT, 'factories.json')
    if os.path.exists(fp):
        fac = json.load(open(fp))
        L = ['# Fábricas de classe (o que os dados podem criar)', '',
             'Registradas com `IwClassFactoryAdd(nome, criar)` (0x280c20). Ao carregar um `.group.bin`/`.itx`, o motor '
             'procura o hash do nome da classe nesta lista e chama a função de criação; o objeto então lê seus dados '
             'em `Serialise` (vf5 de CIwManaged). Componentes de jogo (CBarricade, CDoor…) não estão aqui: entram '
             'pelas specs de entidade (`CIsEntitySpec_*`) e pela reflexão (ver reflexao.md).', '',
             '| classe | criar | registro | vtable | módulo |', '|---|---|---|---|---|']
        for name in sorted(fac):
            d = fac[name]
            L.append('| `%s` | %s | `%s` | %s | %s |' % (name, '`%s`' % h(d['create']) if d['create'] else '?',
                     h(d['register']), d.get('class') or '', M.module_of_class(name) or ''))
        open(os.path.join(DOCS, 'fabricas.md'), 'w').write('\n'.join(L) + '\n')

    # ---------------- unidades.md: cada .cpp (sequência contígua de funções da mesma classe)
    units = []
    for o in sorted(F):
        s = sym[o]
        c = M.unwrap(s['class']) if s.get('class') and s['conf'] in ('vtable', 'reflect', 'vizinho', 'known') else None
        key = c or ('?' + s['module'])
        if units and (units[-1]['key'] == key or (c is None and units[-1]['key'].startswith('?') is False and False)):
            units[-1]['end'] = F[o]['end']
            units[-1]['n'] += 1
            units[-1]['strings'] += F[o]['strings'][:2]
        else:
            units.append({'key': key, 'cls': c, 'module': s['module'], 'start': o, 'end': F[o]['end'], 'n': 1,
                          'strings': list(F[o]['strings'][:2])})
    # junta unidades sem classe pequenas entre duas da mesma classe
    merged = []
    for u in units:
        if (len(merged) >= 2 and merged[-1]['cls'] is None and merged[-1]['n'] <= 3 and merged[-2]['cls'] == u['cls']
                and u['cls']):
            mid = merged.pop()
            merged[-1]['end'] = u['end']
            merged[-1]['n'] += mid['n'] + u['n']
            merged[-1]['strings'] += mid['strings'] + u['strings']
            continue
        if merged and merged[-1]['key'] == u['key']:
            merged[-1]['end'] = u['end']
            merged[-1]['n'] += u['n']
            merged[-1]['strings'] += u['strings']
            continue
        merged.append(u)
    # suavização: unidades pequenas (<= 4 funções) entre duas da mesma classe são absorvidas; sem classe e com o
    # mesmo módulo do anterior, também (são funções inline/modelos emitidos no meio do .cpp)
    changed = True
    while changed:
        changed = False
        out = []
        i = 0
        while i < len(merged):
            u = merged[i]
            if (out and i + 1 < len(merged) and u['n'] <= 4 and out[-1]['cls'] and out[-1]['cls'] == merged[i + 1]['cls']):
                nx = merged[i + 1]
                out[-1]['end'] = nx['end']
                out[-1]['n'] += u['n'] + nx['n']
                out[-1]['strings'] += u['strings'] + nx['strings']
                i += 2
                changed = True
                continue
            if out and u['cls'] is None and u['module'] == out[-1]['module'] and u['n'] <= 4:
                out[-1]['end'] = u['end']
                out[-1]['n'] += u['n']
                out[-1]['strings'] += u['strings']
                i += 1
                changed = True
                continue
            out.append(u)
            i += 1
        merged = out
    L = ['# Mapa do código por unidade (classe/arquivo) e endereço', '',
         'Cada linha é uma sequência contígua de funções da mesma classe (o compilador emite cada .cpp junto; no código '
         'do jogo, 0x138000..0x258000, os arquivos vêm em ordem alfabética: CAIController… → CZombieConfiguration). '
         'Linhas `?módulo` = funções sem classe identificada (atribuídas por vizinhança/chamadas/faixa).', '',
         '| início | fim | funções | unidade | módulo | strings |', '|---|---|---|---|---|---|']
    for u in merged:
        uniq = []
        for x in u['strings']:
            if x not in uniq:
                uniq.append(x)
        L.append('| `%s` | `%s` | %d | %s | [%s](%s.md) | %s |' % (h(u['start']), h(u['end']), u['n'],
                 md_escape(u['cls'] or u['key']), u['module'], u['module'], short(uniq, 4, 28)))
    open(os.path.join(DOCS, 'unidades.md'), 'w').write('\n'.join(L) + '\n')
    json.dump([{k: v for k, v in u.items() if k != 'strings'} for u in merged], open(os.path.join(OUT, 'units.json'), 'w'))
    return by_mod, cls_mod


def main():
    F, C, imports, rf, kn = load()
    sym = assign(F, C, rf, kn)
    by_mod, cls_mod = write_outputs(F, C, imports, rf, kn, sym)
    conf = collections.Counter(s['conf'] for s in sym.values())
    print('funções', len(F), 'confiança', dict(conf))
    for mod, layer, title in M.MODULES:
        print('%-22s %6d funções %4d classes' % (mod, len(by_mod.get(mod, [])), len(cls_mod.get(mod, []))))
    # conferência dos nomes à mão contra o nome automático
    for k, (n, note) in kn['functions'].items():
        o = int(k, 16)
        if o in sym and sym[o].get('auto') and sym[o]['auto'] != n:
            print('known %s: %s  (auto: %s)' % (k, n, sym[o]['auto']))
        if o not in F:
            print('known %s: %s  (não é início de função na análise)' % (k, n))
    json.dump({h(o): s for o, s in sym.items()}, open(os.path.join(OUT, 'sym_full.json'), 'w'))


if __name__ == '__main__':
    main()
