#!/usr/bin/env python3
"""Troca textos de toque por textos de controle nos arquivos .dz (DTRZ) do jogo.
Os textos ficam sem compressão; a troca mantém o mesmo tamanho em bytes (completa com espaços).
Uso: patch_text.py <arquivo.dz> [...]"""
import re
import sys

REPLACEMENTS = [
    ("Touch Screen to Start", "Press A to Start"),
    ("touch screen to start", "press a to start"),
    # arquivos já trocados quando o controle era o de SNES (botão B)
    ("Press B to Start", "Press A to Start"),
    ("press b to start", "press a to start"),
    # dica do toque duplo: trocar de arma é o Y no controle
    ("Double tap to swap your weapon.", "Press [Y] to swap your weapon."),
    ("Double  [A] swap your weapon.  ", "Press [Y] to swap your weapon."),
]


def same_size(old, new):
    pad = len(old) - len(new)
    assert pad >= 0, (old, new)
    left = pad // 2
    return (" " * left + new + " " * (pad - left)).encode()


# prompts de interação "Tap to <ação>" (e TAP TO / tap to): vira "[A] to <ação>" (o botão de usar no controle
# é o A), com o mesmo tamanho; o jogo não aceita textos maiores no arquivo de idioma (tentativa com LOC0
# regravado não teve efeito). Também converte o "[A] <ação>" centralizado das versões anteriores.
TAP = re.compile(rb'(?:Tap|TAP|tap) (to|TO) ([^\0]*)\0')
OLD = re.compile(rb'(?<=\0) *\[A\] ((?:(?!to |TO )[^\0])*?) *\0')


def tap_prompt(m):
    return b'[A] ' + m.group(1) + b' ' + m.group(2) + b'\0'


def old_prompt(m):
    act = m.group(1)
    to = b'TO' if re.sub(rb'%\w', b'', act).upper() == re.sub(rb'%\w', b'', act) else b'to'
    new = b'[A] ' + to + b' ' + act
    return new + b' ' * (len(m.group(0)) - 1 - len(new)) + b'\0' if len(new) < len(m.group(0)) else m.group(0)


for path in sys.argv[1:]:
    data = open(path, "rb").read()
    total = 0
    for old, new in REPLACEMENTS:
        n = data.count(old.encode())
        data = data.replace(old.encode(), same_size(old, new))
        total += n
    data, n = TAP.subn(tap_prompt, data)
    total += n
    data, n = OLD.subn(old_prompt, data)
    total += n
    open(path, "wb").write(data)
    print("%s: %d texto(s) trocado(s)" % (path, total))
