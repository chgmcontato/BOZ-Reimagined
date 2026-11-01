#!/usr/bin/env python3
"""Ajustes no layout do HUD (ingame/ingame.group.bin) dentro do OBB, sem mudar tamanhos.

Cabeçalho de um elemento IwUI no grupo: hash da classe, hash do nome, u32 nº da 1ª lista, 2×(2×i32),
depois as propriedades (hash, u32, ref, u16 nº, [nome u32, tipo u32, valor]). Os filhos de uma página ficam
embutidos logo depois do cabeçalho dela.
Uso: hud_patch.py <obb.dz>"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dz  # noqa: E402
import iwgroup  # noqa: E402

# página de interação sem Xperia (ig_poiReg): a "mão" de toque é a CIwUIImage "Image", recorte (630,111)-(773,243)
# do atlas HUD_ALL (1024x512). Mover a imagem não adianta (o CIwUILayoutForm da página a reposiciona); em vez
# disso o recorte (props uv0/uv1, CIwSVec2) passa a um quadrado totalmente transparente do atlas. O texto
# "[A] ..." (CIwUILabel da mesma página) fica.
HIDE = {'ig_poiReg': ('Image',)}
UV0, UV1 = 0x0B88B400, 0x0B88B401           # nomes (hash) das props do recorte; tipo CIwSVec2 (2×i16)
CLEAR_UV = ((842, 272), (858, 288))          # 16×16 com alfa 0 no atlas HUD_ALL
RINGS = {((485, 779), (702, 996)): ((144, 0), (160, 16)),    # aro: fundo (hud_icons, 16×16 com alfa 0)
         ((23, 14), (243, 234)): CLEAR_UV,                  # aro: contorno (HUD_ALL)
         ((345, 0), (484, 46)): CLEAR_UV}                   # faca (HUD_ALL): piscava no começo da partida
CLASSES = {h for h, n in iwgroup.NAMES.items() if n.startswith(('CIwUI', 'CUI', 'CTouch', 'CGuiTouch'))}
SVEC2 = iwgroup.djb2('CIwSVec2')


def h(n):
    return n if isinstance(n, int) else iwgroup.djb2(n)


def _patch_layout(data):
    """Varredura limitada a UM recurso de UI; nunca interpreta pixels como propriedades."""
    data = bytearray(data)
    changes = []
    def replace(q, value):
        before = bytes(data[q:q + 4])
        after = struct.pack('<hh', *value)
        if before != after:
            data[q:q + 4] = after
            changes.append(q)

    for page, kids in HIDE.items():
        hp = struct.pack('<I', h(page))
        i = data.find(hp, 4)
        while i >= 0:
            if i + 8 <= len(data) and struct.unpack_from('<I', data, i - 4)[0] in CLASSES and struct.unpack_from('<I', data, i + 4)[0]:
                want = {h(k) for k in kids}
                for p in range(i + 4, min(i + 0x400, len(data) - 7)):
                    c, nm = struct.unpack_from('<II', data, p)
                    if c in CLASSES and nm in want:
                        for q in range(p + 8, min(p + 0x80, len(data) - 11)):
                            prop, typ = struct.unpack_from('<II', data, q)
                            if typ == SVEC2 and prop in (UV0, UV1):
                                replace(q + 8, CLEAR_UV[prop - UV0])
                        want.discard(nm)
                    if not want:
                        break
            i = data.find(hp, i + 1)
    signature = struct.pack('<II', UV0, SVEC2)
    q = data.find(signature)
    while q >= 0:
        if q + 12 > len(data):
            break
        v0 = struct.unpack_from('<hh', data, q + 8)
        for k in range(q + 12, min(q + 40, len(data) - 11)):
            p2, t2 = struct.unpack_from('<II', data, k)
            if t2 == SVEC2 and p2 == UV1:
                new = RINGS.get((v0, struct.unpack_from('<hh', data, k + 8)))
                if new:
                    replace(q + 8, new[0])
                    replace(k + 8, new[1])
                break
        q = data.find(signature, q + 1)
    return bytes(data), changes


def patch_report(data):
    group = iwgroup.Group.load(data)
    if group.dump() != data:
        raise ValueError('HUD não regrava idêntico antes do patch')
    edits = []
    for li, lst in enumerate(group.lists()):
        for oi, obj in enumerate(lst.objects):
            if obj['cls'] not in CLASSES:
                continue
            envelope = struct.pack('<II', obj['cls'], obj['name']) + obj['data']
            result, offsets = _patch_layout(envelope)
            if offsets:
                if len(result) != len(envelope) or result[:8] != envelope[:8]:
                    raise ValueError('patch alterou cabeçalho/tamanho do recurso')
                obj['data'] = result[8:]
                edits.append({'list': li, 'object': oi, 'name': iwgroup.name(obj['name']),
                              'offsets': [q - 8 for q in offsets]})
    result = group.dump()
    if len(result) != len(data) or iwgroup.Group.load(result).dump() != result:
        raise ValueError('HUD inválido após o patch')
    return result, edits


def patch(data):
    result, edits = patch_report(data)
    return result, sum(len(e['offsets']) for e in edits)


def main():
    src = sys.argv[1]
    ents, dirs = dz.entries_of(src)
    out, total = [], 0
    for dr, name, data, di in ents:
        if name.lower() == 'ingame.group.bin':
            data, total = patch(data)
        out.append((dr, name, data, di))
    print('coordenadas UV alteradas: %d' % total)
    dz.write(src, out, dirs=dirs)


if __name__ == '__main__':
    main()
