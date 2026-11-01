#!/usr/bin/env python3
"""Troca os prompts de interação por "Press [A] to <ação>" nos textos em inglês (localisation/english_*.dat)
de um .dz (blackops_loader.dz do APK, que é o que o jogo usa, ou o OBB), sem mudar o tamanho dos arquivos
(a troca é feita no próprio .dz).

Formato LOC0: "LOC0", u32 (?), u32 tamanho do bloco de textos, u32 nº de textos, u32 deslocamento de cada
texto (relativo ao bloco), "utf-8\\0", bloco de textos terminados em \\0.
Os prompts crescem; a diferença sai do texto de ajuda dos CoD Points (a loja foi removida do port).
Rodar depois do patch_text.py (aceita "Tap to X" e "[A] X").
Uso: loc_patch.py <arquivo.dz>"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dz  # noqa: E402

FILES = ('english_uk.dat', 'english_us.dat')   # em localisation/ (APK: blackops_loader.dz; OBB)
PROMPT = re.compile(rb'^\s*(?:\[A\]|Tap to|TAP TO|tap to) (.*?)\s*$', re.S)
# textos fixos: dica do toque duplo (o patch_text antigo deixou "Double  [A] swap your weapon.") -> botão Y
FIXED = {b'Double  [A] swap your weapon.  ': b'Press [Y] to swap your weapon.',
         b'Double tap to swap your weapon.': b'Press [Y] to swap your weapon.'}
COD_HELP = b'CoD Points\n\n'
COD_SHORT = b'CoD Points\n\n\xe2\x80\xa2 CoD Points are not used in this version.'


def prompt(s):
    if s in FIXED:
        return FIXED[s]
    m = PROMPT.match(s)
    if not m or b'Press [A]' in s or b'PRESS [A]' in s:
        return s
    act = m.group(1)
    caps = re.sub(rb'%\w', b'', act)   # "%d" não conta como minúscula
    return (b'PRESS [A] TO ' if caps.upper() == caps else b'Press [A] to ') + act


def patch(d):
    magic, x, size, n = struct.unpack_from('<4sIII', d, 0)
    assert magic == b'LOC0'
    offs = struct.unpack_from('<%dI' % n, d, 16)
    base = 16 + 4 * n
    enc = d[base:d.index(b'\0', base) + 1]
    blob = d[base + len(enc):base + len(enc) + size]
    strs = [blob[o:blob.index(b'\0', o)] for o in offs]
    new = [prompt(s) for s in strs]
    changed = sum(a != b for a, b in zip(strs, new))
    cod = [k for k, s in enumerate(new) if s.startswith(COD_HELP)]
    assert len(cod) == 1, cod
    grow = sum(len(s) for s in new) - sum(len(s) for s in strs)
    k = cod[0]
    room = len(new[k]) - len(COD_SHORT) - grow
    if room < 0:
        raise SystemExit('sem espaço: faltam %d bytes' % -room)
    new[k] = COD_SHORT + b' ' * room          # completa até o tamanho original do bloco
    out_blob, out_offs = bytearray(), []
    for s in new:
        out_offs.append(len(out_blob))
        out_blob += s + b'\0'
    assert len(out_blob) == size, (len(out_blob), size)
    out = d[:16] + struct.pack('<%dI' % n, *out_offs) + enc + bytes(out_blob) + d[base + len(enc) + size:]
    assert len(out) == len(d)
    return out, changed


def main():
    path = sys.argv[1]
    d, files, chunks, arch, _ = dz.read(path)
    todo = []
    for name, dr, ch, di in files:
        if dr.strip('\\/').lower() == 'localisation' and name.lower() in FILES:
            assert len(ch) == 1 and chunks[ch[0]][3] == 0x100, 'arquivo comprimido'
            off, size = chunks[ch[0]][0], chunks[ch[0]][1]
            data, changed = patch(d[off:off + size])
            todo.append((off, data))
            print('%s: %s, %d prompt(s) trocado(s)' % (path, name, changed))
    del d
    if not todo:
        raise SystemExit('nenhum english_*.dat encontrado')
    with open(path, 'r+b') as f:
        for off, data in todo:
            f.seek(off)
            f.write(data)

if __name__ == '__main__':
    main()
