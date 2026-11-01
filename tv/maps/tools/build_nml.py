#!/usr/bin/env python3
"""No Man's Land (Moon, BO1 PC) -> salas do clone "moon" (levels/moon/lobby_shared.group.bin) no OBB.

Só a área onde o jogador anda (CAIXA) entra; céu e arredores ficam de fora. Cada triângulo ganha vértices
próprios (UV embrulhado por triângulo, cabe nos 15 bits do GLUVs) e luz falsa por normal (CIwModelBlockCols cinza).
Os pedaços (<= MAX_VERTS vértices) ocupam os CIwModel do lobby do Kino, na posição do CIsEntitySpec de cada um;
os modelos que sobram ficam vazios. Texturas RGBA4444 (formato 5, como o HUD_ALL), materiais clonados do Kino.

Formato do CIwModel (ver docs/formato-group-bin.md):
  u32 flags (0x20000; 0x120000 na casca da sala, mantida do original), u16 nº vértices, u16 nº posições, 3×i32 centro, u32 raio, u32 nº blocos, blocos,
  u16 0, u16 nº vértices, u32 0, u32 nº materiais, hashes dos materiais (o u32 de cada GLTriList é o índice).
  Bloco: hash da classe 2×, u16 tamanho em memória, u16 nº elementos, u16 0, conteúdo.
Uso: build_nml.py <obb.dz> --output <novo.dz> [--max-chunks 1] [--max-tex 4] [--dump <grupo.bin>]
O OBB de origem é preservado. Colisão e navmesh continuam herdadas do clone."""
import math
import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import numpy as np
    from PIL import Image
except ImportError:
    np = Image = None
import dz  # noqa: E402
import iwgroup  # noqa: E402
import entityspec
import reflres
import modelcheck

HERE = os.path.dirname(os.path.abspath(__file__))
PC = os.path.join(HERE, '..', 'pc', 'moon')
OBJ = os.path.join(PC, 'nml', 'nml.obj')
MTL = os.path.join(PC, 'gfxworld', 'zombie_moon.mtl')
IMAGES = os.path.join(PC, 'images')
LEVEL = 'levels\\moon'
ROOM = 'lobby_shared.group.bin'

CAIXA = (12400, 16300, -16300, -12300, -900, 200)   # X, Y, Z (BO1) da área jogável
SPAWN = (13990, -14061, -652)                      # initial_spawn_points
ANCORA = (0.0, 177.0, 2700.0)                      # chão no meio do lobby do Kino (BOZ, Y para cima)
MAX_VERTS = 9000
TEX_GRANDE, TEX_PEQ, N_GRANDES = 256, 128, 12
MAX_TEX = int(os.environ.get('MAX_TEX', 64))
MAX_CHUNKS = int(os.environ.get('MAX_CHUNKS', 99))   # teste: limita a geometria
SOL = (0.35, 0.45, 0.82)                         # direção da luz (BO1, Z para cima)
ALLOW_MISSING_TEXTURES = False
SKIP = re.compile(r'sky|shadowcaster|caulk|clip|nodraw|portal|decal|blood|water_puddle', re.I)

H = iwgroup.djb2
VERTS, UVS, UVS2, COLS, TRIS = (H(n) for n in ('CIwModelBlockVerts', 'CIwModelBlockGLUVs', 'CIwModelBlockGLUVs2',
                                                'CIwModelBlockCols', 'CIwModelBlockGLTriList'))


def bo1_to_boz(p):
    """(X, Y, Z) Z para cima -> (x, y, z) Y para cima, com o spawn na âncora"""
    x, y, z = p[..., 0] - SPAWN[0], p[..., 1] - SPAWN[1], p[..., 2] - SPAWN[2]
    return np.stack([x + ANCORA[0], z + ANCORA[1], -y + ANCORA[2]], -1)


def load_obj():
    tex_of = {}
    cur = None
    for ln in open(MTL):
        t = ln.split()
        if t and t[0] == 'newmtl':
            cur = t[1]
        elif t and t[0] == 'map_Kd':
            tex_of[cur] = t[1].rsplit('.', 1)[0]
    V, VT, tris = [], [], []
    mtl = None
    for ln in open(OBJ):
        t = ln.split()
        if not t:
            continue
        if t[0] == 'v':
            V.append([float(a) for a in t[1:7]] + [1.0] * (6 - len(t[1:7])))
        elif t[0] == 'vt':
            VT.append((float(t[1]), float(t[2])))
        elif t[0] == 'usemtl':
            mtl = t[1]
        elif t[0] == 'f' and not SKIP.search(mtl or '') and mtl in tex_of:
            c = [[int(a) if a else 0 for a in q.split('/')] + [0] for q in t[1:]]
            for k in range(1, len(c) - 1):
                tris.append((tex_of[mtl], c[0], c[k], c[k + 1]))
    return np.array(V), np.array(VT), tris


def lit(n):
    return 0.42 + 0.58 * max(0.0, float(np.dot(n, SOL)))


def build_tris(V, VT, tris, flip):
    """-> {textura: [(pos3x3 BOZ, uv3x2, cinza3)]} só com triângulos dentro da CAIXA"""
    x0, x1, y0, y1, z0, z1 = CAIXA
    out = {}
    for tex, a, b, c in tris:
        P = V[[a[0] - 1, b[0] - 1, c[0] - 1]]
        cen = P[:, :3].mean(0)
        if not (x0 <= cen[0] <= x1 and y0 <= cen[1] <= y1 and z0 <= cen[2] <= z1):
            continue
        n = np.cross(P[1, :3] - P[0, :3], P[2, :3] - P[0, :3])
        ln = np.linalg.norm(n)
        if ln < 1e-6:
            continue
        g = lit(n / ln)
        uv = np.array([VT[q[1] - 1] if q[1] else (0, 0) for q in (a, b, c)])
        uv[:, 1] = 1 - uv[:, 1]
        uv -= np.floor(uv.min(0))
        uv = np.clip(uv, -7.9, 7.9)
        cols = [min(255, int(255 * g * P[k, 3:6].mean())) for k in range(3)]
        pos = bo1_to_boz(P[:, :3])
        if flip:
            pos, uv, cols = pos[::-1], uv[::-1], cols[::-1]
        out.setdefault(tex, []).append((pos, uv, cols))
    return out


# ---- texturas
def load_tex(name, size):
    path = os.path.join(IMAGES, name + '.dds')
    try:
        im = Image.open(path).convert('RGBA').resize((size, size), Image.BILINEAR)
    except Exception as e:  # noqa: BLE001
        if not ALLOW_MISSING_TEXTURES:
            raise ValueError('textura não pôde ser carregada: %s (%s)' % (path, e)) from e
        print('  textura faltando', name, e)
        im = Image.new('RGBA', (size, size), (128, 128, 128, 255))
    a = np.asarray(im).astype(np.uint16) >> 4
    px = (a[..., 0] << 12) | (a[..., 1] << 8) | (a[..., 2] << 4) | 0xF
    return px.astype('<u2').tobytes()


def texture_obj(tmpl, size, data):
    """cabeçalho de 23 bytes (formato no byte 10; larg, alt, pitch em 13), pixels, e um byte 0 no fim"""
    hdr = bytearray(tmpl[:23])
    hdr[10] = 5
    struct.pack_into('<HHH', hdr, 13, size, size, size * 2)
    return bytes(hdr) + data + b'\0'


# ---- CIwModel
def block(cls, nbytes, count, body):
    """tamanho em memória = dados arredondados a 4 + 28 (conferido nos modelos do Kino)"""
    memsize = ((nbytes + 3) & ~3) + 28
    if memsize >= 0x10000 or not 0 <= count <= 65535:
        raise ValueError('bloco excede campos u16: memória=%d, contagem=%d' % (memsize, count))
    return struct.pack('<IIHHH', cls, cls, memsize, count, 0) + body


def model_bytes(pos, uv, cols, groups, mats, flags=0x20000):
    """pos (n,3) locais, uv (n,2), cols (n,), groups [(índice do material, [índices])]"""
    n = len(pos)
    if not 1 <= n <= 65535 or pos.shape != (n, 3) or uv.shape != (n, 2) or len(cols) != n:
        raise ValueError('dimensões/contagens inválidas no modelo')
    if not np.isfinite(pos).all() or not np.isfinite(uv).all():
        raise ValueError('posições/UVs devem ser finitas')
    if np.abs(pos).max() >= 32000 or np.abs(uv * 4096).max() > 32767:
        raise ValueError('posição/UV excede o intervalo representável')
    p = np.round(pos).astype(np.int32)
    lo, hi = p.min(0), p.max(0)
    cen = (lo + hi) // 2
    rad = int(math.ceil(np.linalg.norm((hi - lo) / 2))) + 1
    blocks = []
    body = struct.pack('<HH', n, 0) + b''.join(p[:, k].astype('<i2').tobytes() for k in range(3))
    blocks.append(block(VERTS, 6 * n, n, body))
    u = np.round(uv * 4096).astype('<i2')
    blocks.append(block(UVS, 4 * n, n, u.tobytes()))
    blocks.append(block(UVS2, 4 * n, n, np.zeros((n, 2), '<i2').tobytes()))
    blocks.append(block(COLS, 4 * n, n, b'\x01' + bytes(cols)))
    for mi, idx in groups:
        blocks.append(block(TRIS, 2 * len(idx), len(idx), struct.pack('<I', mi) +
                            np.array(idx, '<u2').tobytes()))
    head = struct.pack('<IHH3iII', flags, n, n, *cen.tolist(), rad, len(blocks))
    tail = struct.pack('<HHII', 0, n, 0, len(mats)) + b''.join(struct.pack('<I', m) for m in mats)
    return head + b''.join(blocks) + tail


EMPTY = None


def empty_model(mat, flags):
    pos = np.zeros((3, 3))
    return model_bytes(pos, np.zeros((3, 2)), [0, 0, 0], [(0, [0, 1, 2])], [mat], flags)


def spec_positions(g):
    pos = {}
    for l in g.lists():
        for o in l.objects:
            if o['cls'] != entityspec.ENTITY:
                continue
            spec, rest = entityspec.parse(o['data'])
            candidates = []
            for component in spec['components']:
                if component.get('type') != H('CIsTransform'):
                    continue
                raw = component['data']
                reflected = reflres.parse(struct.pack('<I', len(raw)) + raw)
                for field in reflected['fields']:
                    if field['name'] == H('m_localPosition') and field['type'] == H('CIwFVec3'):
                        if len(field['value']) != 12:
                            raise ValueError('m_localPosition não tem 12 bytes')
                        candidates.append(struct.unpack('<3f', field['value']))
            if len(candidates) != 1:
                raise ValueError('entidade %08x sem posição única' % o['name'])
            pos[o['name']] = np.array(candidates[0])
    return pos


def main():
    global OBJ, MTL, IMAGES, LEVEL, ROOM, MAX_CHUNKS, MAX_TEX, ALLOW_MISSING_TEXTURES
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', help='OBB de origem, preservado')
    parser.add_argument('--output', help='OBB novo; padrão: <origem>.nml.dz')
    parser.add_argument('--obj', default=OBJ)
    parser.add_argument('--mtl', default=MTL)
    parser.add_argument('--images', default=IMAGES)
    parser.add_argument('--level', default='moon')
    parser.add_argument('--room', default=ROOM)
    parser.add_argument('--max-chunks', type=int, default=MAX_CHUNKS)
    parser.add_argument('--max-tex', type=int, default=MAX_TEX)
    parser.add_argument('--flip', action='store_true')
    parser.add_argument('--dump', help='grupo gerado (arquivo novo)')
    parser.add_argument('--allow-missing-textures', action='store_true', help='substituir texturas ausentes por cinza')
    args = parser.parse_args()
    if np is None or Image is None:
        parser.error('este conversor precisa de numpy e Pillow no Python utilizado; portkit não precisa deles')
    src = args.source
    dst = args.output or (src + '.nml.dz')
    outputs = [os.path.realpath(p) for p in (dst, dst + '.json', args.dump) if p]
    if len(set(outputs)) != len(outputs) or any(os.path.exists(p) or p == os.path.realpath(src) for p in outputs):
        parser.error('saídas devem ser novas, distintas e diferentes da origem')
    if not 1 <= args.max_chunks <= 65535 or not 1 <= args.max_tex <= 64:
        parser.error('max-chunks deve ser positivo; max-tex deve estar entre 1 e 64')
    if not re.fullmatch('[a-z][a-z0-9_]*', args.level) or '/' in args.room or '\\' in args.room:
        parser.error('nome de mapa/sala inválido')
    OBJ, MTL, IMAGES = args.obj, args.mtl, args.images
    LEVEL, ROOM = 'levels\\' + args.level, args.room
    MAX_CHUNKS, MAX_TEX = args.max_chunks, args.max_tex
    ALLOW_MISSING_TEXTURES = args.allow_missing_textures
    flip = args.flip
    V, VT, tris = load_obj()
    by_tex = build_tris(V, VT, tris, flip)
    order = sorted(by_tex, key=lambda t: -len(by_tex[t]))
    if not order:
        raise ValueError('nenhum triângulo utilizável dentro da caixa NML')
    keep = order[:MAX_TEX]
    print('triângulos', sum(len(v) for v in by_tex.values()), 'texturas', len(order), '-> usadas', len(keep))

    ents, dirs = dz.entries_of(src)
    gi = [i for i, e in enumerate(ents) if e[0] == LEVEL and e[1] == ROOM][0]
    g = iwgroup.Group.load(ents[gi][2])
    lists = {iwgroup.name(l.cls): l for l in g.lists()}
    generated_hashes = {H('nml_white')} | {H('nml_%s_%d' % (kind, k)) for kind in ('tex', 'mat') for k in range(64)}
    if any(o['name'] in generated_hashes for l in g.lists() for o in l.objects):
        raise ValueError('a sala já contém NML; use um clone limpo para não duplicar recursos')
    tmpl_tex = None
    for dr, n, data, di in ents:
        if n == 'ingame.group.bin' and dr == 'ingame':
            for l in iwgroup.Group.load(data).lists():
                if iwgroup.name(l.cls) == 'CIwTexture':
                    tmpl_tex = next(o['data'] for o in l.objects if o['data'][10] == 5)
    tmpl_mat = lists['CIwMaterial'].objects[0]['data']

    # texturas + materiais (o 2º slot de textura, que era o lightmap, fica com uma textura branca)
    white = H('nml_white')
    lists['CIwTexture'].objects.append({'cls': lists['CIwTexture'].cls, 'name': white,
                                        'data': texture_obj(tmpl_tex, 8, b'\xff\xff' * 64)})
    mat_of = {}
    for k, t in enumerate(keep):
        th, mh = H('nml_tex_%d' % k), H('nml_mat_%d' % k)
        size = TEX_GRANDE if k < N_GRANDES else TEX_PEQ
        lists['CIwTexture'].objects.append({'cls': lists['CIwTexture'].cls, 'name': th,
                                            'data': texture_obj(tmpl_tex, size, load_tex(t, size))})
        m = bytearray(tmpl_mat)
        struct.pack_into('<II', m, 0x1d, th, white)
        lists['CIwMaterial'].objects.append({'cls': lists['CIwMaterial'].cls, 'name': mh, 'data': bytes(m)})
        mat_of[t] = mh
    fallback = mat_of[keep[0]]
    if os.environ.get('KINO_MATS') == '1':   # teste: sem texturas/materiais novos, só a geometria
        for l in ('CIwTexture', 'CIwMaterial'):
            lists[l].objects = [o for o in lists[l].objects if not iwgroup.name(o['name']).startswith('nml')
                                and o['name'] not in set(mat_of.values()) and o['name'] != white
                                and not any(o['name'] == H('nml_tex_%d' % k) for k in range(len(keep)))]
        fallback = lists['CIwMaterial'].objects[0]['name']
        mat_of = {t: fallback for t in mat_of}

    # triângulos -> pedaços por material, até MAX_VERTS vértices cada
    chunks, cur = [], []
    for t in order:
        for tri in by_tex[t]:
            if len(cur) * 3 + 3 > MAX_VERTS:
                chunks.append(cur)
                cur = []
            cur.append((mat_of.get(t, fallback), tri))
    if cur:
        chunks.append(cur)
    full_chunk_count = len(chunks)
    chunks = chunks[:MAX_CHUNKS]
    models = lists['CIwModel'].objects
    specs = spec_positions(g)
    print('pedaços', len(chunks), 'modelos disponíveis', len(models))
    if len(chunks) > len(models):
        raise ValueError('pedaços excedem o número de modelos da sala')
    for k, o in enumerate(models):
        # flags do modelo original: 0x100000 (casca da sala) mantém as posições na CPU, que a montagem da sala
        # (0x2657ec) lê; sem ele o jogo cai no fim do loading
        flags = struct.unpack_from('<I', o['data'], 0)[0]
        if k >= len(chunks):
            o['data'] = empty_model(fallback, flags)
            continue
        origin = specs[o['name']]
        mats = sorted({m for m, _ in chunks[k]})
        mi = {m: i for i, m in enumerate(mats)}
        pos, uv, cols, groups = [], [], [], {}
        for m, (p, u, c) in chunks[k]:
            base = len(pos) // 1
            groups.setdefault(mi[m], []).extend([len(pos), len(pos) + 1, len(pos) + 2])
            pos.extend(p - origin)
            uv.extend(u)
            cols.extend(c)
        o['data'] = model_bytes(np.array(pos), np.array(uv), cols, sorted(groups.items()), mats, flags)
    data = g.dump()
    validated = []
    for o in models:
        validated.append({'name_hash': '%08x' % o['name'], **modelcheck.inspect(o['data'])})
    if iwgroup.Group.load(data).dump() != data:
        raise ValueError('grupo NML não regrava idêntico')
    old_size = len(ents[gi][2])
    ents[gi] = (ents[gi][0], ents[gi][1], data, ents[gi][3])
    if args.dump:
        with open(args.dump, 'xb') as f:
            f.write(data)
    print('%s: %d -> %d bytes' % (ROOM, old_size, len(data)))
    dz.write(dst, ents, dirs=dirs)
    with open(dst + '.json', 'x', encoding='utf-8') as f:
        json.dump({'source': os.path.abspath(src), 'output': os.path.abspath(dst),
                   'obj': os.path.abspath(OBJ), 'chunks_total': full_chunk_count,
                   'chunks_written': len(chunks), 'models': validated,
                   'runtime_tested': False, 'collision_and_navmesh': 'inherited from base map'}, f, indent=2)
    print('Gerado: %s (colisão e navegação continuam as do mapa base)' % dst)


if __name__ == '__main__':
    main()
