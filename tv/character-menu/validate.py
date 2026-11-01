#!/usr/bin/env python3
"""Valida o APK final e o grafo espacial de navegação que ele realmente contém."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[2]
apk = ROOT / "BOZ-TV.apk"
with zipfile.ZipFile(apk) as z:
    assert z.testzip() is None, "APK corrompido"
    assert not any("libgadget" in n for n in z.namelist()), "Build contém Frida"
    for p in (ROOT / "build/character-menu/assets").iterdir():
        assert z.read("assets/character-menu/" + p.name) == p.read_bytes(), p.name
    menu = json.loads(z.read("assets/boz_menus.json"))
    items = next(s["items"] for s in menu["screens"] if s["id"] == "charselect")
    # Mesmo algoritmo espacial do Menus: nenhum botão pode ser uma ilha no controle.
    graph = {i: set() for i in range(len(items))}
    for i, item in enumerate(items):
        _, x, y, w, h, *_ = item
        assert x >= 64 and x + w <= 1216 and y >= 36 and y + h <= 684, item
        for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
            choices = []
            for j, b in enumerate(items):
                xx, yy = b[1] + b[3] / 2 - (x + w / 2), b[2] + b[4] / 2 - (y + h / 2)
                along = xx * dx + yy * dy
                if along > 10:
                    choices.append((along + 2.5 * abs(yy if dx else xx), j))
            if choices:
                graph[i].add(min(choices)[1])
    for start in graph:
        reached, pending = {start}, [start]
        while pending:
            for n in graph[pending.pop()] - reached:
                reached.add(n)
                pending.append(n)
        assert len(reached) == len(items), (start, reached)
print(json.dumps({"apk": str(apk), "sha256": hashlib.sha256(apk.read_bytes()).hexdigest(),
                  "reachable_buttons": len(items), "assets_verified": True, "frida": False}, indent=2))
