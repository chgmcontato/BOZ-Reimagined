#!/usr/bin/env python3
"""Lista/extrai arquivos de um pacote DTRZ (.dz do Marmalade).
Uso: dz.py <arquivo.dz> [saida_dir] [filtro]"""
import struct, sys, os, zlib, lzma, bz2
import tempfile

FLAGS = {1: 'combuf', 4: 'dz', 8: 'zlib', 0x10: 'bzip', 0x20: 'mp3', 0x40: 'jpeg', 0x80: 'zero',
         0x100: 'copy', 0x200: 'lzma', 0x400: 'random'}


def cstr(d, p):
    e = d.index(b'\0', p)
    return d[p:e].decode('latin1'), e + 1


def read(path):
    with open(path, 'rb') as source:
        d = source.read()
    if len(d) < 9 or d[:4] != b'DTRZ':
        raise ValueError('cabeçalho DTRZ inválido/truncado')
    nfiles, ndirs, ver = struct.unpack_from('<HHB', d, 4)
    if ndirs == 0:
        raise ValueError('DTRZ sem diretório raiz')
    p = 9
    names, dirs = [], []
    for _ in range(nfiles):
        s, p = cstr(d, p); names.append(s)
    dirs.append('')  # diretório 0 = raiz, implícito
    for _ in range(ndirs - 1):
        s, p = cstr(d, p); dirs.append(s)
    files = []
    for i in range(nfiles):
        # diretório, depois os índices dos blocos terminados por 0xFFFF
        di, = struct.unpack_from('<H', d, p); p += 2
        ch = []
        while True:
            c, = struct.unpack_from('<H', d, p); p += 2
            if c == 0xFFFF:
                break
            ch.append(c)
        if di >= len(dirs):
            raise ValueError('índice de diretório inválido: %d' % di)
        files.append((names[i], dirs[di], ch, di))
    narch, nchunks = struct.unpack_from('<HH', d, p); p += 4
    chunks = []
    for _ in range(nchunks):
        chunks.append(struct.unpack_from('<IIIHH', d, p)); p += 16
    arch = [path]
    for _ in range(narch - 1):
        s, p = cstr(d, p); arch.append(s)
    if narch == 0:
        raise ValueError('DTRZ sem arquivo de dados')
    for _, _, indices, _ in files:
        if any(c >= nchunks for c in indices):
            raise ValueError('índice de bloco fora da tabela')
    for off, clen, dlen, flags, fi in chunks:
        if fi >= narch:
            raise ValueError('índice de pacote fora da tabela')
        if fi == 0 and (off < p or off + clen > len(d)):
            raise ValueError('bloco fora da área de dados: 0x%x + %d' % (off, clen))
    read.dirs = dirs
    return d, files, chunks, arch, p


def decomp(raw, flags, size):
    if flags & 0x200:
        # LZMA "alone" sem tamanho -> props(5) + dados
        dec = lzma.LZMADecompressor(lzma.FORMAT_ALONE)
        hdr = raw[:5] + struct.pack('<Q', size) + raw[5:] if len(raw) >= 5 else raw
        try:
            return dec.decompress(hdr)[:size]
        except lzma.LZMAError:
            return lzma.LZMADecompressor(lzma.FORMAT_ALONE).decompress(raw)[:size]
    if flags & 0x8:
        return zlib.decompress(raw)
    if flags & 0x10:
        return bz2.decompress(raw)
    if flags & 0x80:
        return b'\0' * size
    return raw


if __name__ == '__main__':
    d, files, chunks, arch, end = read(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else None
    filt = sys.argv[3] if len(sys.argv) > 3 else ''
    print('%d arquivos, %d blocos, pacotes %s' % (len(files), len(chunks), arch))
    for name, dr, ch, _ in files:
        full = (dr + '/' + name).replace('\\', '/').lstrip('/')
        if filt not in full:
            continue
        size = sum(chunks[c][2] for c in ch)
        fl = '|'.join(v for k, v in FLAGS.items() if any(chunks[c][3] & k for c in ch))
        print('%10d %-8s %s' % (size, fl, full))
        if out:
            data = b''
            for c in ch:
                off, clen, dlen, flags, fi = chunks[c]
                src = d if fi == 0 else open(os.path.join(os.path.dirname(sys.argv[1]), arch[fi]), 'rb').read()
                data += decomp(src[off:off + clen], flags, dlen)
            os.makedirs(os.path.join(out, os.path.dirname(full)), exist_ok=True)
            open(os.path.join(out, full), 'wb').write(data)


def write(path, entries, version=0, dirs=None):
    """Grava um DTRZ sem compressão. entries = [(diretório com '\\\\', nome, bytes)] na ordem desejada;
    dirs = lista de diretórios a manter (o original repete nomes), novos são acrescentados."""
    dirs = list(dirs) if dirs else ['']
    entries = list(entries)
    if not dirs or dirs[0] != '':
        raise ValueError('diretório zero deve ser a raiz')
    for dr, *_ in entries:
        if dr not in dirs:
            dirs.append(dr)
    dir_of = [e[3] if len(e) > 3 else dirs.index(e[0]) for e in entries]
    if len(entries) > 65535 or len(dirs) > 65535:
        raise ValueError('limite u16 de arquivos/diretórios excedido')
    for entry, di in zip(entries, dir_of):
        if not 0 <= di < len(dirs) or dirs[di] != entry[0]:
            raise ValueError('índice de diretório inconsistente')
        if '\0' in entry[1]:
            raise ValueError('nome de arquivo contém NUL')
    if any('\0' in dr for dr in dirs):
        raise ValueError('diretório contém NUL')
    head = bytearray(b'DTRZ' + struct.pack('<HHB', len(entries), len(dirs), version))
    for _, name, *_ in entries:
        head += name.encode('latin1') + b'\0'
    for dr in dirs[1:]:
        head += dr.encode('latin1') + b'\0'
    for i in range(len(entries)):
        head += struct.pack('<HHH', dir_of[i], i, 0xFFFF)
    head += struct.pack('<HH', 1, len(entries))
    table_at = len(head)
    head += b'\0' * 16 * len(entries)
    off = len(head)
    if off + sum(len(e[2]) for e in entries) > 0xffffffff:
        raise ValueError('pacote excede offsets u32')
    # Publica somente o arquivo inteiro; uma exceção nunca trunca o OBB de entrada.
    target = os.path.abspath(path)
    fd, tmp = tempfile.mkstemp(prefix='.' + os.path.basename(target) + '-', dir=os.path.dirname(target))
    try:
        with os.fdopen(fd, 'wb') as f:
            if os.path.exists(target):
                os.fchmod(f.fileno(), os.stat(target).st_mode & 0o777)
            f.write(head)
            for i, (_, _, data, *_) in enumerate(entries):
                struct.pack_into('<IIIHH', head, table_at + 16 * i, off, len(data), len(data), 0x100, 0)
                f.write(data)
                off += len(data)
            f.seek(0)
            f.write(head)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, target)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def entries_of(path):
    """Lê um DTRZ como lista de (diretório, nome, bytes), na ordem original (só pacote único, sem compressão)."""
    d, files, chunks, arch, _ = read(path)
    if len(arch) != 1 or any(c[3] != 0x100 or c[4] != 0 or c[1] != c[2] for c in chunks):
        raise ValueError('entries_of só aceita pacote único com blocos copy (0x100); use portkit extract para descompressão')
    out = []
    for name, dr, ch, di in files:
        out.append((dr, name, b''.join(d[chunks[c][0]:chunks[c][0] + chunks[c][1]] for c in ch), di))
    return out, read.dirs
