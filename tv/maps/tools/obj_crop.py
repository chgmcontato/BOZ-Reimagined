#!/usr/bin/env python3
"""Recorta de um OBJ (exportado pelo OpenAssetTools) as faces com algum vértice dentro de uma caixa XY.
Mantém vt/vn/usemtl/mtllib; reindexa. Uso: obj_crop.py <in.obj> <out.obj> xmin xmax ymin ymax
Materiais de céu/sombra (SKIP) ficam de fora."""
import re
import sys

SKIP = re.compile(r"sky|shadowcaster|caulk|clip|nodraw|portal")

src, out = sys.argv[1], sys.argv[2]
x0, x1, y0, y1 = map(float, sys.argv[3:7])
V, VT, VN = [], [], []
faces = []           # (mtl, [(v, vt, vn)...])
mtl, mtllib = None, None
for ln in open(src):
    t = ln.split()
    if not t:
        continue
    if t[0] == 'v':
        V.append(ln)
    elif t[0] == 'vt':
        VT.append(ln)
    elif t[0] == 'vn':
        VN.append(ln)
    elif t[0] == 'usemtl':
        mtl = t[1]
    elif t[0] == 'mtllib':
        mtllib = ln
    elif t[0] == 'f':
        idx = [[int(p) if p else 0 for p in c.split('/')] + [0, 0] for c in t[1:]]
        faces.append((mtl, idx))


def inside(i):
    p = V[i - 1].split()
    return x0 <= float(p[1]) <= x1 and y0 <= float(p[2]) <= y1


maps = ({}, {}, {})
with open(out, 'w') as f:
    if mtllib:
        f.write(mtllib)
    body, cur = [], None
    for m, idx in faces:
        if (m and SKIP.search(m)) or not any(inside(c[0]) for c in idx):
            continue
        if m != cur:
            body.append('usemtl %s\n' % m)
            cur = m
        cs = []
        for c in idx:
            r = []
            for k, src_list in enumerate((V, VT, VN)):
                if c[k]:
                    mp = maps[k]
                    if c[k] not in mp:
                        mp[c[k]] = len(mp) + 1
                    r.append(str(mp[c[k]]))
                else:
                    r.append('')
            cs.append('/'.join(r).rstrip('/'))
        body.append('f %s\n' % ' '.join(cs))
    for k, (tag, lst) in enumerate((('v', V), ('vt', VT), ('vn', VN))):
        for i, _ in sorted(maps[k].items(), key=lambda a: a[1]):
            f.write(lst[i - 1])
    f.writelines(body)
print('faces', sum(1 for b in body if b[0] == 'f'), 'verts', len(maps[0]))
