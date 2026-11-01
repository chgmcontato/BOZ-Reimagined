#!/usr/bin/env python3
"""Muda parâmetros do console.bin dentro do OBB, no lugar (int/float, mesmo tamanho).
Uso: console_set.py <obb.dz> nome=valor [...]   (ex.: GFXMaxParticles=512 doBodyLifetime=8000)
Sem nome=valor: só mostra os valores atuais dos parâmetros de desempenho conhecidos."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import console_bin  # noqa: E402
import dz  # noqa: E402

PERF = ('GFXMaxParticles', 'doBodyLifetime', 'LODQuotas', 'LODQuotasLow', 'LODQuotaQualityReducer',
        'doMaxZombieSpawnPoints')


def locate(b, key):
    """offset do valor de 'key' (entrada = tipo u32 + nome\\0 + valor)"""
    k = key.encode() + b'\0'
    p = b.find(k)
    while p >= 4:
        t, = struct.unpack_from('<I', b, p - 4)
        if t in (console_bin.INT, console_bin.FLT):
            return t, p + len(k)
        p = b.find(k, p + 1)
    raise KeyError(key)


def main():
    src, sets = sys.argv[1], dict(a.split('=', 1) for a in sys.argv[2:])
    ents, dirs = dz.entries_of(src)
    out = []
    for dr, name, data, di in ents:
        if name == 'console.bin':
            vals = {n: v for n, _, v in console_bin.parse(data)}
            for k in PERF:
                print('%s = %s' % (k, vals.get(k)))
            b = bytearray(data)
            for k, v in sets.items():
                t, off = locate(data, k)
                struct.pack_into('<i' if t == console_bin.INT else '<f', b, off, int(v) if t == console_bin.INT else float(v))
                print('-> %s = %s' % (k, v))
            data = bytes(b)
        out.append((dr, name, data, di))
    if sets:
        dz.write(src, out, dirs=dirs)


if __name__ == '__main__':
    main()
