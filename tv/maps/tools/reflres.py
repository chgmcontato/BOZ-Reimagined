#!/usr/bin/env python3
"""Recursos refletidos do BOZ (CWave, CLevel, CArea, CZombieConfiguration... tudo que usa
CIsReflectedResource::Serialise, 0xf4020): lê, mostra com nomes e regrava.

Formato dos dados do objeto (depois do cabeçalho do iwgroup):
  u32 tamanho (resto)  'LFER'  u32 hash da classe  u32 tamanho  u32 nº de campos
  campo = u32 hash da classe dona, u32 hash do tipo, u32 a, u32 hash do nome, u32 b, u32 tamanho, valor[tamanho]
  (a e b valem 1 nos campos simples; o significado exato fica em aberto — são preservados)
O valor é interpretado pelo tipo (unsigned int, float, bool...) só para exibição/edição; o original é mantido.

Uso: reflres.py <blackops_etc.dz> [filtro de classe]   -> imprime os objetos e confere a regravação
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import iwgroup  # noqa: E402

TAG = b'LFER'
TYPES = {}
for t in ('unsigned int', 'int', 'float', 'bool', 'char', 'unsigned char', 'short', 'CIwFVec3', 'CIwFVec2',
          'CIwFQuat', 'CIwColour', 'CIsTimer'):
    TYPES[iwgroup.djb2(t)] = t
_ref = os.path.join(HERE, '..', '..', 'build', 're', 'out', 'reflect.json')
FIELDS = {}
if os.path.exists(_ref):
    for cls, c in json.load(open(_ref))['classes'].items():
        for f in c['fields']:
            FIELDS[iwgroup.djb2(f['name'])] = f['name']
            TYPES.setdefault(iwgroup.djb2(f['type']), f['type'])


def is_reflected(data):
    return len(data) >= 20 and data[4:8] == TAG


def parse(data):
    iwgroup.need(data, 0, 20, 'recurso refletido')
    total, = struct.unpack_from('<I', data, 0)
    if data[4:8] != TAG or total != len(data) - 4:
        raise ValueError('cabeçalho/tamanho LFER inválido')
    cls, size, n = struct.unpack_from('<3I', data, 8)
    if n > (len(data) - 20) // 24:
        raise ValueError('contagem de campos excede os bytes LFER')
    p = 20
    fields = []
    for _ in range(n):
        iwgroup.need(data, p, 24, 'cabeçalho de campo LFER')
        owner, typ, a, name, b, ln = struct.unpack_from('<6I', data, p)
        p += 24
        iwgroup.need(data, p, ln, 'valor de campo LFER')
        fields.append({'owner': owner, 'type': typ, 'a': a, 'name': name, 'b': b, 'value': data[p:p + ln]})
        p += ln
    return {'total': total, 'cls': cls, 'size': size, 'fields': fields, 'rest': data[p:]}


def build(obj):
    body = b''
    for f in obj['fields']:
        body += struct.pack('<6I', f['owner'], f['type'], f['a'], f['name'], f['b'], len(f['value'])) + f['value']
    head = TAG + struct.pack('<3I', obj['cls'], obj['size'], len(obj['fields']))
    out = head + body + obj['rest']
    return struct.pack('<I', len(out)) + out


def show_value(f):
    t = TYPES.get(f['type'], '%08x' % f['type'])
    v = f['value']
    try:
        if t in ('unsigned int',) and len(v) == 4:
            x = struct.unpack('<I', v)[0]
            return iwgroup.NAMES.get(x, x) if x > 0xffff else x
        if t == 'int' and len(v) == 4:
            return struct.unpack('<i', v)[0]
        if t == 'float' and len(v) == 4:
            return round(struct.unpack('<f', v)[0], 4)
        if t == 'bool' and len(v) == 1:
            return bool(v[0])
        if t in ('CIwFVec3', 'CIwFVec2', 'CIwFQuat') and len(v) % 4 == 0:
            return [round(x, 3) for x in struct.unpack('<%df' % (len(v) // 4), v)]
        if len(v) >= 4:
            k = struct.unpack_from('<I', v)[0]
            if len(v) == 4 + 4 * k and k:
                xs = struct.unpack_from('<%dI' % k, v, 4)
                return [iwgroup.NAMES.get(x, x) for x in xs]
            parts = v[4:].split(b'\0')
            if k and len(parts) == k + 1 and all(p.isascii() for p in parts):
                return [p.decode() for p in parts[:-1]]
    except struct.error:
        pass
    return v.hex()


def field_name(h):
    return FIELDS.get(h) or iwgroup.NAMES.get(h) or '%08x' % h


def main():
    sys.path.insert(0, HERE)
    import dz
    ents, _ = dz.entries_of(sys.argv[1])
    flt = sys.argv[2] if len(sys.argv) > 2 else None
    ok = bad = 0
    for d, n, b, _ in ents:
        if not n.endswith('.group.bin'):
            continue
        try:
            g = iwgroup.Group.load(b)
        except Exception:
            continue
        for L in g.lists():
            for o in L.objects:
                if not is_reflected(o['data']):
                    continue
                obj = parse(o['data'])
                if build(obj) == o['data']:
                    ok += 1
                else:
                    bad += 1
                cname = iwgroup.name(obj['cls'])
                if flt and flt.lower() not in cname.lower():
                    continue
                print('%s/%s  %s %s' % (d, n, cname, iwgroup.name(o['name'])))
                for f in obj['fields']:
                    print('    %-32s %-14s %s' % (field_name(f['name']), TYPES.get(f['type'], '%08x' % f['type'])[:14],
                                                   show_value(f)))
    print('objetos refletidos: %d regravados idênticos, %d diferentes' % (ok, bad), file=sys.stderr)


if __name__ == '__main__':
    main()
