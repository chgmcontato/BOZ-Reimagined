#!/usr/bin/env python3
"""Reads edited PSDs back: lists what changed against layers.json and exports the changed layers.

It never edits the game. Moved or resized buttons are reported, because their click area and draw
position live in src/menus.c (see README.md): art alone cannot move them.
"""
import argparse, json, os
from psd_tools import PSDImage


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("edited", help="folder with the edited .psd files")
    ap.add_argument("--base", default="assets/reimagined/psd")
    ap.add_argument("--out", default="assets/reimagined/psd/changed")
    a = ap.parse_args()
    mapping = json.load(open(os.path.join(a.base, "layers.json")))
    original_dir = os.path.join(a.base, "layers")
    report = []
    for fn in sorted(os.listdir(a.edited)):
        if not fn.endswith(".psd"):
            continue
        name = fn[:-4]
        if name not in mapping["screens"]:
            report.append(f"{fn}: not an exported screen")
            continue
        known = {l["name"]: l for l in mapping["screens"][name]["layers"]}
        seen = set()
        for layer in PSDImage.open(os.path.join(a.edited, fn)).descendants():
            if layer.is_group():
                continue
            seen.add(layer.name)
            base = known.get(layer.name)
            if base is None:
                report.append(f"{name}: NEW layer '{layer.name}': needs a decision in code")
                continue
            rect = [layer.left, layer.top, layer.width, layer.height]
            moved = rect != base["psd_rect"]
            image = layer.topil()
            safe = "".join(c if c.isalnum() else "_" for c in base["group"] + "__" + base["name"]) + ".png"
            from PIL import Image, ImageChops
            orig = Image.open(os.path.join(original_dir, name, safe)).convert("RGBA")
            changed = moved or image is None or orig.size != image.size or ImageChops.difference(orig, image.convert("RGBA")).getbbox() is not None
            if not changed:
                continue
            os.makedirs(os.path.join(a.out, name), exist_ok=True)
            if image is not None:
                image.save(os.path.join(a.out, name, safe))
            logical = [round(v / mapping["scale"], 2) for v in rect]
            line = f"{name}: '{layer.name}' changed, source {base['source']}, logical rect {logical}"
            if moved:
                line += " MOVED/RESIZED: update the draw position"
                if base["role"] == "hit" or base["group"] in ("BUTTONS", "PANELS"):
                    line += " AND the click area in src/menus.c"
            report.append(line)
        for missing in known.keys() - seen:
            report.append(f"{name}: layer '{missing}' was removed: check the code still needs it")
    print("\n".join(report) or "no changes")


if __name__ == "__main__":
    main()
