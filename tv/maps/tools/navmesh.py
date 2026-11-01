#!/usr/bin/env python3
"""CIsNavMesh (navegação dos zumbis): o recurso é o navmesh do Recast/Detour no formato "MSET" do RecastDemo,
precedido pelos parâmetros de construção do Recast. Gerar um navmesh novo = rodar o Recast sobre a malha do mapa
com estes parâmetros e gravar o MSET (RecastDemo: Sample_TileMesh::saveAll).

  rcConfig usado pelos mapas: cellSize f, cellHeight f, agentHeight f, agentRadius f, agentMaxClimb f,
    agentMaxSlope f, regionMinSize i, regionMergeSize i, edgeMaxLen f, edgeMaxError f, vertsPerPoly i,
    detailSampleDist f, detailSampleMaxError f, tileSize i, bmin f[3], bmax f[3]        (= 20 palavras)
  MSET: 'TESM' (u32 'MSET'), u32 versão 1, u32 nº de tiles, dtNavMeshParams (orig f[3], tileWidth f,
    tileHeight f, maxTiles i, maxPolys i); por tile: u32 tileRef, u32 tamanho, dados do tile ('VAND' = DNAV v7)

Uso: navmesh.py <blackops_etc.dz>   -> mostra os parâmetros de cada navmesh e confere a regravação
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import iwgroup  # noqa: E402

CFG = ('cellSize f', 'cellHeight f', 'agentHeight f', 'agentRadius f', 'agentMaxClimb f', 'agentMaxSlope f',
       'regionMinSize i', 'regionMergeSize i', 'edgeMaxLen f', 'edgeMaxError f', 'vertsPerPoly i',
       'detailSampleDist f', 'detailSampleMaxError f', 'tileSize i')


def parse(d):
    cfg = {}
    for i, k in enumerate(CFG):
        name, t = k.split()
        cfg[name] = struct.unpack_from('<' + t, d, 4 * i)[0]
    cfg['bmin'] = struct.unpack_from('<3f', d, 56)
    cfg['bmax'] = struct.unpack_from('<3f', d, 68)
    p = 80
    assert d[p:p + 4] == b'TESM', d[p:p + 4]
    ver, n = struct.unpack_from('<2I', d, p + 4)
    params = d[p + 12:p + 40]
    p += 40
    tiles = []
    for _ in range(n):
        ref, ln = struct.unpack_from('<2I', d, p)
        tiles.append((ref, d[p + 8:p + 8 + ln]))
        p += 8 + ln
    return {'cfg': cfg, 'head': d[:80], 'version': ver, 'params': params, 'tiles': tiles, 'rest': d[p:]}


def build(nm):
    out = nm['head'] + b'TESM' + struct.pack('<2I', nm['version'], len(nm['tiles'])) + nm['params']
    for ref, t in nm['tiles']:
        out += struct.pack('<2I', ref, len(t)) + t
    return out + nm['rest']


def main():
    import dz
    ents, _ = dz.entries_of(sys.argv[1])
    for d, n, b, _ in ents:
        if not n.endswith('.group.bin'):
            continue
        try:
            g = iwgroup.Group.load(b)
        except Exception:
            continue
        for L in g.lists():
            for o in L.objects:
                if (o.get('cls') or L.cls) != iwgroup.djb2('CIsNavMesh'):
                    continue
                try:
                    nm = parse(o['data'])
                except AssertionError as e:
                    print('%s/%s: formato diferente %r' % (d, n, e))
                    continue
                same = build(nm) == o['data']
                print('%s/%s  %d tiles, regravação %s, sobra %d' % (d, n, len(nm['tiles']),
                                                                     'idêntica' if same else 'DIFERENTE', len(nm['rest'])))
                print('   ', {k: (round(v, 3) if isinstance(v, float) else v) for k, v in nm['cfg'].items()
                              if not isinstance(v, tuple)})


if __name__ == '__main__':
    main()
