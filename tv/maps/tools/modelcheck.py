#!/usr/bin/env python3
"""Parser estrito do subconjunto CIwModel emitido por build_nml.py.

Modelos com blocos ainda desconhecidos são 'unsupported', nunca aprovados por
uma busca heurística. Offsets são relativos aos dados do recurso (sem nome).
"""
import struct

import iwgroup

H = iwgroup.djb2
VERTS, UVS, UVS2, COLS, TRIS = (H(n) for n in (
    'CIwModelBlockVerts', 'CIwModelBlockGLUVs', 'CIwModelBlockGLUVs2',
    'CIwModelBlockCols', 'CIwModelBlockGLTriList'))


class UnsupportedModel(ValueError):
    pass


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, n):
        iwgroup.need(self.data, self.pos, n, 'CIwModel')
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v

    def unpack(self, fmt):
        return struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))


def inspect(data):
    r = Reader(data)
    flags, nv, unique, cx, cy, cz, radius, nb = r.unpack('IHH3iII')
    if nb > (len(data) - r.pos) // 14:
        raise ValueError('contagem de blocos excede os bytes do modelo')
    blocks, triangles, material_indices = [], 0, []
    positions = []
    verts_seen = 0
    for _ in range(nb):
        at = r.pos
        cls, name, mem, count, extra = r.unpack('IIHHH')
        if cls not in (VERTS, UVS, UVS2, COLS, TRIS):
            raise UnsupportedModel('bloco %s em 0x%x' % (iwgroup.name(cls), at))
        if cls == VERTS:
            verts_seen += 1
            n, center = r.unpack('Hh')
            if n > count or count != nv or n != unique:
                raise ValueError('contagens de vértices inconsistentes')
            axes = [r.unpack('%dh' % n) for _ in range(3)]
            positions = [tuple(axes[k][i] + center for k in range(3)) for i in range(n)]
            remap = r.unpack('%dH' % (count - n))
            if any(i >= n for i in remap):
                raise ValueError('remapeamento de vértice fora do vetor')
            positions.extend(positions[i] for i in remap)
            memory_data = 6 * count
        elif cls in (UVS, UVS2):
            if count != nv:
                raise ValueError('UVs não cobrem os vértices')
            r.take(4 * count)
            memory_data = 4 * count
        elif cls == COLS:
            if count != nv:
                raise ValueError('cores não cobrem os vértices')
            mode, = r.unpack('B')
            if mode & ~7:
                raise UnsupportedModel('modo de cor desconhecido: %d' % mode)
            r.take(count if mode & 1 else (2 + bool(mode & 2)) * count if mode & 4 else 4 * count)
            memory_data = 4 * count
        else:
            mi, = r.unpack('I')
            idx = r.unpack('%dH' % count)
            if count % 3 or any(i >= nv for i in idx):
                raise ValueError('índices de triângulos inválidos')
            material_indices.append(mi)
            triangles += count // 3
            memory_data = 2 * count
        expected = ((memory_data + 3) & ~3) + 28
        if mem != expected:
            raise ValueError('%s: tamanho em memória %d, esperado %d' % (iwgroup.name(cls), mem, expected))
        blocks.append({'offset': at, 'class': iwgroup.name(cls), 'count': count,
                       'memory_bytes': mem, 'serialized_bytes': r.pos - at})
    extension_count, tail_nv, unknown, nm = r.unpack('HHII')
    if extension_count or unknown:
        raise UnsupportedModel('extensões/rodapé fora do perfil NML')
    if tail_nv != nv or verts_seen != 1:
        raise ValueError('rodapé/vetor de posições inconsistente')
    materials = list(r.unpack('%dI' % nm))
    if any(i >= nm for i in material_indices):
        raise ValueError('índice de material fora do vetor')
    if r.pos != len(data):
        raise ValueError('%d bytes não consumidos no modelo' % (len(data) - r.pos))
    return {'flags': flags, 'vertices': nv, 'unique_positions': unique, 'triangles': triangles,
            'centre': [cx, cy, cz], 'radius': radius, 'blocks': blocks, 'materials': materials,
            'bounds': [[min(p[k] for p in positions) for k in range(3)],
                       [max(p[k] for p in positions) for k in range(3)]] if positions else None,
            'block_memory_bytes': sum(b['memory_bytes'] for b in blocks)}
