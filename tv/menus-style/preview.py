#!/usr/bin/env python3
"""Prévia aproximada do StyledScreen sobre os prints do SWF (menus/shots), para conferir janelas e tarjas.

Não é o render do Android: painéis sem inclinação e papel liso. Contorno verde = área de foco do item.
Uso: preview.py  ->  menus-style/previews/<tela>.png
"""
import json
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent
PAPER, INK, RED, DIM, CAPTION = "#f2eee4", "#151211", "#791d24", "#191614", "#d5c9ac"
SHOTS = {"single": "14_single", "newgame": "15_newgame", "quitconfirm": "17_quitconfirm",
         "savequit": "22_savequit", "pause": "18_pause", "gameover": "19_gameover",
         "settings": "06_settings", "pausesettings": "20_pausesettings", "sensitivity": "21_sensitivity",
         "controls": "05_controls", "help": "08_help", "coop": "13_coop", "extras": "04_extras"}


def font(size):
    return ImageFont.truetype(str(BUILD / "character-menu/assets/comic.ttf"), size)


def fit(d, text, size, width):
    f = font(size)
    w = d.textlength(text, font=f)
    return font(max(8, int(size * width / w))) if w > width else f


def box(r):
    return [r[0], r[1], r[0] + r[2], r[1] + r[3]]


def main():
    screens = json.loads((HERE / "assets/screens.json").read_text())["screens"]
    with tempfile.TemporaryDirectory() as tmp:
        generated = Path(tmp) / "boz_menus.json"
        subprocess.run(["python3", "-I", str(BUILD / "menus/menus.py"), str(generated)],
                       check=True, capture_output=True)
        menus = {s["id"]: s for s in json.loads(generated.read_text())["screens"]}
    out = HERE / "previews"
    out.mkdir(exist_ok=True)
    for sid, st in screens.items():
        img = Image.open(BUILD / f"menus/shots/{SHOTS[sid]}.png").convert("RGB")
        d = ImageDraw.Draw(img)
        if st["kind"] == "page":
            layer = Image.new("RGB", img.size, PAPER)
            ld = ImageDraw.Draw(layer)
            ld.rectangle([48, 134, 1232, 674], fill=INK)
            ld.rectangle([64, 38, 1216, 134], fill=INK)
            mask = Image.new("L", img.size, 255)
            for w in st.get("windows", []):
                ImageDraw.Draw(mask).rectangle(box(w), fill=0)
            img.paste(layer, (0, 0), mask)
            d.text((86, 80), "BLACK OPS ZOMBIES", font=font(30), fill=PAPER, anchor="ls")
            d.text((85, 119), "REIMAGINED", font=font(42), fill=PAPER, anchor="ls")
            d.text((1194, 100), st["title"], font=font(33), fill=PAPER, anchor="rs")
            for w in st.get("windows", []):
                d.rectangle(box(w), outline=INK, width=6)
            for i, line in enumerate(st.get("message", [])):
                d.text((640, 260 + i * 56), line, font=fit(d, line, 40, 1080), fill=PAPER, anchor="ms",
                       stroke_width=4, stroke_fill=INK)
            for b in st.get("buttons", []):
                x, y, w, h = b["bounds"]
                d.rectangle([x, y, x + w, y + h], fill=RED if b.get("primary") and "art" not in b else DIM,
                            outline=INK, width=5)
                if "art" in b:
                    art = Image.open(BUILD / b["art"].replace("character-menu/", "character-menu/assets/")).convert("RGB")
                    scale = max(w / art.width, (h - 100) / art.height)
                    art = art.resize((int(art.width * scale), int(art.height * scale)))
                    left, top = (art.width - w) // 2, (art.height - (h - 100)) // 2
                    img.paste(art.crop((left, top, left + w, top + h - 100)), (x, y))
                if b.get("caption"):
                    top = y + h - 112
                    d.rectangle([x, top, x + w, y + h], fill=RED if b.get("primary") else INK)
                    d.text((x + 16, top + 44), b["title"], font=fit(d, b["title"], 38, w - 32), fill=PAPER, anchor="ls")
                    d.text((x + 16, top + 85), b["caption"], font=fit(d, b["caption"], 19, w - 32), fill=CAPTION, anchor="ls")
                else:
                    size = min(31, int(h * 0.42))
                    d.text((x + 22, y + h / 2 + size * 0.36), b["title"], font=fit(d, b["title"], size, w - 44),
                           fill=PAPER, anchor="ls")
            hint = st.get("hint", "D-PAD: NAVIGATE     A: SELECT     B: BACK")
            d.text((640, 698), hint, font=fit(d, hint, 20, 780), fill=INK, anchor="ms")
        else:
            x, y, w, h = st["tag"]
            d.rectangle([x, y, x + w, y + h], fill=INK, outline="#827763", width=2)
            size = min(44, int(h * 0.62))
            d.text((x + 18, y + h / 2 + size * 0.36), st["title"], font=fit(d, st["title"], size, w - 36),
                   fill=PAPER, anchor="ls")
            d.rectangle([380, 684, 900, 714], fill=INK)
            hint = st.get("hint", "D-PAD: NAVIGATE     A: SELECT     B: BACK")
            d.text((640, 706), hint, font=fit(d, hint, 17, 500), fill=PAPER, anchor="ms")
        for it in menus.get(sid, {}).get("items", []):
            if it[0] == "b":
                d.rectangle(box(it[1:5]), outline="#2fd36b", width=2)
        img.save(out / f"{sid}.png")
        print(out / f"{sid}.png")


if __name__ == "__main__":
    main()
