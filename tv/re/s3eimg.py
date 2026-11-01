#!/usr/bin/env python3
"""Leitura da imagem do jogo (assets/boz.s3e) e modelo de endereços.

Todos os endereços desta pasta são OFFSETS NA IMAGEM (o mesmo usado em patch_s3e.py, RESUMO.md e nos
comentários): imagem = binário descomprimido a partir de 0x43F3.

    em execução (Frida/logcat)  = 0x4a000000 + offset - 0x35090
    offset                      = em execução - 0x4a000000 + 0x35090
    ponteiro gravado nos dados  = endereço em execução (a imagem é carregada em 0x4a000000 - 0x35090)

Layout (medido em 1.0.11):
    0x000000  cabeçalho de importação: u32 0, u32 tamanho, u16 nº (391), nomes s3e*/gl*/egl* terminados em 0
    0x001960  tabela de relocações (u32 1, u32 tam, u32 nº=52075, offsets relativos a 0x4a000000)
    0x035090  crt0 (ARM): chama main em 0x1eaad4 (Thumb)
    0x035144  PLT0; 0x035158..0x036380 stubs do PLT (381, ARM, `ldr pc,[ip,#..]!` na GOT em 0x4460bc)
    0x036380  código do jogo (Thumb-2, ~0x36380..0x270000)
    0x270000  bibliotecas em ARM (IwGx/IwGL/libc/libstdc++/zlib/png/jpeg/ogg/swf...) até ~0x3b3000
    0x3b4000  rodata (strings, nomes RTTI)
    0x40f12d  área sempre zerada (CAVE/CAVE2 do patch_s3e.py em 0x410800/0x410900)
    0x410f20  dados (vtables, typeinfo, tabelas); 0x4460b0 GOT
    0x452bac  fim do arquivo; bss (zerado) até 0x4dce58 — variáveis globais e ponteiros de singletons
"""
import lzma
import os
import struct

IMAGE = 0x43F3
RT_BASE = 0x4A000000
RT_DELTA = 0x35090
PTR_BIAS = RT_BASE - RT_DELTA          # ponteiro gravado = offset + PTR_BIAS

TEXT_START = 0x36380
THUMB_END = 0x270000                    # aproximado; o modo real de cada função vem das sementes
TEXT_END = 0x3B3800
RODATA_START = 0x3B3800
DATA_START = 0x410F20
GOT = 0x4460B0
BSS_END = 0x4DCE58                      # cabeçalho [7] = 0x4a7dc8 (relativo a 0x4a000000) + 0x35090

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_S3E = os.path.join(PORT, 'build', 'base.apk')


def rt(off):
    """offset na imagem -> endereço em execução"""
    return off + PTR_BIAS


def off(addr):
    """endereço em execução (ou ponteiro dos dados) -> offset na imagem"""
    return addr - PTR_BIAS


def load(path=None, cache=None):
    """Devolve os bytes da imagem (a partir de 0x43F3). Aceita o boz.s3e ou o base.apk."""
    path = path or DEFAULT_S3E
    if cache and os.path.exists(cache):
        return open(cache, 'rb').read()
    if path.endswith('.apk'):
        import zipfile
        raw = zipfile.ZipFile(path).read('assets/boz.s3e')
    else:
        raw = open(path, 'rb').read()
    img = lzma.decompress(raw, format=lzma.FORMAT_ALONE)[IMAGE:]
    if cache:
        open(cache, 'wb').write(img)
    return img


def import_names(img):
    n = struct.unpack_from('<H', img, 8)[0]
    p, out = 10, []
    for _ in range(n):
        e = img.index(b'\0', p)
        out.append(img[p:e].decode())
        p = e + 1
    return out


def import_slots(img):
    """{offset na imagem do ponteiro (GOT ou dados): nome da importação}. Tabela no fim do cabeçalho
    (0x34730: u32 nº=399, depois [u16 alto, u16 baixo, u16 índice do nome]; endereço relativo a 0x4a000000)."""
    names = import_names(img)
    n = struct.unpack_from('<I', img, 0x34730)[0]
    out = {}
    for i in range(n):
        hi, lo, idx = struct.unpack_from('<3H', img, 0x34734 + 6 * i)
        out[(hi << 16 | lo) + RT_DELTA] = names[idx]
    return out


def relocs(img):
    """offsets na imagem das palavras relocadas (ponteiros) — 52075 entradas a partir de 0x1974"""
    n = struct.unpack_from('<I', img, 0x1968)[0]
    return [x + RT_DELTA for x in struct.unpack_from('<%dI' % n, img, 0x196C)]


def _dpimm(w):
    r, v = (w >> 8) & 0xF, w & 0xFF
    return ((v >> (2 * r)) | (v << (32 - 2 * r))) & 0xFFFFFFFF


def plt_stubs(img):
    """[(offset do stub, offset da entrada na GOT)]"""
    out, a = [], 0x35158
    while a < TEXT_START:
        w0, w1, w2 = struct.unpack_from('<3I', img, a)
        if (w0 & 0xFFFFF000) == 0xE28FC000 and (w1 & 0xFFFFF000) == 0xE28CC000 and (w2 & 0xFFFFF000) == 0xE5BCF000:
            out.append((a, a + 8 + _dpimm(w0) + _dpimm(w1) + (w2 & 0xFFF)))
            a += 12
        else:
            a += 4
    return out


def cstr(img, o, maxlen=300):
    if not 0 <= o < len(img):
        return None
    e = img.find(b'\0', o, o + maxlen)
    if e <= o:
        return None
    s = img[o:e]
    if all(32 <= c < 127 or c in (9, 10, 13) for c in s):
        return s.decode()
    return None
