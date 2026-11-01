#!/usr/bin/env python3
"""Exporta os menus Java para SVG editáveis (design/<tela>.svg), um por tela, em 1280x720.

Gerado a partir dos mesmos dados que o jogo lê (menus-style/assets/screens.json, main-menu/assets/layout.json)
e das constantes de StyledScreen/MainMenu/CharacterMenu/NameEditor. Camadas nomeadas (Inkscape):
"Referência SWF" (travada: o que o jogo desenha por baixo), "Moldura", "Botões", "Textos", "Foco".
Cada botão tem id = id do JSON, para o redesenho voltar ao código. Fonte: CCAdamKubert (design/comic.ttf).
Uso: python3 -I export_svg.py
"""
import base64, json, shutil
from pathlib import Path
from xml.sax.saxutils import escape

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = HERE / "design"
CM = ROOT / "character-menu/assets"
PAPER, INK, RED, DIM, CAP, LINE = "#F2EEE4", "#151211", "#791D24", "#191614", "#D5C9AC", "#827763"
HINT = "D-PAD: NAVIGATE     A: SELECT     B: BACK"
_cache = {}


def img(path):
    p = Path(path)
    if p not in _cache:
        _cache[p] = "data:image/png;base64," + base64.b64encode(p.read_bytes()).decode()
    return _cache[p]


def asset(name):
    """Nome de asset do APK ("character-menu/x.png", "main-menu/x.png") -> arquivo no repositório."""
    a, b = name.split("/", 1)
    return ROOT / ("character-menu/assets" if a == "character-menu" else "main-menu/assets") / b


class Svg:
    def __init__(self, title):
        self.title, self.layers = title, []

    def layer(self, name, locked=False):
        self.layers.append([name, locked, []])
        return self.layers[-1][2]

    def save(self, path):
        font = base64.b64encode((CM / "comic.ttf").read_bytes()).decode()
        out = ['<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
               'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
               'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" '
               'width="1280" height="720" viewBox="0 0 1280 720">',
               '<title>%s</title>' % escape(self.title),
               '<style>@font-face{font-family:"CCAdamKubert";src:url(data:font/ttf;base64,%s);}'
               'text{font-family:"CCAdamKubert";}</style>' % font]
        for name, locked, items in self.layers:
            out.append('<g id="%s" inkscape:groupmode="layer" inkscape:label="%s"%s>' % (
                name.lower().replace(" ", "-").replace("ê", "e"), escape(name),
                ' sodipodi:insensitive="true"' if locked else ""))
            out += items
            out.append('</g>')
        out.append('</svg>')
        path.write_text("\n".join(out))


def rect(x, y, w, h, fill, extra=""):
    return '<rect x="%g" y="%g" width="%g" height="%g" fill="%s"%s/>' % (x, y, w, h, fill, extra)


def panel(x, y, w, h, fill, skew, ident=None, opacity=None, stroke2=LINE, sw=5):
    d = "M%g,%g L%g,%g L%g,%g L%g,%g Z" % (x, y + skew, x + w, y, x + w - 3, y + h - skew, x + 2, y + h)
    op = ' fill-opacity="%g"' % opacity if opacity else ""
    i = ' id="%s"' % ident if ident else ""
    return ('<g%s><path d="%s" fill="%s"%s stroke="%s" stroke-width="%g"/>'
            '<path d="%s" fill="none" stroke="%s" stroke-width="1.5"/></g>') % (i, d, fill, op, INK, sw, d, stroke2)


_fonts = {}


def width(s, size):
    """Largura do texto na fonte do jogo (como Paint.measureText)."""
    from PIL import ImageFont
    if size not in _fonts:
        _fonts[size] = ImageFont.truetype(str(CM / "comic.ttf"), 100)
    return _fonts[size].getlength(s) * size / 100


def text(s, x, y, size, color, anchor="start", outline=False, ident=None, rotate=None, fit=None):
    """fit = largura máxima: reduz a fonte como o Java (size * fit / largura)."""
    if fit and width(s, size) > fit:
        size = size * fit / width(s, size)
    i = ' id="%s"' % ident if ident else ""
    r = ' transform="rotate(%g %g %g)"' % (rotate, x, y) if rotate else ""
    o = (' stroke="%s" stroke-width="%g" stroke-linejoin="round" paint-order="stroke"' % (INK, size * 0.14)
         if outline else "")
    return '<text%s xml:space="preserve" x="%g" y="%g" font-size="%g" fill="%s" text-anchor="%s"%s%s>%s</text>' % (
        i, x, y, size, color, anchor, o, r, escape(s))


def image(src, x, y, w, h, clip=None, slice_=False, ident=None):
    par = "xMidYMid slice" if slice_ else "none"
    c = ' clip-path="url(#%s)"' % clip if clip else ""
    i = ' id="%s"' % ident if ident else ""
    return '<image%s x="%g" y="%g" width="%g" height="%g" preserveAspectRatio="%s"%s xlink:href="%s"/>' % (
        i, x, y, w, h, par, c, img(src))


def chrome(svg, title=None):
    m = svg.layer("Moldura")
    m.append(rect(0, 0, 1280, 720, PAPER))
    m.append(image(CM / "paper.png", 0, 0, 1280, 720, ident="papel"))
    m.append(rect(48, 134, 1184, 540, INK, ' id="corpo"'))
    m.append(rect(64, 38, 1152, 96, INK, ' id="faixa-titulo"'))
    t = svg.layer("Textos")
    t.append(text("BLACK OPS ZOMBIES", 86, 80, 30, PAPER, ident="marca-1"))
    t.append(text("REIMAGINED", 85, 119, 42, PAPER, ident="marca-2"))
    if title:
        t.append(text(title, 1194, 100, 33, PAPER, "end", ident="titulo", fit=420))
    return m, t


def tile(b, items, texts, n, primary_red):
    """Botão de página (MainMenu/StyledScreen): painel, arte em center-crop, faixa de legenda."""
    x, y, w, h = b["bounds"]
    cap = b.get("caption", "")
    red = primary_red(b)
    fill = RED if red and "art" not in b else DIM
    clip = "clip-%s-%d" % (b["id"], n)
    d = "M%g,%g L%g,%g L%g,%g L%g,%g Z" % (x, y + 6, x + w, y, x + w - 3, y + h - 6, x + 2, y + h)
    items.append('<g id="%s"><defs><clipPath id="%s"><path d="%s"/></clipPath></defs>' % (b["id"], clip, d))
    items.append('<g clip-path="url(#%s)">' % clip + rect(x, y, w, h, fill, ' fill-opacity="0.87"' if fill == DIM else ""))
    if "art" in b:
        items.append(image(asset(b["art"]), x, y, w, h - 100, slice_=True))
    if cap:
        items.append(rect(x, y + h - 112, w, 112, RED if red else INK))
        items.append(text(b["title"], x + 16, y + h - 68, 38, PAPER, fit=w - 32))
        items.append(text(cap, x + 16, y + h - 27, 19, CAP, fit=w - 32))
    else:
        size = min(31, h * 0.42)
        items.append(text(b["title"], x + 22, y + h / 2 + size * 0.36, size, PAPER, fit=w - 44))
    items.append('</g><path d="%s" fill="none" stroke="%s" stroke-width="5"/>'
                 '<path d="%s" fill="none" stroke="%s" stroke-width="1.5"/></g>' % (d, INK, d, LINE))


def focus(svg, b):
    x, y, w, h = b
    c = min(34, min(w, h) * 0.32)
    d = ("M{l},{tc} L{l},{t} L{lc},{t} M{rc},{t} L{r},{t} L{r},{tc} M{l},{bc} L{l},{b} L{lc},{b} "
         "M{rc},{b} L{r},{b} L{r},{bc}").format(l=x, t=y, r=x + w, b=y + h, lc=x + c, rc=x + w - c,
                                                 tc=y + c, bc=y + h - c)
    f = svg.layer("Foco (exemplo)")
    f.append('<path d="%s" fill="none" stroke="#000" stroke-width="9" stroke-linecap="square"/>' % d)
    f.append('<path d="%s" fill="none" stroke="#fff" stroke-width="5" stroke-linecap="square"/>' % d)


def footer(t, s, page=True):
    if not page:
        t.append(rect(380, 684, 520, 30, INK, ' id="rodape-fundo"'))
    t.append(text(s, 640, 698 if page else 706, 20 if page else 17, INK if page else PAPER, "middle", ident="rodape"))


SHOTS = {"settings": "06_settings", "pausesettings": "20_pausesettings", "sensitivity": "21_sensitivity",
         "controls": "05_controls", "help": "08_help", "coop": "13_coop", "extras": "04_extras",
         "player_profile": "23_profile", "pause": "18_pause", "gameover": "19_gameover"}


def main():
    OUT.mkdir(exist_ok=True)
    shutil.copy(CM / "comic.ttf", OUT / "comic.ttf")
    screens = json.loads((HERE / "assets/screens.json").read_text())["screens"]
    layout = json.loads((ROOT / "main-menu/assets/layout.json").read_text())["items"]
    made = []

    # Menu principal (MainMenu.java)
    svg = Svg("Main menu")
    _, t = chrome(svg)
    t.append(text("MAIN MENU", 942, 100, 33, PAPER, ident="titulo"))
    b = svg.layer("Botões")
    for i, it in enumerate(layout):
        it = dict(it)
        if it["id"] == "name":
            it["title"] = "PLAYER: HEFESTO"
        tile(it, b, None, i, lambda x: x["id"] == "single")
    footer(svg.layer("Rodapé"), HINT)
    focus(svg, layout[0]["bounds"])
    svg.save(OUT / "01_main.svg"); made.append("01_main")

    # Telas do StyledScreen
    n = 2
    for sid, st in screens.items():
        svg = Svg(st["title"])
        if sid in SHOTS:
            svg.layer("Referência SWF", locked=True).append(
                image(ROOT / "menus/shots" / (SHOTS[sid] + ".png"), 0, 0, 1280, 720))
        if st["kind"] == "page":
            m, t = chrome(svg, st["title"])
            # janelas: o SWF aparece por baixo (números dinâmicos)
            if st.get("windows"):
                m.append('<g id="janelas-swf"><title>Janelas: o SWF aparece aqui (recorte)</title>')
                for w in st["windows"]:
                    m.append(image(ROOT / "menus/shots" / (SHOTS[sid] + ".png"), 0, 0, 1280, 720,
                                   clip="win-%s-%d" % (sid, w[0])).replace(
                        "<image", '<defs><clipPath id="win-%s-%d">%s</clipPath></defs><image' % (
                            sid, w[0], rect(*w, "#000")), 1))
                    m.append(rect(*w, "none", ' stroke="%s" stroke-width="6"' % INK))
                    m.append(rect(*w, "none", ' stroke="%s" stroke-width="1.5"' % LINE))
                m.append('</g>')
            for i, line in enumerate(st.get("message", [])):
                t.append(text(line, 640, 260 + i * 56, 40, PAPER, "middle", True, ident="mensagem-%d" % (i + 1)))
            b = svg.layer("Botões")
            for i, bt in enumerate(st.get("buttons", [])):
                tile(bt, b, None, i, lambda x: x.get("primary"))
            footer(svg.layer("Rodapé"), st.get("hint", HINT))
            if st.get("buttons"):
                focus(svg, st["buttons"][0]["bounds"])
        else:
            m = svg.layer("Moldura")
            x, y, w, h = st["tag"]
            m.append(panel(x, y, w, h, INK, 5, ident="tarja-titulo"))
            size = min(44, h * 0.62)
            t = svg.layer("Textos")
            t.append(text(st["title"], x + 18, y + h / 2 + size * 0.36, size, PAPER, ident="titulo", fit=w - 36))
            if "name" in st:
                x, y, w, h = st["name"]
                m.append(panel(x, y, w, h, INK, 4, ident="placa-nome"))
                ns = min(30, h * 0.62)
                t.append(text("NAME:", x + 16, y + h / 2 + ns * 0.36, ns, CAP, ident="nome-rotulo"))
                nx = x + 24 + width("NAME:", ns)
                t.append(text("HEFESTO", nx, y + h / 2 + ns * 0.36, ns, PAPER, ident="nome-valor", fit=x + w - 16 - nx))
            footer(svg.layer("Rodapé"), st.get("hint", HINT), page=False)
        name = "%02d_%s" % (n, sid); n += 1
        svg.save(OUT / (name + ".svg")); made.append(name)

    # Seleção de personagem (CharacterMenu.java)
    maps = ["KINO DER TOTEN", "DEAD OPS ARCADE", "ASCENSION", "CALL OF THE DEAD"]
    names = [("NIKOLAI", "BELINSKI"), ("TAKEO", "MASAKI"), ("DR. EDWARD", "RICHTOFEN"), ("TANK", "DEMPSEY")]
    svg = Svg("Select character")
    m = svg.layer("Moldura")
    m += [rect(0, 0, 1280, 720, INK), image(CM / "paper.png", 0, 0, 1280, 720), rect(48, 80, 1184, 582, INK),
          image(CM / "background.png", 64, 96, 1152, 556, ident="cenario"), rect(64, 96, 1152, 556, "#000", ' fill-opacity="0.39"'),
          image(CM / "title.png", 66, 34, 742, 119), image(CM / "title.png", 837, 73, 374, 79)]
    t = svg.layer("Textos")
    t.append(text("SELECT CHARACTER", 103, 108, 39, INK, rotate=-3, ident="titulo"))
    t.append(text("SELECT MAP", 869, 125, 28, INK, rotate=-3, ident="titulo-mapa"))
    b = svg.layer("Botões")
    for i, (a, z) in enumerate(names):
        y = 164 + i * 100
        b.append('<g id="char-%d">' % i + image(CM / (["nikolai", "takeo", "richtofen", "dempsey"][i] + ".png"), 86, y, 300, 94))
        x = 211 if i in (0, 3) else 101
        base = y + (42 if i in (0, 3) else 29)
        b.append(text(a, x, base, 23, PAPER, outline=True) + text(z, x, base + 25, 23, PAPER, outline=True) + "</g>")
    b.append(image(CM / "tick.png", 350, 167, 34, 37, ident="marca-personagem"))
    b.append(panel(410, 155, 410, 399, INK, 7, ident="quadro-mapa", sw=6, stroke2="#9C9687"))
    b.append(image(CM / "background.png", 413, 155, 404, 399, slice_=True))
    b.append(image(CM / "title.png", 418, 467, 392, 76))
    b.append(text(maps[0], 445, 515, 27, INK, rotate=-3, ident="nome-mapa"))
    for i, mp in enumerate(maps):
        y = 174 + i * 77
        b.append('<g id="map-%d">' % (i + 1) + panel(850, y, 344, 64, RED if i == 0 else DIM, 4 if i % 2 == 0 else -3,
                 opacity=None if i == 0 else 0.87, sw=6, stroke2="#9C9687") + text(mp, 866, y + 41, 24, PAPER, outline=True) + "</g>")
    b.append(image(CM / "tick.png", 1151, 187, 36, 38))
    b.append('<g id="back">' + image(CM / "back.png", 80, 577, 215, 94) + text("BACK", 113, 630, 33, PAPER, outline=True) + "</g>")
    b.append('<g id="start">' + image(CM / "start.png", 952, 577, 254, 94) + text("START", 984, 632, 38, PAPER, outline=True) + "</g>")
    t.append(text("READY TO SURVIVE!", 408, 620, 25, PAPER, outline=True, ident="status"))
    t.append(text("PLAYER: HEFESTO", 408, 648, 19, CAP, outline=True, ident="jogador"))
    t.append(text(HINT, 265, 698, 20, INK, ident="rodape"))
    focus(svg, [86, 164, 300, 94])
    svg.save(OUT / ("%02d_charselect.svg" % n)); made.append("%02d_charselect" % n); n += 1

    # Editor de nome (NameEditor.java)
    svg = Svg("Player name")
    _, t = chrome(svg)
    t.append(text("PLAYER NAME", 900, 100, 33, PAPER, ident="titulo"))
    b = svg.layer("Botões")
    for i in range(2):
        y = 170 + i * 110
        b.append('<g id="slot-%d">' % (i + 1) + panel(80, y, 270, 90, RED if i == 0 else DIM, 4 if i == 0 else -3, sw=4,
                 opacity=None if i == 0 else 0.87) + text("PLAYER %d%s" % (i + 1, "  (CO-OP)" if i else ""), 98, y + 32, 20, CAP)
                 + text("HEFESTO" if i == 0 else "PLAYER 2", 98, y + 72, 28, PAPER, outline=True) + "</g>")
    for i, s in enumerate(["LB / RB: SWITCH PLAYER", "UP TO 16 CHARACTERS", "EMPTY = DEFAULT NAME"]):
        t.append(text(s, 92, 420 + i * 30, 19, CAP))
    b.append('<g id="campo-nome">' + panel(380, 164, 816, 88, PAPER, 5, sw=4) + text("HEFESTO_", 402, 226, 46, INK)
             + text("7/16", 1178, 240, 20, LINE, "end") + "</g>")
    rows = ["1234567890", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-_.!"]
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            x, y = 380 + c * 82, 280 + r * 70
            b.append('<g id="tecla-%s">' % ("ponto" if ch == "." else ch) + panel(x, y, 78, 62, DIM, 2 if (r + c) % 2 == 0 else -2, sw=4, opacity=0.87)
                     + text(ch, x + 39, y + 44, 30, PAPER, "middle", True) + "</g>")
    col = 0
    for i, (a, span) in enumerate(zip(["SPACE", "DELETE", "CANCEL", "SAVE"], [3, 3, 2, 2])):
        x, y, w = 380 + col * 82, 280 + 4 * 70, span * 82 - 4
        b.append('<g id="acao-%s">' % a.lower() + panel(x, y, w, 62, RED if a == "SAVE" else DIM, 2 if i % 2 == 0 else -2, sw=4,
                 opacity=None if a == "SAVE" else 0.87) + text(a, x + w / 2, y + 42, 26, PAPER, "middle", True) + "</g>")
        col += span
    t.append(text("D-PAD: MOVE   A: TYPE   B: DELETE   X: SPACE   Y: CANCEL   START: SAVE", 640, 698, 20, INK, "middle", ident="rodape"))
    focus(svg, [380, 350, 78, 62])
    svg.save(OUT / ("%02d_name_editor.svg" % n)); made.append("%02d_name_editor" % n); n += 1

    # Cortina de transição (StyledScreen.drawCurtain)
    svg = Svg("Transition curtain")
    m, t = chrome(svg)
    svg.layer("Traço de espera").append(
        '<rect id="traco" x="500" y="129" width="220" height="5" fill="%s"><title>Corre da esquerda para a direita '
        'em 1,1 s, só depois de 250 ms de espera</title></rect>' % RED)
    svg.save(OUT / ("%02d_transition.svg" % n)); made.append("%02d_transition" % n)
    print("\n".join(made))


if __name__ == "__main__":
    main()
