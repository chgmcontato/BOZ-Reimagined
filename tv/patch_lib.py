#!/usr/bin/env python3
"""Patches no loader do Marmalade (lib/armeabi/libs3e_android.so).
Cada patch confere os bytes originais antes de trocar.
Uso: patch_lib.py <libs3e_android.so>"""
import struct
import sys

NOP = 0xE1A00000  # mov r0, r0

PATCHES = [
    # depois de s3eCryptoVerifyRsa: "bne <Incorrect signature in s3e file>" -> nop,
    # para aceitar o boz.s3e com a config alterada (patch_s3e.py)
    (0x1097C, 0x1A000068, NOP),
]

path = sys.argv[1]
data = bytearray(open(path, "rb").read())
for off, old, new in PATCHES:
    cur = struct.unpack_from("<I", data, off)[0]
    if cur == new:
        print("já aplicado: %#x" % off)
        continue
    assert cur == old, "bytes inesperados em %#x: %#x" % (off, cur)
    struct.pack_into("<I", data, off, new)
    print("patch %#x: %#010x -> %#010x" % (off, old, new))
open(path, "wb").write(data)
