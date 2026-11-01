#!/usr/bin/env python3
"""Renderiza um OBJ (v x y z r g b) em vista isométrica com cor de vértice e sombreamento por normal.
Uso: render.py <in.obj> <out.png> [eixo_cima: y|z] [azimute] [largura]"""
import sys, math
import numpy as np
from PIL import Image, ImageDraw
src, out = sys.argv[1], sys.argv[2]
up = sys.argv[3] if len(sys.argv) > 3 else 'z'
az = math.radians(float(sys.argv[4]) if len(sys.argv) > 4 else 35)
W = int(sys.argv[5]) if len(sys.argv) > 5 else 1000
V, C, F = [], [], []
for line in open(src):
    if line.startswith('v '):
        p = line.split(); V.append([float(x) for x in p[1:4]]); C.append([float(x) for x in p[4:7]] if len(p) >= 7 else [1, 1, 1])
    elif line.startswith('f '):
        F.append([int(t.split('/')[0]) - 1 for t in line.split()[1:4]])
V, C, F = np.array(V), np.array(C), np.array(F)
if up == 'y':
    V = V[:, [0, 2, 1]]
# câmera isométrica: gira no azimute e inclina 35°
ca, sa = math.cos(az), math.sin(az)
x = V[:, 0] * ca - V[:, 1] * sa
y = V[:, 0] * sa + V[:, 1] * ca
el = math.radians(35)
sx = x
sy = -(V[:, 2] * math.cos(el) - y * math.sin(el))
depth = y * math.cos(el) + V[:, 2] * math.sin(el)
mn, mx = np.array([sx.min(), sy.min()]), np.array([sx.max(), sy.max()])
sc = (W - 20) / max(mx - mn)
H = int((mx[1] - mn[1]) * sc) + 20
P = np.stack([10 + (sx - mn[0]) * sc, 10 + (sy - mn[1]) * sc], 1)
T = V[F]
n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
light = np.abs(n @ np.array([0.3, -0.5, 0.8])) * 0.6 + 0.4
order = np.argsort(-depth[F].mean(1))
img = Image.new('RGB', (W, H), (25, 25, 35)); d = ImageDraw.Draw(img)
for i in order:
    c = C[F[i]].mean(0) * light[i]
    d.polygon([tuple(P[k]) for k in F[i]], fill=tuple(int(min(255, 255 * v)) for v in c))
img.save(out)
print(out, img.size)
