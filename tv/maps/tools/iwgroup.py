#!/usr/bin/env python3
"""Lê e grava .group.bin (IwResManager) do COD:BOZ separando cada recurso em bytes opacos.

Estrutura (docs/formato-group-bin.md):
  u32 versão, u16 0, blocos [tag u32][tam u32, contando o próprio campo][conteúdo] até um u32 0:
  1º bloco = nome\\0 + u32 flags; depois ResGroupChildPaths, ResGroupResources, ...
  ResGroupResources: u32 nº listas; lista = u32 hash da classe, u32 nº objetos, u8 mesma_classe, u8 com_tamanho,
  objeto = [u32 tam (desde este campo)] [u32 classe se !mesma_classe] [u32 hash do nome] dados...
Uso: iwgroup.py <arquivo.group.bin>   (mostra a estrutura e confere a regravação byte a byte)"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
try:
    NAMES = {int(k): v for k, v in json.load(open(os.path.join(HERE, 'hashes.json'))).items()}
except OSError:
    NAMES = {}


def djb2(s):
    h = 5381
    for c in s.lower().encode():
        h = (h * 33 + c) & 0xffffffff
    return h


RESOURCES = djb2('ResGroupResources')
CHILDREN = djb2('ResGroupChildPaths')


class UnsupportedGroup(ValueError):
    """Formato válido em potencial, mas sem separação de objetos conhecida."""


def need(data, pos, size, context):
    if pos < 0 or size < 0 or pos + size > len(data):
        raise ValueError('%s truncado em 0x%x: precisa %d bytes, total %d' % (context, pos, size, len(data)))


def name(h):
    return NAMES.get(h, '%08x' % h)


class Group:
    def __init__(self):
        self.version = 0
        self.pad = 0
        self.chunks = []  # (tag, bytes) ou (RESOURCES, [listas]); o 1º é o nome do grupo (nome\0 + u32 flags)
        self.tail = b'\0' * 4  # terminador

    # ------------------------------------------------------------ leitura
    @classmethod
    def load(cls, data):
        g = cls()
        need(data, 0, 10, 'grupo')
        g.version, g.pad = struct.unpack_from('<IH', data, 0)
        p = 6
        while True:
            need(data, p, 4, 'tag de bloco')
            tag, = struct.unpack_from('<I', data, p)
            if tag == 0:  # terminador
                g.tail = data[p:]
                break
            need(data, p + 4, 4, 'tamanho do bloco')
            size, = struct.unpack_from('<I', data, p + 4)
            if size < 4:
                raise ValueError('tamanho de bloco inválido em 0x%x: %d' % (p, size))
            need(data, p, 4 + size, 'bloco')
            body = data[p + 8:p + 4 + size]
            g.chunks.append((tag, parse_resources(body) if tag == RESOURCES else body))
            p += 4 + size
        return g

    # ------------------------------------------------------------ escrita
    def dump(self):
        out = bytearray(struct.pack('<IH', self.version, self.pad))
        for tag, body in self.chunks:
            raw = write_resources(body) if tag == RESOURCES else body
            out += struct.pack('<II', tag, 4 + len(raw)) + raw
        out += self.tail
        return bytes(out)

    def lists(self):
        for tag, body in self.chunks:
            if tag == RESOURCES:
                return body
        return []


class ResList:
    def __init__(self, cls_hash, same_class, sized):
        self.cls = cls_hash
        self.same = same_class
        self.sized = sized
        self.objects = []  # dicts {cls, name, data}


def parse_resources(body):
    need(body, 0, 4, 'número de listas')
    n, = struct.unpack_from('<I', body, 0)
    if n > (len(body) - 4) // 10:
        raise ValueError('número de listas excede o bloco')
    p = 4
    out = []
    for _ in range(n):
        need(body, p, 10, 'lista de recursos')
        cls_hash, count, same, sized = struct.unpack_from('<IIBB', body, p)
        p += 10
        if same not in (0, 1) or sized not in (0, 1):
            raise ValueError('flags de lista inválidas')
        if not sized and count:
            raise UnsupportedGroup('lista sem tamanhos: classe %s' % name(cls_hash))
        if count > (len(body) - p) // (8 if same else 12):
            raise ValueError('número de objetos excede o bloco')
        lst = ResList(cls_hash, same, sized)
        for _ in range(count):
            start = p
            size = None
            if sized:
                need(body, p, 4, 'tamanho do recurso')
                size, = struct.unpack_from('<I', body, p); p += 4
                minimum = 8 if same else 12
                if size < minimum:
                    raise ValueError('recurso menor que o cabeçalho em 0x%x' % start)
                need(body, start, size, 'recurso')
            ocls = cls_hash
            if not same:
                ocls, = struct.unpack_from('<I', body, p); p += 4
            oname, = struct.unpack_from('<I', body, p); p += 4
            if size is None:
                raise ValueError('lista sem tamanhos: não dá para separar sem entender a classe %s' % name(cls_hash))
            end = start + size
            lst.objects.append({'cls': ocls, 'name': oname, 'data': body[p:end]})
            p = end
        out.append(lst)
    if p != len(body):
        raise ValueError('sobraram %d bytes em ResGroupResources' % (len(body) - p))
    return out


def write_resources(lists):
    out = bytearray(struct.pack('<I', len(lists)))
    for lst in lists:
        if lst.objects and not lst.sized:
            raise UnsupportedGroup('não é possível gravar lista sem tamanhos')
        out += struct.pack('<IIBB', lst.cls, len(lst.objects), lst.same, lst.sized)
        for o in lst.objects:
            if lst.same and o['cls'] != lst.cls:
                raise ValueError('classe do objeto difere da lista de classe única')
            rec = bytearray()
            if not lst.same:
                rec += struct.pack('<I', o['cls'])
            rec += struct.pack('<I', o['name']) + o['data']
            out += struct.pack('<I', 4 + len(rec)) + rec
    return bytes(out)


if __name__ == '__main__':
    raw = open(sys.argv[1], 'rb').read()
    g = Group.load(raw)
    print('grupo %r versão %#x' % (g.chunks[0][1].split(b'\0')[0].decode('latin1'), g.version))
    for tag, body in g.chunks:
        if tag == RESOURCES:
            for lst in body:
                print('  lista %-22s %4d objetos (mesma classe=%d, tamanhos=%d)' % (
                    name(lst.cls), len(lst.objects), lst.same, lst.sized))
        else:
            print('  bloco %s (%d bytes)' % (name(tag), len(body)))
    print('regravação idêntica:', g.dump() == raw)
