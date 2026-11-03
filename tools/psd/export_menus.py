#!/usr/bin/env python3
"""Exports the Reimagined menus as layered PSDs, SVGs (live text), PNG previews and layers.json.

The menus are drawn by code (src/menus.c, src/reimagined.c), so the layers are rebuilt from the same
geometry. PSD pixels = 1280x720 logical coordinates x 1.5. The art comes from the installed game and
is never committed: run with --assets build/game/reimagined/assets.
"""
import argparse, json, math, os, sys
from html import escape
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psdwrite import Layer, write_psd

K = 1.5
W, H = 1920, 1080
PAPER, INK, RED, SAND, EDGE = (0xf2, 0xee, 0xe4, 255), (0x15, 0x12, 0x11, 255), (0x79, 0x1d, 0x24, 255), (0xd5, 0xc9, 0xac, 255), (0x82, 0x77, 0x63, 255)
YELLOW, BLACK, DARK, BODY, MUTED = (0xf5, 0xc5, 0x18, 255), (7, 6, 6, 255), (0x11, 0x11, 0x11, 255), (0x0d, 0x0c, 0x0b, 255), (0xbc, 0xb8, 0xad, 255)
RAY = (0x1d, 0x12, 0x11, 255)
SX0, SY0, SX1, SY1, BX0, BY0, BX1, BY1 = 44, 14, 1236, 690, 72, 62, 1208, 668
GROUPS = ["BACKGROUND", "NATIVE_REFERENCE", "PANELS", "DECORATIONS", "BUTTONS", "ICONS", "TEXT", "OVERLAYS", "VARIANTS", "HIT_AREAS"]
HIDDEN = {"NATIVE_REFERENCE", "VARIANTS", "HIT_AREAS"}
NAMES = [("NIKOLAI", "BELINSKI"), ("TAKEO", "MASAKI"), ("DR. EDWARD", "RICHTOFEN"), ("TANK", "DEMPSEY")]
MAPS = ["KINO DER TOTEN", "DEAD OPS ARCADE", "ASCENSION", "CALL OF THE DEAD"]


class Art:
    def __init__(self, root):
        self.root = root
        self.cache = {}
        self.font_path = os.path.join(root, "character-menu/comic.ttf")

    def __call__(self, rel):
        if rel not in self.cache:
            self.cache[rel] = Image.open(os.path.join(self.root, rel)).convert("RGBA")
        return self.cache[rel]

    def font(self, size):
        # the game (stb_truetype) sizes by ascent-to-descent height, Pillow by em
        if not hasattr(self, "em_ratio"):
            probe = ImageFont.truetype(self.font_path, 1000)
            self.em_ratio = 1000 / sum(probe.getmetrics())
        return ImageFont.truetype(self.font_path, size * K * self.em_ratio)


class Screen:
    def __init__(self, name, art):
        self.name, self.art, self.layers = name, art, []
        self.holes = []

    def add(self, group, name, image, source, role="visual", rect=None, svg=None, visible=None):
        bbox = image.getbbox() or (0, 0, 1, 1)
        self.layers.append(dict(group=group, name=name, image=image.crop(bbox), left=bbox[0], top=bbox[1], source=source,
                                role=role, rect=rect, svg=svg or {}, visible=visible))

    def canvas(self):
        return Image.new("RGBA", (W, H), (0, 0, 0, 0))

    # ---- primitives, in logical coordinates ----
    def poly(self, group, name, pts, fill, source, outline=None, role="visual", rect=None):
        im = self.canvas()
        scale = 2
        big = Image.new("RGBA", (W * scale, H * scale), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        p = [(x * K * scale, y * K * scale) for x, y in pts]
        if fill:
            d.polygon(p, fill=fill)
        for width, col in outline or []:
            d.line(p + [p[0]], fill=col, width=int(width * K * scale), joint="curve")
        im = big.resize((W, H), Image.LANCZOS)
        self.add(group, name, im, source, role, rect or bounds(pts), dict(kind="polygon", pts=pts, fill=fill, outline=outline))

    def image(self, group, name, rel, x, y, w, h, source, alpha=1.0, role="visual", part=None, visible=None):
        src = self.art(rel)
        if part:
            u0, v0, u1, v1 = part
            src = src.crop((int(u0 * src.width), int(v0 * src.height), int(u1 * src.width), int(v1 * src.height)))
        pic = src.resize((max(1, round(w * K)), max(1, round(h * K))), Image.LANCZOS)
        if alpha < 1:
            pic.putalpha(pic.getchannel("A").point(lambda v: int(v * alpha)))
        im = self.canvas()
        im.alpha_composite(pic, (0, 0)) if False else im.paste(pic, (round(x * K), round(y * K)), pic)
        self.add(group, name, im, source, role, [x, y, w, h], dict(kind="image", href=rel, part=part, alpha=alpha), visible)

    def text(self, group, name, s, x, base, size, col, source, max_w=1e9, outline=False, align="l", role="text", visible=None):
        f = self.art.font(size)
        w = f.getlength(s) / K
        if w > max_w and w > 0:
            size = size * max_w / w
            f = self.art.font(size)
            w = f.getlength(s) / K
        if align == "c":
            x -= w / 2
        elif align == "r":
            x -= w
        im = self.canvas()
        d = ImageDraw.Draw(im)
        px, py = x * K, base * K
        if outline:
            o = max(1.0, size * 0.06 * K)
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)):
                d.text((px + dx * o, py + dy * o), s, font=f, fill=DARK, anchor="ls")
        d.text((px, py), s, font=f, fill=col, anchor="ls")
        self.add(group, name, im, source, role, [x, base - size, w, size * 1.2], dict(kind="text", text=s, x=x, y=base, size=size, fill=col, outline=outline), visible)


def bounds(pts):
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return [min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)]


def comic(x, y, w, h, skew):
    return [(x, y + skew), (x + w, y), (x + w - 3, y + h - skew), (x + 2, y + h)]


def torn(i):
    return ((i * 2654435761) & 0xffffffff) >> 24 & 0xff if False else (((i * 2654435761) & 0xffffffff) >> 24) % 9


def sheet(s, body=True, source="kit_sheet"):
    s.poly("BACKGROUND", "black surround", [(0, 0), (1280, 0), (1280, 720), (0, 720)], BLACK, source)
    im = s.canvas()
    pic = s.art("character-menu/paper.png").resize((W, H), Image.LANCZOS)
    mask = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(mask)
    d.rectangle([SX0 * K, SY0 * K, SX1 * K, SY1 * K], fill=255)
    def edge(x0, y0, x1, y1, nx, ny, seed):
        n = int(math.hypot(x1 - x0, y1 - y0) / 38)
        for i in range(n):
            t0, t1, m = i / n, (i + 1) / n, (i + .5) / n
            dep = 2 + torn(seed + i)
            d.polygon([((x0 + (x1 - x0) * t0) * K, (y0 + (y1 - y0) * t0) * K), ((x0 + (x1 - x0) * t1) * K, (y0 + (y1 - y0) * t1) * K),
                       ((x0 + (x1 - x0) * m + nx * dep) * K, (y0 + (y1 - y0) * m + ny * dep) * K)], fill=0)
    edge(SX0, SY0, SX1, SY0, 0, 1, 0)
    edge(SX1, SY0, SX1, SY1, -1, 0, 40)
    edge(SX0, SY1, SX1, SY1, 0, -1, 80)
    edge(SX0, SY0, SX0, SY1, 1, 0, 120)
    im.paste(pic, (0, 0), mask)
    s.add("BACKGROUND", "torn paper sheet", im, source + " + character-menu/paper.png", rect=[SX0, SY0, SX1 - SX0, SY1 - SY0])
    if body:
        s.poly("BACKGROUND", "ink body", [(BX0, BY0 + 4), (BX1, BY0), (BX1 - 4, BY1 - 4), (BX0 + 3, BY1)], BODY, source)
        rays(s, source)


def rays(s, source):
    im = s.canvas()
    d = ImageDraw.Draw(im)
    cx, cy, r = 640, 400, 760
    for i in range(28):
        a0 = i * 0.2244; a1 = a0 + 0.11
        c = lambda v, lo, hi: min(hi, max(lo, v))
        p0 = (c(cx + r * math.cos(a1), BX0 + 4, BX1 - 4), c(cy + r * math.sin(a1), BY0 + 6, BY1 - 4))
        p1 = (c(cx + r * math.cos(a0), BX0 + 4, BX1 - 4), c(cy + r * math.sin(a0), BY0 + 6, BY1 - 4))
        d.polygon([(cx * K, cy * K), (p0[0] * K, p0[1] * K), (p1[0] * K, p1[1] * K)], fill=RAY)
    s.add("BACKGROUND", "body rays", im, "kit_rays", rect=[BX0, BY0, BX1 - BX0, BY1 - BY0])


def strip(s, x, y, w, title, name="title"):
    h = w / 4
    s.image("DECORATIONS", name + " strip", "character-menu/title.png", x, y, w, h, "kit_strip")
    size = min(h * 0.42, 44)
    s.text("TEXT", name, title, x + w * 0.09, y + h * 0.45, size, INK, "kit_strip", w * 0.80)


def page_title(s, title):
    strip(s, 60, 8, 520, title)
    s.text("TEXT", "brand line 1", "BLACK OPS ZOMBIES", 1188, 112, 26, SAND, "kit_page_title", 560, True, "r")
    s.text("TEXT", "brand line 2", "REIMAGINED", 1188, 152, 38, PAPER, "kit_page_title", 560, True, "r")


def hint(s, text="CLICK: SELECT     ESC: BACK"):
    s.text("TEXT", "footer hint", text, 640, 711, 18, SAND, "hint", 1000, False, "c")


def arrow(s, back, x, y, w, h, label, name, dim=False, hit=None):
    iw = min(w, max(h * 2.2, 240)); ih = iw / 2; ix = x + (w - iw) / 2; iy = y + h - ih + 8
    s.image("BUTTONS", name + " arrow", "character-menu/back.png" if back else "character-menu/start.png", ix, iy, iw, ih, "kit_arrow", alpha=.55 if dim else 1)
    size = min(ih * .36, 38)
    s.text("TEXT", name + " label", label, ix + iw * (.56 if back else .46), iy + ih * .48, size, MUTED if dim else PAPER, "kit_arrow", iw * .72, True, "c")
    hit_area(s, name, *(hit or (x, y, w, h)))


def hit_area(s, name, x, y, w, h):
    s.poly("HIT_AREAS", "HIT " + name, [(x, y), (x + w, y), (x + w, y + h), (x, y + h)], (255, 0, 200, 90), "add_target/build_targets", role="hit", rect=[x, y, w, h])


def panel(s, x, y, w, h, art, title, caption, name, selected=False, dim=False, hit=True):
    pts = comic(x, y, w, h, 6 if h > 100 else 4)
    s.poly("PANELS", name + " panel", pts, INK, "kit_panel")
    if art:
        src = s.art(art)
        im = s.canvas()
        if src.width == src.height:
            side = max(w, h) * .86
            pic = src.resize((round(side * K), round(side * K)), Image.LANCZOS)
            pos = (round((x + (w - side) / 2) * K), round((y + (h - side) * .35) * K))
        else:
            sc = max(w / src.width, h / src.height)
            pic = src.resize((round(src.width * sc * K), round(src.height * sc * K)), Image.LANCZOS)
            pos = (round((x + w / 2) * K - pic.width / 2), round((y + h / 2) * K - pic.height / 2))
        im.paste(pic, pos, pic)
        mask = Image.new("L", (W, H), 0)
        ImageDraw.Draw(mask).polygon([(a * K, b * K) for a, b in pts], fill=255)
        im.putalpha(Image.composite(im.getchannel("A"), Image.new("L", (W, H), 0), mask))
        s.add("PANELS", name + " art", im, "kit_panel + " + art, rect=[x, y, w, h], svg=dict(kind="image", href=art, clip=pts))
        s.poly("PANELS", name + " caption shade", [(x, y + h * .62), (x + w, y + h * .62), (x + w, y + h), (x, y + h)], (0, 0, 0, 0x70), "kit_panel")
        if dim:
            s.poly("PANELS", name + " dim", pts, (0, 0, 0, 0x58), "kit_panel: not selected")
    small = h <= 100
    size = min(30, h * .44) if small else min(40, w * .16)
    base = y + h / 2 + size * .36 if small else y + h - (48 if caption else 22)
    s.text("TEXT", name + " title", title, x + 18, base, size, YELLOW if selected else PAPER, "kit_panel", w - 36, True)
    if caption:
        s.text("TEXT", name + " caption", caption, x + 18, y + h - 18, 20, SAND, "kit_panel", w - 36, True)
    s.poly("PANELS", name + " frame", pts, None, "kit_panel", outline=[(5, INK), (1.5, RED if selected else EDGE)])
    if hit:
        hit_area(s, name, x, y, w, h)
    focus(s, name, x, y, w, h)


def focus(s, name, x, y, w, h):
    im = s.canvas()
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = x - 6, y - 6, x + w + 6, y + h + 6
    ln = min(34, min(w, h) / 3)
    for th, c in ((8, INK), (4, (255, 255, 255, 255))):
        for r in ((x0, y0, x0 + ln, y0 + th), (x0, y0, x0 + th, y0 + ln), (x1 - ln, y0, x1, y0 + th), (x1 - th, y0, x1, y0 + ln),
                  (x0, y1 - th, x0 + ln, y1), (x0, y1 - ln, x0 + th, y1), (x1 - ln, y1 - th, x1, y1), (x1 - th, y1 - ln, x1, y1)):
            d.rectangle([v * K for v in r], fill=c)
    s.add("VARIANTS", name + " [focused]", im, "draw_focus (drawn in code)", role="variant", rect=[x0, y0, x1 - x0, y1 - y0])


def burst(s, messages):
    s.poly("BACKGROUND", "black", [(0, 0), (1280, 0), (1280, 720), (0, 720)], (0x0a, 0x08, 0x07, 255), "kit_burst")
    im = s.canvas(); d = ImageDraw.Draw(im)
    for i in range(16):
        a0 = i * .3927 + .08; a1 = a0 + .2 + .05 * (i % 3)
        d.polygon([(640 * K, 360 * K), ((640 + 900 * math.cos(a0)) * K, (360 + 900 * math.sin(a0)) * K), ((640 + 900 * math.cos(a1)) * K, (360 + 900 * math.sin(a1)) * K)],
                  fill=(0x6b, 0x3a, 0x1c, 255) if i % 2 else (0x8a, 0x4d, 0x25, 255))
    s.add("BACKGROUND", "burst rays", im, "kit_burst", rect=[0, 0, 1280, 720])
    s.poly("BACKGROUND", "burst shade", [(0, 0), (1280, 0), (1280, 720), (0, 720)], (0, 0, 0, 0x40), "kit_burst")
    pts = comic(90, 250, 1100, 60 + len(messages) * 56, 6)
    s.poly("PANELS", "message plate", pts, (0x0d, 0x0c, 0x0b, 0xe8), "draw_page", outline=[(5, INK), (1.5, EDGE)])
    for i, m in enumerate(messages):
        s.text("TEXT", f"message {i + 1}", m, 640, 300 + i * 56, 38, PAPER, "draw_page", 1080, True, "c")


def button(s, b, source_art=None):
    id_ = b["id"]; x, y, w, h = b["bounds"]
    if id_ in ("back", "no", "quit", "return"):
        arrow(s, True, x, y, w, h, b["title"], id_)
    elif id_ in ("ok", "yes", "retry"):
        arrow(s, False, x, y, w, h, b["title"], id_)
    else:
        panel(s, x, y, w, h, source_art or b.get("art"), b["title"], b.get("caption", ""), id_)


def build(art, styles, main_items):
    out = {}
    st = lambda k: styles[k]
    def new(n):
        out[n] = Screen(n, art)
        return out[n]

    s = new("main"); sheet(s); page_title(s, "MAIN MENU")
    for it in main_items:
        it = dict(it)
        if it["id"] == "name":
            it["title"] = "PLAYER: NAME"
        button(s, it)
    hint(s)

    s = new("single"); sheet(s); page_title(s, st("single")["title"])
    for b in st("single")["buttons"]:
        button(s, b, b.get("art") or {"new": "character-menu/kino.png", "continue": "character-menu/cotd.png"}.get(b["id"]))
    hint(s)

    for n in ("newgame", "quitconfirm", "savequit", "leavecoop", "reconnectconfirm"):
        s = new(n); burst(s, st(n).get("message", []))
        for b in st(n)["buttons"]:
            button(s, b)
        hint(s)

    s = new("pause_solo"); sheet(s)
    win = st("pause")["windows"][0]
    s.image("PANELS", "map scenery (one per map)", "character-menu/background.png", BX0 + 6, BY0 + 6, BX1 - BX0 - 12, BY1 - BY0 - 12, "kit_cover",
            part=(0, (1 - (1024 * (BY1 - BY0 - 12) / (BX1 - BX0 - 12)) / 1024) * .4, 1, (1 - (1024 * (BY1 - BY0 - 12) / (BX1 - BX0 - 12)) / 1024) * .4 + (BY1 - BY0 - 12) / (BX1 - BX0 - 12)))
    s.poly("PANELS", "map scenery shade", [(BX0 + 6, BY0 + 6), (BX1 - 6, BY0 + 6), (BX1 - 6, BY1 - 6), (BX0 + 6, BY1 - 6)], (0, 0, 0, 0x8c), "kit_cover")
    s.holes.append(win)
    fr = [(win[0] - 3, win[1] - 3), (win[0] + win[2] + 3, win[1] - 3), (win[0] + win[2] + 3, win[1] + win[3] + 3), (win[0] - 3, win[1] + win[3] + 3)]
    page_title(s, st("pause")["title"])
    for b in st("pause")["buttons"]:
        button(s, b)
    s.poly("OVERLAYS", "native card frame", fr, None, "draw_page", outline=[(6, INK)])
    s.text("TEXT", "player name", "TESTE", 196, 196, 44, PAPER, "draw_page", 640, True)
    s.text("TEXT", "map and round", "KINO DER TOTEN   -   ROUND 1", 198, 236, 26, YELLOW, "draw_page", 640, True)
    hint(s, "CLICK: SELECT     ESC: RESUME")

    s = new("gameover"); sheet(s); s.holes.append([120, 140, 700, 390]); page_title(s, st("gameover")["title"])
    for b in st("gameover")["buttons"]:
        button(s, b)
    s.poly("OVERLAYS", "native card frame", [(117, 137), (823, 137), (823, 533), (117, 533)], None, "draw_page", outline=[(6, INK)])
    hint(s)

    def coop_entry(n, title, buttons, rejoin_off=False):
        s = new(n); sheet(s); page_title(s, title)
        for b in buttons:
            button(s, b)
        hint(s)
    wifi = dict(id="wifi", title="LOCAL WI-FI", caption="HOST OR JOIN ON YOUR NETWORK", bounds=[96, 170, 650, 380], art="main-menu/coop.png")
    rejoin = dict(id="rejoin", title="RE-JOIN", caption="LAST DISCONNECTED SESSION", bounds=[770, 170, 410, 380], art="main-menu/profile.png")
    back = dict(id="back", title="BACK", bounds=[76, 560, 240, 104])
    coop_entry("coop", "CO-OP MULTIPLAYER", [wifi, rejoin, back])
    s = out["coop"]
    # the disabled RE-JOIN, separate and hidden in the same file
    s.poly("VARIANTS", "rejoin [disabled] dim", comic(770, 170, 410, 380, 6), (0, 0, 0, 0x58), "draw_tile: rejoin without session", role="variant")
    s.text("VARIANTS", "rejoin [disabled] caption", "NO SESSION TO RE-JOIN", 788, 532, 20, SAND, "draw_tile", 374, True, role="variant")
    coop_entry("coopwifi", "LOCAL WI-FI", [dict(id="single-map", title="SINGLE MAP", caption="CHOOSE THE MAP FOR EVERYONE", bounds=[96, 170, 530, 380], art="character-menu/ascension.png"),
                                           dict(id="map-vote", title="MAP VOTE", caption="LET THE PARTY DECIDE", bounds=[650, 170, 530, 380], art="character-menu/kino.png"), back])

    s = new("charselect"); sheet(s)
    slots = [(74, 74, 104, 214, 108), (150, 178, 100, 106, 236), (288, 288, 100, 106, 326), (364, 388, 112, 218, 468)]
    portraits = ["nikolai", "takeo", "richtofen", "dempsey"]
    for i in (3, 2, 1, 0):
        s.image("PANELS", "portrait " + portraits[i], f"character-menu/{portraits[i]}.png", 86, slots[i][0], 290, 190, "draw_charselect k_chars")
    for i, c in enumerate(slots):
        col = YELLOW if i == 0 else PAPER
        s.text("TEXT", f"{portraits[i]} name 1", NAMES[i][0], c[3], c[4], 23, col, "draw_charselect", 170, True)
        s.text("TEXT", f"{portraits[i]} name 2", NAMES[i][1], c[3], c[4] + 25, 23, col, "draw_charselect", 170, True)
        hit_area(s, portraits[i], 86, c[1], 290, c[2])
    s.image("ICONS", "character tick", "character-menu/tick.png", slots[0][3] + 112, slots[0][4] - 2, 38, 38, "draw_charselect", role="variant")
    thumbs = ["kino", "deadops", "ascension", "cotd"]
    for i in range(4):
        x, y = 400 + i * 202, 112 - (4 if i % 2 else 0)
        panel(s, x, y, 196, 330, f"character-menu/{thumbs[i]}.png", MAPS[i], "", "map " + thumbs[i], selected=i == 0, dim=i != 0)
    s.image("ICONS", "map tick", "character-menu/tick.png", 400 + 196 - 52, 122, 42, 42, "draw_charselect", role="variant")
    strip(s, 58, 4, 470, "SELECT CHARACTER", "character title")
    strip(s, 700, 2, 400, "SELECT MAP", "map title")
    arrow(s, True, 76, 548, 240, 116, "BACK", "back", hit=(76, 560, 240, 104))
    arrow(s, False, 960, 548, 240, 116, "START", "start", hit=(960, 560, 240, 104))
    s.text("TEXT", "status", "READY TO SURVIVE!", 640, 590, 38, PAPER, "draw_charselect (status changes in code)", 560, True, "c")
    s.text("TEXT", "player", "PLAYER: NAME", 640, 628, 24, SAND, "draw_charselect", 560, True, "c")
    hint(s)

    s = new("pause_coop_box")
    s.poly("OVERLAYS", "box", comic(448, 26, 410, 112, 3), (0x0d, 0x0c, 0x0b, 0xf0), "draw_coop_pause_page", outline=[(5, INK), (1.5, EDGE)])
    s.text("TEXT", "map and round", "KINO DER TOTEN - ROUND 1", 464, 70, 25, PAPER, "draw_coop_pause_page", 378, True)
    panel(s, 460, 88, 386, 40, None, "TIMER: ON", "", "timer")

    s = new("pause_settings_fps")
    panel(s, 610, 190, 470, 50, None, "FPS LIMIT: 60", "", "fps limit")
    panel(s, 610, 248, 470, 50, None, "LOW FPS WHILE MATCH PAUSED: OFF", "", "low fps")

    s = new("match_paused")
    x, y, w, h = 330, 230, 620, 250
    s.poly("OVERLAYS", "dim game", [(0, 0), (1280, 0), (1280, 720), (0, 720)], (0, 0, 0, 0x99), "draw_coop_pause")
    s.poly("OVERLAYS", "card", [(x, y + 8), (x + w, y), (x + w - 4, y + h - 6), (x + 3, y + h)], INK, "draw_coop_pause", outline=[(5, INK), (1.5, EDGE)])
    s.poly("OVERLAYS", "title band", [(x + 24, y + 30), (x + w - 30, y + 22), (x + w - 34, y + 84), (x + 22, y + 90)], RED, "draw_coop_pause")
    s.text("TEXT", "title", "MATCH PAUSED", 640, y + 72, 40, PAPER, "draw_coop_pause", align="c")
    s.text("TEXT", "waiting", "WAITING FOR A PLAYER TO RECONNECT", 640, y + 138, 24, PAPER, "draw_coop_pause (dots animate in code)", align="c")
    s.text("TEXT", "players", "PLAYERS 1 / 2", 640, y + 182, 22, YELLOW, "draw_coop_pause (UI_FONT_BODY in game)", align="c")
    s.text("TEXT", "note", "The match resumes on its own when everyone is back.", 640, y + 220, 18, MUTED, "draw_coop_pause (UI_FONT_BODY in game)", align="c")

    s = new("name_editor")
    s.poly("OVERLAYS", "dim", [(0, 0), (1280, 0), (1280, 720), (0, 720)], (0, 0, 0, 0xc0), "draw_name_editor")
    s.poly("PANELS", "panel", comic(290, 230, 700, 250, 7), (0x0d, 0x0c, 0x0b, 255), "draw_name_editor", outline=[(5, INK), (1.5, EDGE)])
    strip(s, 310, 196, 420, "PLAYER NAME")
    s.poly("PANELS", "field", [(320, 320), (960, 320), (960, 390), (320, 390)], (0x23, 0x1d, 0x1b, 255), "draw_name_editor")
    s.text("TEXT", "name", "NAME_", 340, 372, 44, YELLOW, "draw_name_editor", 600, True)
    s.text("TEXT", "help", "TYPE A NAME     ENTER: SAVE     ESC: CANCEL", 640, 445, 20, SAND, "draw_name_editor", 640, False, "c")

    s = new("title")
    s.image("BACKGROUND", "title art", "branding/title-reimagined-v1.png", 0, 0, 1280, 720, "draw_title")
    s.text("TEXT", "prompt", "CLICK TO START", 640, 660, 29, PAPER, "draw_title", 900, False, "c")
    hit_area(s, "anywhere (tap 640,650)", 0, 0, 1280, 720)

    s = new("transition_curtain"); sheet(s)
    s.poly("OVERLAYS", "loading stroke", [(300, BY1 - 8), (520, BY1 - 8), (520, BY1 - 3), (300, BY1 - 3)], RED, "draw_curtain (animated in code)")
    return out


def cut_holes(s):
    """Real transparency over everything drawn by the sheet, for the native card."""
    for l in s.layers:
        if l["group"] in ("BACKGROUND", "PANELS", "DECORATIONS") and not l["name"].startswith("HIT"):
            for hx, hy, hw, hh in s.holes:
                x0, y0 = round(hx * K) - l["left"], round(hy * K) - l["top"]
                x1, y1 = x0 + round(hw * K), y0 + round(hh * K)
                a = l["image"].getchannel("A")
                ImageDraw.Draw(a).rectangle([x0, y0, x1 - 1, y1 - 1], fill=0)
                l["image"].putalpha(a)


def compose(s):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for g in GROUPS:
        if g in HIDDEN:
            continue
        for l in s.layers:
            if l["group"] == g and l["visible"] is not False:
                im.alpha_composite(l["image"], (l["left"], l["top"]))
    return im


def save_psd(s, path):
    layers = []
    for g in GROUPS:
        items = [l for l in s.layers if l["group"] == g]
        if not items:
            continue
        layers.append(Layer("</Layer group>", section=3))
        for l in items:
            layers.append(Layer(l["name"], l["image"], l["left"], l["top"], visible=g not in HIDDEN and l["visible"] is not False))
        layers.append(Layer(g, section=1, visible=g not in HIDDEN))
    guides = [(0, True), (W, True), (0, False), (H, False)]
    for hx, hy, hw, hh in s.holes:
        guides += [(hx * K, True), ((hx + hw) * K, True), (hy * K, False), ((hy + hh) * K, False)]
    for l in s.layers:
        if l["role"] == "hit":
            x, y, w, h = l["rect"]
            guides += [(x * K, True), ((x + w) * K, True), (y * K, False), ((y + h) * K, False)]
    write_psd(path, W, H, layers, compose(s), sorted(set(guides)))


def rgb(c):
    return f"rgb({c[0]},{c[1]},{c[2]})", (c[3] / 255 if len(c) > 3 else 1)


def save_svg(s, path, layer_dir):
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{W}" height="{H}" viewBox="0 0 1280 720">',
         '<style>text{font-family:"comic","Comic Book",sans-serif}</style>']
    for g in GROUPS:
        items = [l for l in s.layers if l["group"] == g]
        if not items:
            continue
        o.append(f'<g id="{g}"' + (' display="none"' if g in HIDDEN else "") + ">")
        for i, l in enumerate(items):
            sv = l["svg"]; name = escape(l["name"], quote=True); k = sv.get("kind")
            if k == "text":
                f, a = rgb(sv["fill"])
                stroke = f' stroke="rgb(17,17,17)" stroke-width="{sv["size"] * 0.12:.2f}" paint-order="stroke"' if sv["outline"] else ""
                o.append(f'<text id="{name}" x="{sv["x"]:.2f}" y="{sv["y"]:.2f}" font-size="{sv["size"]:.2f}" fill="{f}" fill-opacity="{a:.3f}"{stroke}>{escape(sv["text"])}</text>')
            elif k == "polygon" and not s.holes:
                pts = " ".join(f"{x:.2f},{y:.2f}" for x, y in sv["pts"])
                f, a = rgb(sv["fill"]) if sv["fill"] else ("none", 1)
                st = ""
                if sv["outline"]:
                    w_, c_ = sv["outline"][0]
                    st = f' stroke="{rgb(c_)[0]}" stroke-width="{w_}"'
                o.append(f'<polygon id="{name}" points="{pts}" fill="{f}" fill-opacity="{a:.3f}"{st}/>')
            else:
                rel = os.path.basename(layer_dir) + "/" + layer_file(l)
                o.append(f'<image id="{name}" x="{l["left"] / K:.2f}" y="{l["top"] / K:.2f}" width="{l["image"].width / K:.2f}" height="{l["image"].height / K:.2f}" xlink:href="{escape(rel)}"/>')
        o.append("</g>")
    o.append("</svg>")
    open(path, "w").write("\n".join(o))


def layer_file(l):
    safe = "".join(c if c.isalnum() else "_" for c in l["group"] + "__" + l["name"])
    return safe + ".png"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", required=True)
    ap.add_argument("--out", default="assets/reimagined/psd")
    ap.add_argument("--refs", help="folder with <screen>.png game captures, kept as hidden reference layers")
    a = ap.parse_args()
    art = Art(a.assets)
    styles = json.load(open(os.path.join(a.assets, "menus-style/screens.json")))["screens"]
    items = json.load(open(os.path.join(a.assets, "main-menu/layout.json")))["items"]
    for d in ("psd", "svg", "preview", "layers"):
        os.makedirs(os.path.join(a.out, d), exist_ok=True)
    screens = build(art, styles, items)
    mapping = {"scale": K, "logical": [1280, 720], "psd": [W, H], "screens": {}}
    for n, s in screens.items():
        ref = a.refs and os.path.join(a.refs, n + ".png")
        if ref and os.path.exists(ref):
            s.add("NATIVE_REFERENCE", "game capture (reference only)", Image.open(ref).convert("RGBA").resize((W, H), Image.LANCZOS), "captured from the running game", role="reference")
        cut_holes(s)
        save_psd(s, os.path.join(a.out, "psd", n + ".psd"))
        compose(s).save(os.path.join(a.out, "preview", n + ".png"))
        ld = os.path.join(a.out, "layers", n)
        os.makedirs(ld, exist_ok=True)
        for l in s.layers:
            l["image"].save(os.path.join(ld, layer_file(l)))
        os.makedirs(os.path.join(a.out, "svg", n), exist_ok=True)
        svg_layers = os.path.join(a.out, "svg", n, "layers")
        os.makedirs(svg_layers, exist_ok=True)
        for l in s.layers:
            l["image"].save(os.path.join(svg_layers, layer_file(l)))
        save_svg(s, os.path.join(a.out, "svg", n, n + ".svg"), svg_layers)
        mapping["screens"][n] = {"holes": s.holes, "layers": [
            {"group": l["group"], "name": l["name"], "source": l["source"], "role": l["role"], "psd_rect": [l["left"], l["top"], l["image"].width, l["image"].height],
             "logical_rect": l["rect"], "text": l["svg"].get("text")} for l in s.layers]}
        print(n, len(s.layers), "layers")
    json.dump(mapping, open(os.path.join(a.out, "layers.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
