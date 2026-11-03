#!/usr/bin/env python3
"""Opens every exported PSD with psd-tools and checks layers, groups, transparency and the composite."""
import glob, json, os, sys
from PIL import Image, ImageChops
from psd_tools import PSDImage
out = sys.argv[1] if len(sys.argv) > 1 else "assets/reimagined/psd"
mapping = json.load(open(os.path.join(out, "layers.json")))
bad = 0
for p in sorted(glob.glob(os.path.join(out, "psd", "*.psd"))):
    n = os.path.basename(p)[:-4]
    psd = PSDImage.open(p)
    groups = [g.name for g in psd if g.is_group()]
    count = sum(1 for l in psd.descendants() if not l.is_group())
    want = len(mapping["screens"][n]["layers"])
    ok = psd.size == (1920, 1080) and count == want and bool(groups)
    rects = {}
    for l in psd.descendants():
        if not l.is_group():
            rects[l.name] = (l.left, l.top)
    note = ""
    for hx, hy, hw, hh in mapping["screens"][n]["holes"]:
        cx, cy = int((hx + hw / 2) * 1.5), int((hy + hh / 2) * 1.5)
        comp = Image.open(os.path.join(out, "preview", n + ".png")).getpixel((cx, cy))
        note += f" hole alpha={comp[3]}"
        ok = ok and comp[3] == 0
    bad += not ok
    refs = [l for l in psd.descendants() if l.name.startswith("game capture")]
    note += f" ref={len(refs)}"
    print(("OK " if ok else "BAD"), n, f"layers {count}/{want} groups {groups}{note}")
print("failures:", bad)
sys.exit(1 if bad else 0)
