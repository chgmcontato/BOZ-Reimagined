#!/usr/bin/env python3
"""Lê a árvore de um CIwUIElement serializado (layout do HUD) do COD:BOZ.

Elemento (CIwUIElement::Serialise 0x3368e4): lista[+0x1c], pos (2×i32), tamanho (2×i32),
  propriedades (hash u32, u32, ref u32, u16 nº, [nome u32, tipo u32, valor]), ref[+0x8c], ref[+0x58] (layout),
  lista[+0x34] (filhos).
Lista: u32 nº, cada item = hash da classe + objeto. Ref: hash da classe (0 = nada) + objeto.
Uso: iwui.py <ingame.group.bin> <nome do elemento>"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iwgroup  # noqa: E402

H = iwgroup.djb2
for n in ('bool', 'CIwVec2', 'CIwSVec2', 'CIwColour', 'iwfixed', 'iwangle', 'int16', 'int32', 'uint32', 'int8',
          'uint8', 'uint16', 'float', 'IwUIAlignmentH', 'IwUIAlignmentV', 'CIwPropertyString', 'CIwTexture',
          'CIwMaterial', 'CIwGxFont', 'CIwUIStyle', 'CIwUIRect', 'CIwFVec2', 'CIwColour', 'texture', 'visible',
          'left', 'right', 'top', 'bottom', 'RIGHT', 'LEFT', 'TOP', 'BOTTOM', 'CENTRE', 'centre'):
    iwgroup.NAMES.setdefault(H(n), n)

# tamanho do valor por tipo (bytes); None = string (u32 tam + bytes) — completar conforme aparecerem
SIZES = {H('bool'): 1, H('CIwVec2'): 8, H('CIwSVec2'): 4, H('CIwColour'): 4, H('iwfixed'): 4, H('iwangle'): 4,
         H('int32'): 4, H('uint32'): 4, H('int16'): 2, H('uint16'): 2, H('int8'): 1, H('uint8'): 1,
         H('float'): 4, H('CIwFVec2'): 8, 0xd9609c83: 4}


class R:
    def __init__(self, b, p=0):
        self.b, self.p = b, p

    def u8(self):
        self.p += 1; return self.b[self.p - 1]

    def u16(self):
        self.p += 2; return struct.unpack_from('<H', self.b, self.p - 2)[0]

    def u32(self):
        self.p += 4; return struct.unpack_from('<I', self.b, self.p - 4)[0]

    def i32(self):
        self.p += 4; return struct.unpack_from('<i', self.b, self.p - 4)[0]


unknown = {}


def plausible(b, p):
    """depois de um valor vem outra propriedade (nome, tipo conhecido) ou o fim da lista de props"""
    if p + 8 > len(b):
        return False
    t = struct.unpack_from('<I', b, p + 4)[0]
    return t in SIZES or t in GUESSED


GUESSED = {}


def is_string(b, p):
    """valor texto: u32 tamanho (com o \\0) + caracteres imprimíveis + \\0"""
    if p + 4 > len(b):
        return False
    ln = struct.unpack_from('<I', b, p)[0]
    if not 1 <= ln <= 128 or p + 4 + ln > len(b):
        return False
    t = b[p + 4:p + 4 + ln]
    return t[-1] == 0 and all(32 <= c < 127 for c in t[:-1])


def read_value(r, typ):
    n = SIZES.get(typ, GUESSED.get(typ))
    if n == 'str' or (n is None and is_string(r.b, r.p)):
        GUESSED[typ] = 'str'
        ln = struct.unpack_from('<I', r.b, r.p)[0]
        v = r.b[r.p:r.p + 4 + ln]; r.p += 4 + ln
        return v
    if n is None:
        for cand in (4, 1, 8, 2, 12, 16, 3, 5, 6):
            if plausible(r.b, r.p + cand):
                n = cand
                break
        if n is None:
            n = 4  # sem pista: referência/hash/inteiro
        GUESSED[typ] = n
        print('  (tipo %s deduzido: %d bytes)' % (iwgroup.name(typ), n), file=sys.stderr)
    v = r.b[r.p:r.p + n]; r.p += n
    return v


def read_props(r):
    h = r.u32(); x1 = r.u32(); ref = r.u32()
    n = r.u16()
    props = []
    for _ in range(n):
        at = r.p
        name, typ = r.u32(), r.u32()
        val = read_value(r, typ)
        props.append((at, name, typ, val))
    return {'hash': h, 'x1': x1, 'ref': ref, 'props': props}


TAILS = {H('CIwUIImage'): 5}


def is_class(b, p):
    if p + 4 > len(b):
        return True  # fim dos dados
    v = struct.unpack_from('<I', b, p)[0]
    n = iwgroup.NAMES.get(v, '')
    return n.startswith('C') or v in TAILS


def read_list(r, depth=0):
    n = r.u32()
    out = []
    for k in range(n):
        out.append(read_object(r, r.u32()))
        # entre itens (ou no fim da lista do pai) o próximo deve ser um hash de classe
    return out


def read_ref(r):
    c = r.u32()
    return read_object(r, c) if c else None


def read_object(r, cls):
    """objeto gerenciado embutido: elementos, layouts... (as classes desconhecidas param o parser)"""
    name = r.u32()  # CIwManaged::Serialise (hash do nome)
    return read_element_body(r, cls, name)


def read_element_body(r, cls, name):
    start = r.p
    e = {'cls': cls, 'name': name, 'start': start}
    e['list1'] = read_list(r)
    e['pos_at'] = r.p
    e['pos'] = (r.i32(), r.i32())
    e['size'] = (r.i32(), r.i32())
    e['props'] = read_props(r)
    e['ref8c'] = read_ref(r)
    e['layout'] = read_ref(r)
    e['children'] = read_list(r)
    tail = TAILS.get(cls)
    if tail is None:
        # deduz quantos bytes a subclasse grava depois da parte comum
        for cand in (0, 1, 4, 5, 8, 2, 12, 9, 16, 3, 6, 7, 10, 20, 24):
            if is_class(r.b, r.p + cand):
                tail = cand
                break
        TAILS[cls] = tail
        print('  (classe %s: %s bytes extras)' % (iwgroup.name(cls), tail), file=sys.stderr)
    e['tail'] = r.b[r.p:r.p + tail]
    r.p += tail
    e['end'] = r.p
    return e


def dump(e, depth=0):
    nm = iwgroup.name(e['name'])
    props = ' '.join('%s:%s=%s' % (iwgroup.name(n), iwgroup.name(t), v.hex()) for _, n, t, v in e['props']['props'])
    print('%s%s [%s] pos=%s size=%s %s' % ('  ' * depth, nm, iwgroup.name(e['cls']), e['pos'], e['size'], props))
    for c in e['list1']:
        dump(c, depth + 2)
    for c in e['children']:
        dump(c, depth + 1)


if __name__ == '__main__':
    g = iwgroup.Group.load(open(sys.argv[1], 'rb').read())
    for l in g.lists():
        for o in l.objects:
            if iwgroup.name(o['name']) == sys.argv[2]:
                r = R(o['data'])
                e = read_element_body(r, o['cls'], o['name'])
                dump(e)
                print('consumido %d de %d' % (r.p, len(o['data'])))
