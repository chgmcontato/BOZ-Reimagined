#!/usr/bin/env python3
"""CIsEntitySpec (entidades dos mapas do BOZ): lê, mostra e regrava. Formato tirado de
CIsEntitySpec::Serialise (0xf1140) e CIsComponentSpec::Serialise (0xef6a0), ver docs/re/criador-de-mapas.md.

  spec      = u32 nº de componentes, item*, u32 nº de filhos, item*
  item      = u32 hash da classe (0 = nulo), objeto da classe
  CIsComponentSpec / CIsCollisionObjectSpec:
              u32 hash do nome (CIwManaged), u32 tipo do componente (hash: CIsTransform, CBarricade...),
              u32 tamanho, dados refletidos (reflres.py: 'LFER'...)
  CIsEntitySpec (filho): u32 hash do nome, depois um spec
  CIsCollisionMeshSpec (0x8787c): parte de componente (como acima), u32 tamanho + mundo Bullet ('BULLETf_v278',
              btBulletWorldImporter), malha (0xf9264): u32 nº vértices, u32 nº índices, u32 nº materiais,
              nomes de material (texto + 0), float[3*vértices], u32[índices]; depois u8 por triângulo (índices/3)

Uso: entityspec.py <blackops_etc.dz> [filtro de grupo]   -> resumo e teste de regravação
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import iwgroup  # noqa: E402

H = iwgroup.djb2
COMPONENT_LIKE = {H('CIsComponentSpec'), H('CIsCollisionObjectSpec')}
MESH = H('CIsCollisionMeshSpec')
ENTITY = H('CIsEntitySpec')


class Opaque(Exception):
    pass


def read_spec(d, p):
    spec = {'components': [], 'children': []}
    for key in ('components', 'children'):
        n, = struct.unpack_from('<I', d, p)
        p += 4
        for _ in range(n):
            it, p = read_item(d, p)
            spec[key].append(it)
    return spec, p


def read_item(d, p):
    cls, = struct.unpack_from('<I', d, p)
    p += 4
    if cls == 0:
        return {'cls': 0}, p
    if cls in COMPONENT_LIKE:
        name, typ, ln = struct.unpack_from('<3I', d, p)
        p += 12
        return {'cls': cls, 'name': name, 'type': typ, 'data': d[p:p + ln]}, p + ln
    if cls == MESH:
        name, typ, ln = struct.unpack_from('<3I', d, p)
        p += 12
        it = {'cls': cls, 'name': name, 'type': typ, 'data': d[p:p + ln]}
        p += ln
        bl, = struct.unpack_from('<I', d, p)
        it['bullet'] = d[p + 4:p + 4 + bl]
        p += 4 + bl
        nv, ni, nm = struct.unpack_from('<3I', d, p)
        p += 12
        it['materials'] = []
        for _ in range(nm):                       # IwSerialiseString (até 160 bytes): texto + 0
            e = d.index(b'\0', p)
            it['materials'].append(d[p:e].decode('latin1'))
            p = e + 1
        it['verts'] = d[p:p + 12 * nv]
        p += 12 * nv
        it['indices'] = d[p:p + 4 * ni]
        p += 4 * ni
        it['triflags'] = d[p:p + ni // 3]
        return it, p + ni // 3
    if cls == ENTITY:
        name, = struct.unpack_from('<I', d, p)
        sub, p = read_spec(d, p + 4)
        return {'cls': cls, 'name': name, 'spec': sub}, p
    raise Opaque(iwgroup.name(cls))


def write_spec(spec):
    out = b''
    for key in ('components', 'children'):
        out += struct.pack('<I', len(spec[key]))
        for it in spec[key]:
            out += write_item(it)
    return out


def write_item(it):
    if it['cls'] == 0:
        return b'\0\0\0\0'
    if it['cls'] in COMPONENT_LIKE:
        return struct.pack('<4I', it['cls'], it['name'], it['type'], len(it['data'])) + it['data']
    if it['cls'] == MESH:
        return (struct.pack('<4I', it['cls'], it['name'], it['type'], len(it['data'])) + it['data'] +
                struct.pack('<I', len(it['bullet'])) + it['bullet'] +
                struct.pack('<3I', len(it['verts']) // 12, len(it['indices']) // 4, len(it['materials'])) +
                b''.join(m.encode('latin1') + b'\0' for m in it['materials']) + it['verts'] + it['indices'] + it['triflags'])
    return struct.pack('<2I', it['cls'], it['name']) + write_spec(it['spec'])


def parse(data):
    spec, p = read_spec(data, 0)
    return spec, data[p:]


def describe(spec, ind=1):
    for it in spec['components']:
        if it['cls'] == 0:
            continue
        print('  ' * ind + '%s (%s)' % (iwgroup.name(it['type']), iwgroup.name(it['name'])))
    for it in spec['children']:
        if it['cls']:
            print('  ' * ind + 'filho %s:' % iwgroup.name(it['name']))
            describe(it['spec'], ind + 1)


def main():
    import collections
    import dz
    ents, _ = dz.entries_of(sys.argv[1])
    flt = sys.argv[2] if len(sys.argv) > 2 else None
    ok = bad = opaque = 0
    why = collections.Counter()
    types = collections.Counter()
    for d, n, b, _ in ents:
        if not n.endswith('.group.bin'):
            continue
        try:
            g = iwgroup.Group.load(b)
        except Exception:
            continue
        for L in g.lists():
            for o in L.objects:
                if (o.get('cls') or L.cls) != ENTITY:
                    continue
                try:
                    spec, rest = parse(o['data'])
                except Opaque as e:
                    opaque += 1
                    why[str(e)] += 1
                    continue
                if write_spec(spec) + rest == o['data']:
                    ok += 1
                else:
                    bad += 1
                for it in spec['components']:
                    if it['cls']:
                        types[iwgroup.name(it['type'])] += 1
                if flt and flt.lower() in (d + n).lower():
                    print('%s/%s %s' % (d, n, iwgroup.name(o['name'])))
                    describe(spec)
    print('specs: %d regravadas idênticas, %d diferentes, %d com item opaco %s' % (ok, bad, opaque, dict(why)),
          file=sys.stderr)
    print('tipos de componente:', types.most_common(), file=sys.stderr)


if __name__ == '__main__':
    main()
