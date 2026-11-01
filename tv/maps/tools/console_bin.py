#!/usr/bin/env python3
"""Lê o console.bin (parâmetros de ajuste do jogo): u32 nº de entradas; cada uma = hash do tipo, nome\\0, valor.
Tipos: texto (\\0), int32, float, lista de float/int (u32 nº + itens).
Uso: console_bin.py <console.bin>  -> imprime "nome = valor" (um por linha)"""
import struct
import sys

STR, INT, FLT, FLTS, INTS = 0x583577e8, 0xe2e16f9c, 0x20c45c07, 0xed4db666, 0x371f4f1b


KNOWN = {STR, INT, FLT, FLTS, INTS}
SIZES = {}


def parse(b):
    n, = struct.unpack_from('<I', b, 0)
    p, out = 4, []
    while p < len(b):   # o contador cobre a 1ª tabela; o resto do arquivo segue o mesmo formato
        t, = struct.unpack_from('<I', b, p); p += 4
        e = b.index(b'\0', p); name = b[p:e].decode('latin1'); p = e + 1
        if t == STR:
            e = b.index(b'\0', p); v = b[p:e].decode('latin1'); p = e + 1
        elif t == INT:
            v, = struct.unpack_from('<i', b, p); p += 4
        elif t == FLT:
            v, = struct.unpack_from('<f', b, p); p += 4
        elif t in (FLTS, INTS):
            k, = struct.unpack_from('<I', b, p); p += 4
            v = list(struct.unpack_from('<%d%s' % (k, 'f' if t == FLTS else 'i'), b, p)); p += 4 * k
        else:
            # tipo ainda sem nome (bool, vetores...): o tamanho é o que deixa um tipo conhecido logo depois
            size = SIZES.get(t)
            if size is None:
                for size in (1, 4, 8, 12, 16, 2, 3, 6):
                    if p + size + 4 > len(b) or struct.unpack_from('<I', b, p + size)[0] in KNOWN | set(SIZES):
                        break
                SIZES[t] = size
            raw = b[p:p + size]; p += size
            v = raw[0] if size == 1 else (list(struct.unpack('<%df' % (size // 4), raw)) if size % 4 == 0 else raw.hex())
        out.append((name, t, v))
    return out


if __name__ == '__main__':
    for name, t, v in parse(open(sys.argv[1], 'rb').read()):
        if isinstance(v, float):
            v = round(v, 4)
        print('%s = %s' % (name, v))
