#!/usr/bin/env python3
"""Copia apenas os assets usados no menu Android; não altera o kit de edição nem o OBB."""
from pathlib import Path
import hashlib
import json
import shutil

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SOURCES = {
    "nikolai.png": "assets/246_char_select_nik.png",
    "takeo.png": "assets/245_char_select_tak.png",
    "richtofen.png": "assets/237_char_select_dr.png",
    "dempsey.png": "assets/244_char_select_tan.png",
    "kino.png": "assets/182_setup_kino_art.png",
    "deadops.png": "assets/184_setup_deadops_art.png",
    "ascension.png": "assets/183_setup_ascension_art.png",
    "cotd.png": "assets/185_setup_callofthedead_art.png",
    "background.png": "previews/229_kino_background.png",
    "arcade-background.png": "previews/230_doa_bkg.png",
    "ascension-background.png": "previews/231_ascension_bkg.png",
    "cotd-background.png": "previews/232_callofthedead_bkg.png",
    "paper.png": "previews/210_bgnd_single_player.png",
    "title.png": "assets/025_select_character_title_bgnd.png",
    "back.png": "assets/028_img_butt_back.png",
    "start.png": "assets/212_img_butt_start.png",
    "tick.png": "assets/225_select_character_tick.png",
}

def main():
    sources = {name: ROOT / "menus-edit/work" / path for name, path in SOURCES.items()}
    sources["carbon.ttf"] = Path.home() / ".local/share/fonts/104_Carbon Bold.ttf"
    sources["comic.ttf"] = Path.home() / ".local/share/fonts/21_CCAdamKubert.ttf"
    for path in sources.values():
        if not path.is_file():
            raise SystemExit(f"Asset necessário não encontrado: {path}")
    target = HERE / "assets"
    target.mkdir(exist_ok=True)
    manifest = {}
    for name, path in sources.items():
        shutil.copyfile(path, target / name)
        manifest[name] = {"source": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    (HERE / "sources.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{len(manifest)} assets copiados para {target}")

if __name__ == "__main__":
    main()
