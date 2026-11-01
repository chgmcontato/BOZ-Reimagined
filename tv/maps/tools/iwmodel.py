#!/usr/bin/env python3
"""Lê os blocos de CIwModel de um .group.bin do COD:BOZ e exporta OBJ (formato em docs/formato-group-bin.md).
Os blocos são achados pelo par "hash da classe, hash do nome"; cada sequência Verts..TriList vira uma malha.
Uso: iwmodel.py <sala.group.bin> <saida.obj>"""
import struct
import sys


def djb2(s):
    h = 5381
    for c in s.lower().encode():
        h = (h * 33 + c) & 0xffffffff
    return h


VERTS, UVS, UVS2, COLS, NORMS, TRIS = (djb2(n) for n in (
    'CIwModelBlockVerts', 'CIwModelBlockGLUVs', 'CIwModelBlockGLUVs2', 'CIwModelBlockCols', 'CIwModelBlockNorms',
    'CIwModelBlockGLTriList'))


class Reader:
    def __init__(self, b, p):
        self.b, self.p = b, p

    def u8(self):
        v = self.b[self.p]; self.p += 1; return v

    def u16(self):
        v, = struct.unpack_from('<H', self.b, self.p); self.p += 2; return v

    def u32(self):
        v, = struct.unpack_from('<I', self.b, self.p); self.p += 4; return v

    def s16s(self, n):
        v = struct.unpack_from('<%dh' % n, self.b, self.p); self.p += 2 * n; return list(v)

    def u16s(self, n):
        v = struct.unpack_from('<%dH' % n, self.b, self.p); self.p += 2 * n; return list(v)

    def bytes_(self, n):
        v = self.b[self.p:self.p + n]; self.p += n; return v


def block_header(r):
    """hash da classe, hash do nome, u16 tamanho em memória, u16 nº elementos, u16 extra"""
    cls, name = r.u32(), r.u32()
    size, count, extra = r.u16(), r.u16(), r.u16()
    return cls, count


def read_verts(r, count):
    n = r.u16()
    center = struct.unpack('<h', struct.pack('<H', r.u16()))[0]
    comps = [[v + center for v in r.s16s(n)] for _ in range(3)]
    pos = list(zip(*comps))
    if n != count:
        remap = r.u16s(count - n)
        pos += [pos[i] for i in remap]
    return pos


def read_uvs(r, count):
    v = r.s16s(2 * count)
    return list(zip(v[0::2], v[1::2]))


def read_cols(r, count):
    mode = r.u8()
    if mode & 1:
        g = r.bytes_(count)
        return [(x, x, x, 255) for x in g]
    if mode & 4:
        out = []
        for c in r.u16s(count):
            out.append(((c & 0x1f) << 3, (c >> 5 & 0x3f) << 2, (c >> 11) << 3, 255))
        if mode & 2:
            a = r.bytes_(count)
            out = [(x[0], x[1], x[2], a[i]) for i, x in enumerate(out)]
        return out
    ch = [r.bytes_(count) for _ in range(4)]
    return list(zip(*ch))


def read_tris(r, count):
    mat = r.u32()
    return mat, r.u16s(count)


def parse(b):
    """Devolve malhas [{pos, uv, uv2, col, tris:[(mat, idx)]}] na ordem do arquivo."""
    meshes, cur = [], None
    i = 0
    while i < len(b) - 8:
        h, h2 = struct.unpack_from('<II', b, i)
        if h != h2 or h not in (VERTS, UVS, UVS2, COLS, TRIS):
            i += 1
            continue
        r = Reader(b, i)
        cls, count = block_header(r)
        try:
            if cls == VERTS:
                cur = {'pos': read_verts(r, count), 'uv': None, 'uv2': None, 'col': None, 'tris': []}
                meshes.append(cur)
            elif cur is None:
                pass
            elif cls == UVS:
                cur['uv'] = read_uvs(r, count)
            elif cls == UVS2:
                cur['uv2'] = read_uvs(r, count)
            elif cls == COLS:
                cur['col'] = read_cols(r, count)
            elif cls == TRIS:
                cur['tris'].append(read_tris(r, count))
        except struct.error:
            r.p = i + 1
        i = max(r.p, i + 1)
    return meshes


def write_obj(meshes, path, scale=1.0):
    with open(path, 'w') as f:
        base = 1
        for m, mesh in enumerate(meshes):
            n = len(mesh['pos'])
            f.write('o mesh%d\n' % m)
            for k, p in enumerate(mesh['pos']):
                c = mesh['col'][k] if mesh['col'] and k < len(mesh['col']) else (255, 255, 255, 255)
                f.write('v %g %g %g %.3f %.3f %.3f\n' % (p[0] * scale, p[1] * scale, p[2] * scale,
                                                       c[0] / 255, c[1] / 255, c[2] / 255))
            for k in range(n):
                uv = mesh['uv'][k] if mesh['uv'] and k < len(mesh['uv']) else (0, 0)
                f.write('vt %g %g\n' % (uv[0] / 4096.0, 1 - uv[1] / 4096.0))
            for mat, idx in mesh['tris']:
                for t in range(0, len(idx) - 2, 3):
                    a, bb, c = (base + idx[t + j] for j in range(3))
                    if max(idx[t:t + 3]) < n:
                        f.write('f %d/%d %d/%d %d/%d\n' % (a, a, bb, bb, c, c))
            base += n


if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    ms = parse(data)
    print('%d malhas, %d vértices, %d triângulos' % (
        len(ms), sum(len(m['pos']) for m in ms), sum(len(t[1]) // 3 for m in ms for t in m['tris'])))
    write_obj(ms, sys.argv[2])
