#!/usr/bin/env python3
"""Atalhos do Xperia Play (bloco "XperiaPlayBO" do blackops_etc.dz): o recarregar vinha na tecla 126,
que o atalho global "DeviceBack" consome antes. Recarregar passa para a tecla 9 (seta esquerda) e o
tiro alternativo (cujo comando o patch_s3e.py troca por "sair da mira") fica com a tecla 15 ("2"),
que o controle toca ao soltar o LT. Ganha também um atalho novo: tecla 16 ("3", direcional direito do
controle) -> "toggleFreeMode" (comando sem uso que o patch_s3e.py transforma em "usar a claymore"); o
console.bin muda de tamanho, então o .dz é regravado com maps/tools/dz.py.
Uso: patch_binds.py <blackops_etc.dz>   (idempotente)"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "maps", "tools"))
import dz  # noqa: E402

path = sys.argv[1]
d = bytearray(open(path, "rb").read())
a = d.find(b"XperiaPlayBO\x00")
b = d.find(b"BOPlayerControlsWin\x00", a)
assert a > 0 and b > a, "bloco XperiaPlayBO não encontrado"
# chave = 1º byte do registro: 16 bytes antes de um comando +/- (chave u32 + 7 zeros + flag + hash u32),
# 15 antes de um comando simples (um zero a menos)
SWAP = [(b"+reloadChangeWeapon\x00", 16, 0x7E, 0x09), (b"alternateFire\x00", 15, 0x09, 0x0F)]
for name, back, old, new in SWAP:
    i = d.find(name, a, b) - back
    if name.startswith(b"alternate") and d[i] == 0x7E:   # versão anterior deste patch
        old = 0x7E
    if d[i] == new:
        print("já aplicado:", name.decode().strip("\0"))
        continue
    assert d[i] == old, "byte inesperado em %#x: %#x" % (i, d[i])
    d[i] = new
    print("%s: tecla %d -> %d" % (name.decode().strip("\0"), old, new))
open(path, "wb").write(d)


def djb2(s):
    h = 5381
    for c in s.lower().encode():
        h = (h * 33 + c) & 0xffffffff
    return h


# atalho novo: cópia do registro do alternateFire (comando simples), com outra tecla, hash e nome
ents, dirs = dz.entries_of(path)
for k, (dr, name, data, di) in enumerate(ents):
    a = data.find(b"XperiaPlayBO\x00")
    if a < 0:
        continue
    b = data.find(b"BOPlayerControlsWin\x00", a)
    if data.find(b"toggleFreeMode\x00", a, b) > 0:
        print("já aplicado: toggleFreeMode")
        sys.exit(0)
    alt = data.find(b"alternateFire\x00", a, b) - 15
    nxt = data.find(b"changeWeapon\x00", a, b) - 15
    rec = bytearray(data[alt:nxt])
    assert rec[0] == 0x0F and rec[15:29] == b"alternateFire\x00", rec.hex()
    rec[0] = 0x10
    rec[11:15] = struct.pack("<I", djb2("toggleFreeMode"))
    rec[15:29] = b"toggleFreeMode\x00"[:14]
    rec[29:29] = b"\x00"
    count = struct.unpack_from("<I", data, a + 13)[0]
    assert count == 11, count
    data = data[:a + 13] + struct.pack("<I", count + 1) + data[a + 17:nxt] + bytes(rec) + data[nxt:]
    ents[k] = (dr, name, data, di)
    dz.write(path, ents, dirs=dirs)
    print("toggleFreeMode: tecla 16 (atalho novo, %d atalhos)" % (count + 1))
    break
