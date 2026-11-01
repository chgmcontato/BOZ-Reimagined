#!/usr/bin/env python3
"""Portais e oclusores (visibilidade por salas/setores): CIsPortal::Serialise (0x265334) e CIsOccluder::Serialise
(0x265124). Os dados (depois do hash do nome, que o iwgroup separa) são:

  CIsOccluder: u32 nº de vértices, float[3·n] (polígono), float[3] centro, float raio
  CIsPortal:   u32 nº de vértices, float[3·n], float[3] centro, float raio, u32 a, u32 b,
               sala de um lado (texto + 0), sala do outro lado (texto + 0)

Uso: portals.py <blackops_etc.dz> [mapa]   -> grafo de salas (sala - sala via portal) e teste de regravação
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import iwgroup  # noqa: E402

PORTAL, OCCLUDER = iwgroup.djb2('CIsPortal'), iwgroup.djb2('CIsOccluder')


def parse(cls, d):
    n, = struct.unpack_from('<I', d, 0)
    p = 4
    o = {'verts': [struct.unpack_from('<3f', d, p + 12 * i) for i in range(n)]}
    p += 12 * n
    o['centre'] = struct.unpack_from('<3f', d, p)
    o['radius'], = struct.unpack_from('<f', d, p + 12)
    p += 16
    if cls == PORTAL:
        o['a'], o['b'] = struct.unpack_from('<2I', d, p)
        p += 8
        e = d.index(b'\0', p)
        o['room1'] = d[p:e].decode('latin1')
        p = e + 1
        e = d.index(b'\0', p)
        o['room2'] = d[p:e].decode('latin1')
        p = e + 1
    o['rest'] = d[p:]
    return o


def build(cls, o):
    out = struct.pack('<I', len(o['verts'])) + b''.join(struct.pack('<3f', *v) for v in o['verts'])
    out += struct.pack('<3f', *o['centre']) + struct.pack('<f', o['radius'])
    if cls == PORTAL:
        out += struct.pack('<2I', o['a'], o['b']) + o['room1'].encode('latin1') + b'\0' + o['room2'].encode('latin1') + b'\0'
    return out + o['rest']


def main():
    import collections
    import dz
    ents, _ = dz.entries_of(sys.argv[1])
    flt = sys.argv[2] if len(sys.argv) > 2 else None
    ok = bad = 0
    graph = collections.defaultdict(set)
    occ = collections.Counter()
    for d, n, b, _ in ents:
        if not n.endswith('.group.bin'):
            continue
        try:
            g = iwgroup.Group.load(b)
        except Exception:
            continue
        for L in g.lists():
            for ob in L.objects:
                cls = ob.get('cls') or L.cls
                if cls not in (PORTAL, OCCLUDER):
                    continue
                o = parse(cls, ob['data'])
                if build(cls, o) == ob['data']:
                    ok += 1
                else:
                    bad += 1
                if flt and flt.lower() not in d.lower():
                    continue
                if cls == PORTAL:
                    graph[d].add(tuple(sorted((o['room1'], o['room2']))))
                else:
                    occ[d] += 1
    for d in sorted(graph):
        print('%s: %d portais, %d oclusores' % (d, len(graph[d]), occ[d]))
        for a, b in sorted(graph[d]):
            print('    %s <-> %s' % (a, b))
    print('portais/oclusores: %d regravados idênticos, %d diferentes' % (ok, bad), file=sys.stderr)


if __name__ == '__main__':
    main()
