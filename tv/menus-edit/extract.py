#!/usr/bin/env python3
"""Extrai as artes frontend e o vínculo com o SWF, sem tocar na TV."""
import sys
sys.dont_write_bytecode = True
import argparse
import collections
import html
from pathlib import Path
import re
import shutil
import subprocess

from kit import (HERE, PORT, DEFAULT_WORK, GROUP, SWF, Image, atomic_write, cli_error,
                 decode_texture, file_sha, flash_map, iwgroup, read_archive, sha,
                 swf_bitmap_tags, texture_info, texture_names, textures, write_json)


def catalog(work, assets):
    from PIL import ImageDraw
    links = []
    sheets = work / "catalog"
    sheets.mkdir()
    for first in range(0, len(assets), 40):
        sheet = Image.new("RGB", (1000, 8 * 170), "#292b30")
        draw = ImageDraw.Draw(sheet)
        for j, a in enumerate(assets[first:first + 40]):
            x, y = (j % 5) * 200, (j // 5) * 170
            im = Image.open(work / a["png"]).convert("RGBA")
            im.thumbnail((188, 125))
            bg = Image.new("RGB", im.size, "#777777")
            bg.paste(im, mask=im.getchannel("A"))
            sheet.paste(bg, (x + (200 - im.width) // 2, y + 3))
            label = f"{a['index']:03d} {'EDITAR' if a['editable'] else 'PREVIA ETC'}"
            draw.text((x + 5, y + 130), label, fill="white")
            draw.text((x + 5, y + 146), a["name"][:28], fill="#cccccc")
        sheet.save(sheets / f"{first:03d}-{min(first + 39, len(assets)-1):03d}.jpg", quality=90)
    for a in assets:
        png = html.escape(a["png"], quote=True)
        symbols = ", ".join(a["flash"].get("symbols", [])) or "Sem símbolo exportado"
        roots = ", ".join(a["flash"].get("root_instances", [])) or "Uso não localizado"
        links.append(f'<article><a href="{png}"><img loading="lazy" src="{png}"></a>'
                     f'<b>{a["index"]:03d} — {html.escape(a["name"])}</b>'
                     f'<p>{a["width"]}×{a["height"]} · {a["format"]} · '
                     f'{"EDITÁVEL" if a["editable"] else "SOMENTE PRÉVIA"}</p>'
                     f'<p>{html.escape(a["description"])}</p><small>Tela/instância SWF: '
                     f'{html.escape(roots)}<br>Símbolos: {html.escape(symbols)}</small></article>')
    atomic_write(work / "catalogo.html", ('''<!doctype html><html lang="pt-BR"><meta charset="utf-8">
<title>Artes dos menus — COD BOZ</title><style>
body{background:#18191c;color:#eee;font:16px system-ui;margin:28px}h1{font-size:25px}
main{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:18px}
article{background:#282a30;padding:16px;border-radius:8px;overflow-wrap:anywhere}
img{width:100%;height:180px;object-fit:contain;background:repeating-conic-gradient(#666 0 25%,#888 0 50%) 0/16px 16px}
b{display:block;margin-top:12px}p,small{font-size:13px}input{padding:10px;width:320px;margin:15px 0}
</style><h1>Artes dos menus</h1><p>Edite os arquivos de assets/. previews/ contém ETC bloqueado.
Os nomes de tela são referências estáticas; alguns elementos são compartilhados ou ocultos.</p>
<input placeholder="Filtrar nome, índice, tela..." aria-label="Filtrar" oninput="for(const a of document.querySelectorAll('article'))a.hidden=!a.textContent.toLowerCase().includes(this.value.toLowerCase())">
<main>''' + "\n".join(links) + "</main></html>").encode())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--obb", type=Path, default=PORT / "obb_moon/blackops_etc.dz")
    ap.add_argument("--work", type=Path, default=DEFAULT_WORK)
    ap.add_argument("--ffdec", type=Path, default=Path.home() / "repos/ext/ffdec/ffdec.jar")
    args = ap.parse_args()
    work, source = args.work.resolve(), args.obb.resolve()
    if work.exists():
        raise ValueError(f"{work} já existe. Use --work com outra pasta para preservar suas edições.")
    if not args.ffdec.is_file():
        raise ValueError("FFDec não encontrado: " + str(args.ffdec))
    entries, dirs, paths = read_archive(source)
    raw = entries[paths[GROUP]][2]
    group = iwgroup.Group.load(raw)
    if group.dump() != raw:
        raise ValueError("Grupo não regrava idêntico")
    names = texture_names(group)
    li, ts = textures(group)
    infos = [texture_info(o["data"]) for o in ts]
    for i, name in enumerate(names):
        if iwgroup.djb2(name) != ts[i]["name"]:
            raise ValueError("Ordem de nomes/texturas divergente")
    work.mkdir(parents=True)
    base = work / ".base"
    base.mkdir()
    (work / "assets").mkdir()
    (work / "previews").mkdir()
    print("Preservando OBB base e exportando metadados do SWF...", flush=True)
    snapshot = base / "blackops_etc.dz"
    shutil.copyfile(source, snapshot)
    # Confirmar que a cópia contém exatamente os bytes que acabamos de analisar.
    copied, copied_dirs, _ = read_archive(snapshot)
    if copied != entries or copied_dirs != dirs:
        raise ValueError("OBB mudou durante extração; descarte a pasta incompleta")
    del copied
    swf = base / "frontend.swf"
    swf.write_bytes(entries[paths[SWF]][2])
    xml = base / "frontend.xml"
    java_home = base / "ffdec-home"
    java_home.mkdir()
    cmd = ["java", "-Djava.awt.headless=true", "-Duser.home=" + str(java_home),
           "-Xmx1024m", "-jar", str(args.ffdec.resolve()), "-swf2xml", str(swf), str(xml)]
    with open(base / "ffdec.log", "wb") as log:
        r = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    if r.returncode or not xml.exists():
        raise ValueError("FFDec falhou; consulte " + str(base / "ffdec.log"))
    links = flash_map(xml)
    if len(links) != len(names):
        raise ValueError("Quantidade de bitmaps SWF diverge da tabela externa")
    bitmap_tags = swf_bitmap_tags(entries[paths[SWF]][2])
    if len(bitmap_tags) != len(links):
        raise ValueError("Quantidade de tags de bitmap SWF diverge da tabela externa")
    for link, tag in zip(links, bitmap_tags):
        if link["bitmap_id"] != tag["bitmap_id"]:
            raise ValueError("Ordem de bitmap SWF diverge da tabela externa")
        link["swf_tag"] = tag
    assets = []
    for i, (obj, info) in enumerate(zip(ts, infos)):
        name = names[i] if i < len(names) else f"aux_{obj['name']:08x}"
        slug = re.sub(r"[^A-Za-z0-9_-]+", "_", name).strip("_") or "imagem"
        png = f"{'assets' if info['editable'] else 'previews'}/{i:03d}_{slug}.png"
        im = decode_texture(obj["data"])
        im.save(work / png)
        desc = name.replace("_", " ")
        if name == "img_butt_start":
            desc = "Arte do botão Start; compartilhada com continuação multiplayer. Legenda separada. Primeiro teste sugerido."
        elif name == "main_menu_logo":
            desc = "Logo do menu principal. Conferir reconhecimento da tela após editar."
        flash = links[i] if i < len(links) else {"bitmap_id": None, "symbols": [], "root_instances": [], "evidence": "Textura auxiliar sem bitmap SWF 2x1 correspondente."}
        assets.append(dict(index=i, list_index=li, name=name, name_hash=f"{obj['name']:08x}",
                           png=png, description=desc, **info, flash=flash,
                           resource_sha256=sha(obj["data"]), pixels_sha256=sha(im.tobytes()),
                           recognition="Conferir sondas se alterar título/logo/fundo; recaptura pode exigir APK."))
        if (i + 1) % 40 == 0:
            print(f"{i + 1}/{len(ts)} imagens...", flush=True)
    manifest = dict(schema=1, source=dict(original=str(source), sha256=file_sha(snapshot),
                    bytes=snapshot.stat().st_size), group=GROUP, swf=SWF,
                    group_sha256=sha(raw), swf_sha256=sha(entries[paths[SWF]][2]),
                    constraints=dict(fixed_dimensions=True, fixed_resource_order=True,
                                     etc_preview_only=True, png_bits_per_channel=8,
                                     swf_bitmaps_are_atlas_placeholders=True), assets=assets)
    write_json(work / "manifest.json", manifest)
    atomic_write(base / "manifest.sha256", (file_sha(work / "manifest.json") + "\n").encode())
    catalog(work, assets)
    print("Extração concluída:", work)
    print("Formatos:", dict(collections.Counter(a["format"] for a in assets)))
    print("Abra", work / "catalogo.html")


if __name__ == "__main__":
    cli_error(main)
