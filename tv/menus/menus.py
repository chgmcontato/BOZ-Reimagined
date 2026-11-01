#!/usr/bin/env python3
"""Gera assets/boz_menus.json: telas do jogo, como reconhecê-las e onde ficam os botões.

Coordenadas em 1280x720 (resolução da TV), medidas nos prints de menus/shots/.
Reconhecimento: a imagem do jogo é reduzida para uma grade de 160x90 células (média de 8x8 px em
1280x720) em tons de cinza; cada tela tem uma "sonda" (retângulo estável, em geral o título) e
guarda os valores das células dela. O port compara a sonda com a imagem atual (Menus.java).

Sonda opcional "probe2": a tela só é reconhecida se as duas sondas casarem.
"after" opcional: a tela só é reconhecida vindo de uma destas (diálogos com o mesmo fundo do SWF).
Itens:  ["b", x, y, w, h, tags...]       botão: A toca no centro (tags: "game" volta à partida; "name" abre
                                          o editor de nome; "tap:X,Y" toca em X,Y em vez do centro;
                                          "to:ID" a tela que o toque abre, "to:*" muda de tela sem destino
                                          conhecido; "stay" não muda de tela). A transição é do Cursor.java.
        ["s", x1, y1, x2, y2]             slider: trilho de (x1,y1) a (x2,y2); ←/→ (ou ↑/↓) arrasta o botão
Uso: menus.py <saida.json>
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CELL = 8
SHIFT = 2   # tolerância: a sonda é testada deslocada até ±SHIFT células
GW, GH = 1280 // CELL, 720 // CELL

SCREENS = {
    "title": {"shot": "01_title", "probe": [260, 460, 780, 140], "items": [["b", 440, 620, 400, 60, "to:main"]]},
    # Título Reimagined: o logo pode mudar ou estar ausente; a instrução é estável.
    "title_prompt": {"shot": "01_title_prompt", "probe": [460, 632, 328, 32], "items": [["b", 440, 620, 400, 60, "to:main"]]},
    "main": {"shot": "02_main", "probe": [167, 60, 300, 60], "items": [
        ["b", 180, 70, 290, 250], ["b", 180, 330, 287, 150], ["b", 180, 493, 287, 147],
        ["b", 747, 67, 333, 200], ["b", 747, 287, 333, 193], ["b", 747, 493, 333, 147],
        ["b", 1067, 20, 60, 60]]},
    "single": {"shot": "14_single", "probe": [173, 87, 360, 66], "items": [
        ["b", 220, 240, 427, 320, "selection"], ["b", 647, 133, 460, 507],
        ["b", 170, 575, 215, 111, "tap:267,622"]]},
    "newgame": {"shot": "15_newgame", "probe": [320, 207, 687, 66], "after": ["single"], "items": [
        ["b", 227, 560, 146, 67], ["b", 893, 560, 120, 67, "selection"]]},
    # Layout do CharacterMenu; ações mantêm os pontos de toque do SWF original.
    # A sonda lê somente a SurfaceView do jogo, por baixo do menu Android.
    "charselect": {"shot": "16_charselect", "probe": [500, 620, 330, 50], "items": [
        ["b", 86, 164, 300, 94, "char:0"], ["b", 86, 264, 300, 94, "char:1"],
        ["b", 86, 364, 300, 94, "char:2"], ["b", 86, 464, 300, 94, "char:3"],
        ["b", 850, 174, 344, 64, "map:1"], ["b", 850, 251, 344, 64, "map:2"],
        ["b", 850, 328, 344, 64, "map:3"], ["b", 850, 405, 344, 64, "map:4"],
        ["b", 960, 584, 234, 86, "start"],
        ["b", 86, 584, 200, 86, "tap:267,622"]]},
    "coop": {"shot": "13_coop", "probe": [153, 33, 500, 74], "items": [
        ["b", 233, 133, 254, 460, "to:*"], ["b", 487, 133, 260, 460, "to:coopwifi"], ["b", 747, 113, 266, 400, "to:*"],
        ["b", 868, 498, 56, 56, "stay"], ["b", 175, 580, 217, 111, "to:main"]]},  # moeda dourada: libera o Dead Ops
    # Local Wi-Fi: Single Map / Map Vote abrem o lobby (sem mapa ainda)
    "coopwifi": {"shot": "24_coopwifi", "probe": [195, 50, 470, 45], "items": [
        ["b", 180, 100, 370, 440, "to:coopmap"], ["b", 165, 570, 205, 100, "to:coop"]]},   # Map Vote fora: não é usado
    # Seleção de mapa do co-op (Single Map): os mapas só marcam; Continue abre o lobby
    "coopmap": {"shot": "25_coopmap", "probe": [165, 50, 335, 60], "items": [
        ["b", 175, 95, 225, 365, "stay"], ["b", 405, 70, 235, 385, "stay"], ["b", 640, 65, 235, 370, "stay"],
        ["b", 875, 60, 230, 400, "stay"], ["b", 165, 580, 210, 90, "to:coopwifi"], ["b", 860, 570, 250, 100, "to:*"]]},
    "extras": {"shot": "04_extras", "probe": [193, 47, 307, 73], "items": [
        ["b", 220, 120, 287, 373], ["b", 520, 53, 287, 434], ["b", 820, 53, 260, 180], ["b", 820, 240, 260, 240],
        ["b", 427, 500, 233, 140], ["b", 663, 500, 204, 140], ["b", 873, 496, 207, 144], ["b", 202, 578, 218, 109, "to:main"]]},
    "controls": {"shot": "05_controls", "probe": [133, 47, 520, 60], "items": [
        ["b", 160, 120, 240, 173], ["b", 427, 100, 213, 273],
        ["s", 313, 587, 313, 333], ["s", 440, 467, 720, 467], ["b", 120, 553, 210, 112, "to:*"]]},
    "settings": {"shot": "06_settings", "probe": [160, 53, 333, 67], "items": [
        ["s", 287, 290, 540, 220], ["s", 173, 520, 533, 520], ["s", 660, 176, 983, 176],
        ["b", 955, 100, 56, 56, "stay"], ["b", 159, 570, 216, 114, "to:*"]]},
    "help": {"shot": "08_help", "probe": [153, 53, 287, 67], "items": [
        ["b", 160, 127, 267, 206], ["b", 440, 73, 373, 460], ["b", 820, 53, 287, 500], ["b", 160, 340, 307, 287], ["b", 155, 581, 216, 109, "to:*"]]},
    "quitconfirm": {"shot": "17_quitconfirm", "probe": [470, 205, 380, 40], "after": ["main", "title", "title_prompt"], "items": [
        ["b", 227, 560, 146, 67], ["b", 893, 560, 120, 67]]},
    "pause": {"shot": "18_pause", "probe": [80, 13, 240, 67], "probe2": [90, 590, 220, 90], "items": [
        ["b", 73, 567, 254, 133, "game"], ["b", 840, 33, 173, 187], ["b", 847, 287, 186, 166], ["b", 840, 513, 180, 140]]},
    # configurações da pausa e a subtela de sensibilidade têm o mesmo título: a 2ª sonda (rótulo) as separa
    "pausesettings": {"shot": "20_pausesettings", "probe": [80, 13, 267, 67], "probe2": [1000, 20, 180, 140], "items": [
        ["s", 113, 193, 447, 193], ["s", 113, 351, 447, 351], ["s", 113, 497, 447, 497],
        # Auto Aim / Invert Y / Right Handed: só a caixinha (x≈1035) responde ao toque, não o rótulo
        ["b", 800, 430, 253, 50, "tap:1035,455", "stay"], ["b", 767, 513, 286, 50, "tap:1035,538", "stay"],
        ["b", 767, 593, 286, 54, "tap:1035,620", "stay"], ["b", 993, 13, 200, 154]]},
    "sensitivity": {"shot": "21_sensitivity", "probe": [80, 13, 267, 67], "probe2": [167, 104, 226, 36], "items": [
        ["s", 273, 181, 873, 181], ["s", 273, 368, 873, 368]]},
    # lobby do co-op (Searching host / Setup): o resto da tela é do SWF; o jogo segue sozinho quando conecta
    "cooplobby": {"shot": "26_cooplobby", "probe": [150, 55, 245, 75], "items": [
        ["b", 180, 585, 210, 100, "to:*"]]},
    "savequit": {"shot": "22_savequit", "probe": [140, 335, 1040, 40], "after": ["pause"], "items": [
        ["b", 230, 550, 140, 75], ["b", 920, 550, 150, 75]]},
    # Player Profile: título fixo; a moldura e o nome vêm do StyledScreen (frame).
    "player_profile": {"shot": "23_profile", "probe": [190, 60, 330, 60], "items": [
        ["b", 160, 570, 210, 110, "to:main"]]},
    "gameover": {"shot": "19_gameover", "probe": [53, 27, 274, 186], "items": [
        ["b", 967, 560, 246, 147], ["b", 53, 567, 267, 146]]},
}


def grid(shot):
    """Imagem -> grade GWxGH de cinza (média de blocos de CELL px), igual à que o port calcula."""
    raw = subprocess.run(["convert", str(HERE / "shots" / (shot + ".png")), "-filter", "Box",
                          "-resize", "%dx%d!" % (GW, GH), "-depth", "8", "rgb:-"],
                         check=True, capture_output=True).stdout
    assert len(raw) == GW * GH * 3
    gray = [(299 * raw[i] + 587 * raw[i + 1] + 114 * raw[i + 2]) // 1000 for i in range(0, len(raw), 3)]
    return blur(gray)


def blur(g):
    """Desfoque 3x3 (bordas repetidas): tolera os poucos pixels que os menus deslizam."""
    out = []
    for y in range(GH):
        for x in range(GW):
            acc = 0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    xx = min(GW - 1, max(0, x + dx)); yy = min(GH - 1, max(0, y + dy))
                    acc += g[yy * GW + xx]
            out.append(acc // 9)
    return out


def probe_cells(p):
    x, y, w, h = p
    return x // CELL, y // CELL, max(1, w // CELL), max(1, h // CELL)


def sample(g, cells):
    cx, cy, cw, ch = cells
    return [g[(cy + j) * GW + cx + i] for j in range(ch) for i in range(cw)]


def dist(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def match(ref, g, cells):
    """Menor distância testando a sonda deslocada (os menus deslizam alguns pixels)."""
    cx, cy, cw, ch = cells
    best = 1e9
    for dy in range(-SHIFT, SHIFT + 1):
        for dx in range(-SHIFT, SHIFT + 1):
            if 0 <= cx + dx and cx + dx + cw <= GW and 0 <= cy + dy and cy + dy + ch <= GH:
                best = min(best, dist(ref, sample(g, (cx + dx, cy + dy, cw, ch))))
    return best


# Uma fonte de geometria para desenho Android e foco; destinos continuam no SWF original.
def button_item(b):
    """Botão desenhado no Android: ação opcional ("selection", "game", "name") e ponto tocado no SWF."""
    tags = [b["action"]] if "action" in b else []
    if "to" in b:
        tags.append("to:" + b["to"])
    if "tap" in b:
        tags.append("tap:%d,%d" % tuple(b["tap"]))
    return ["b", *b["bounds"], *tags]


main_layout = json.loads((HERE.parent / "main-menu/assets/layout.json").read_text())
SCREENS["main"]["items"] = [button_item(item) for item in main_layout["items"]]

# Telas redesenhadas (StyledScreen.java): os botões da página substituem os itens medidos no SWF.
styled = json.loads((HERE.parent / "menus-style/assets/screens.json").read_text())["screens"]
for sid, st in styled.items():
    if "buttons" in st:
        SCREENS[sid]["items"] = [button_item(b) for b in st["buttons"]]

grids = {n: grid(s["shot"]) for n, s in SCREENS.items()}
out = {"cell": CELL, "shift": SHIFT, "w": 1280, "h": 720, "screens": []}
THRESHOLD, THRESHOLD2 = 16, 26


def screen_dist(entry, g):
    """Igual ao Menus.java: a sonda principal decide; a 2ª (mais folgada) desempata telas de mesmo título."""
    d = match(entry["ref"], g, entry["probe"])
    if "probe2" in entry and d < THRESHOLD:
        d2 = match(entry["ref2"], g, entry["probe2"])
        d = 999 if d2 >= THRESHOLD2 else (d + d2) / 2
    return d


for n, s in SCREENS.items():
    e = {"id": n, "probe": list(probe_cells(s["probe"])), "items": s["items"]}
    if "after" in s:
        e["after"] = s["after"]
    e["ref"] = sample(grids[n], e["probe"])
    if "probe2" in s:
        e["probe2"] = list(probe_cells(s["probe2"]))
        e["ref2"] = sample(grids[n], e["probe2"])
    out["screens"].append(e)

# quão parecida cada tela fica nas outras (para escolher o limiar)
for e in out["screens"]:
    others = sorted((screen_dist(e, grids[m]), m) for m in grids if m != e["id"])
    print("%-14s mais parecida: %-14s %.1f" % (e["id"], others[0][1], others[0][0]))

# robustez: o mesmo menu capturado em outro momento
g2 = grid("12_main_again")
m = next(s for s in out["screens"] if s["id"] == "main")
print("main vs 12_main_again: %.1f" % screen_dist(m, g2))
Path(sys.argv[1]).write_text(json.dumps(out, separators=(",", ":")))
