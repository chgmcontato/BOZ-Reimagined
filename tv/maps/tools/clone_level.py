#!/usr/bin/env python3
"""Clona um mapa do OBB com outro nome (mesmo tamanho), p.ex. kino -> moon, e grava um OBB novo.

Copia todo arquivo cujo caminho contém o nome antigo (levels/<n>/*, arms_<n>, weapons_<n>, loading/<n>...),
trocando o nome nos caminhos, no nome do grupo e nos subgrupos (com o hash djb2 do nome recalculado).
Os recursos ficam iguais, menos o nome do CLevel (o jogo cria a fase procurando um CLevel com o nome do mapa). Arquivos já existentes com o nome novo são substituídos.
Uso: clone_level.py <obb_entrada.dz> <obb_saida.dz> <antigo> <novo> [dir_com_substitutos] [caminho=arquivo ...]
  dir_com_substitutos: arquivos <caminho do clone> nesse diretório substituem o conteúdo clonado ("-" = nenhum).
  caminho=arquivo: troca o conteúdo de qualquer arquivo do OBB (caminho com /, p.ex. frontend/fe_v4_cs4_swf.bin)."""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dz  # noqa: E402
import iwgroup  # noqa: E402


def swap(text, old, new):
    """Troca old->new preservando maiúsculas (kino/Kino/KINO)."""
    def rep(m):
        s = m.group(0)
        if s.isupper():
            return new.upper()
        if s[0].isupper():
            return new[0].upper() + new[1:]
        return new
    return re.sub(re.escape(old), rep, text, flags=re.I)


def clone_group(data, old, new):
    g = iwgroup.Group.load(data)
    chunks = []
    for tag, body in g.chunks:
        if tag == iwgroup.RESOURCES:
            chunks.append((tag, body))
            continue
        if tag == iwgroup.CHILDREN:
            body = clone_children(body, old, new)
        elif not chunks:  # ResGroupMembers: nome\0 + flags
            name, rest = body.split(b'\0', 1)
            body = swap(name.decode('latin1'), old, new).encode('latin1') + b'\0' + rest
        chunks.append((tag, body))
    g.chunks = chunks
    return rename_level(g.dump(), old, new)


def rename_level(data, old, new):
    """Troca o hash do nome do recurso CLevel (fica 14 bytes depois do hash da classe)."""
    cls = struct.pack('<I', iwgroup.djb2('CLevel'))
    ho, hn = struct.pack('<I', iwgroup.djb2(old)), struct.pack('<I', iwgroup.djb2(new))
    out, i = bytearray(data), data.find(ho)
    while i >= 0:
        if data[i - 14:i - 10] == cls:
            out[i:i + 4] = hn
        i = data.find(ho, i + 1)
    return bytes(out)


def clone_loading_key(data, old, new):
    """fixed.group: a chave de config "loading-<mapa>" (CResolutionSpecificString com os grupos da tela de
    carregamento) é procurada pelo nome do mapa; sem "loading-<novo>" o jogo cai ao iniciar o mapa."""
    g = iwgroup.Group.load(data)
    ho, hn = iwgroup.djb2('loading-' + old), iwgroup.djb2('loading-' + new)
    for tag, body in g.chunks:
        if tag != iwgroup.RESOURCES:
            continue
        for lst in body:
            if any(o['name'] == hn for o in lst.objects):
                return data
            for o in list(lst.objects):
                if o['name'] == ho:
                    nd = swap(o['data'].decode('latin1'), old, new).encode('latin1')
                    assert len(nd) == len(o['data'])
                    lst.objects.append({'cls': o['cls'], 'name': hn, 'data': nd})
                    return g.dump()
    raise ValueError('loading-%s não encontrado no fixed.group' % old)


def clone_children(body, old, new):
    n = body[0]
    p, out = 1, bytearray([n])
    for _ in range(n):
        e = body.index(b'\0', p)
        path = body[p:e].decode('latin1')
        extra = body[e + 1:e + 9]
        h, = struct.unpack_from('<I', body, e + 9)
        # hash = djb2 do nome interno do subgrupo; só muda quando esse nome contém o nome antigo
        base = path.rsplit('/', 1)[-1].rsplit('.', 1)[0]
        npath = swap(path, old, new)
        if h == iwgroup.djb2(base) and old.lower() in base.lower():
            h = iwgroup.djb2(swap(base, old, new))
        out += npath.encode('latin1') + b'\0' + extra + struct.pack('<I', h)
        p = e + 13
    assert p == len(body), 'sobrou no ResGroupChildPaths'
    return bytes(out)


def main():
    src, dst, old, new = sys.argv[1:5]
    subst = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] != '-' else None
    replace = dict(a.split('=', 1) for a in sys.argv[6:])
    assert len(old) == len(new)
    entries, dirs = dz.entries_of(src)
    keep = []
    for dr, name, data, di in entries:
        if new.lower() in (dr + '\\' + name).lower():
            continue
        path = (dr.replace('\\', '/') + '/' + name).lstrip('/')
        if path == 'fixed/fixed.group.bin':
            data = clone_loading_key(data, old, new)
        if path in replace:
            data = open(replace.pop(path), 'rb').read()
            print('trocado:', path)
        keep.append((dr, name, data, di))
    assert not replace, 'caminhos não encontrados: %s' % list(replace)
    clones = []
    for dr, name, data, _ in entries:
        if old.lower() not in (dr + '\\' + name).lower():
            continue
        ndr, nname = swap(dr, old, new), swap(name, old, new)
        if data[:4] == b'\x3d\x03\x07\x01':
            data = clone_group(data, old, new)
        if subst:
            alt = os.path.join(subst, ndr.replace('\\', '/'), nname)
            if os.path.exists(alt):
                data = open(alt, 'rb').read()
                print('substituto:', alt)
        clones.append((ndr, nname, data))
    print('%d arquivos clonados (%.1f MB)' % (len(clones), sum(len(c[2]) for c in clones) / 1e6))
    dz.write(dst, keep + clones, dirs=dirs)


if __name__ == '__main__':
    main()
