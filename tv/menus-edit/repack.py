#!/usr/bin/env python3
"""Recompila PNGs em um novo OBB validado. Nunca instala nem altera o OBB de origem."""
import sys
sys.dont_write_bytecode = True
import argparse
import json
from pathlib import Path
import os
import tempfile

from kit import (DEFAULT_WORK, GROUP, SWF, cli_error, dz, file_sha, image_pixels,
                 iwgroup, load_project, read_archive, safe_path, sha,
                 swf_bitmap_tags, texture_info, textures, write_json)


def build(work=DEFAULT_WORK, require_identical=False):
    work, manifest, base = load_project(work)
    layout_path = work / "layout.json"
    layout = json.loads(layout_path.read_text()) if layout_path.exists() else {}
    if layout == {"title_logo": False}:
        layout = {}
    if layout not in ({}, {"title_logo": True}):
        raise ValueError("Layout não suportado; use title_logo: true/false")
    if require_identical and layout:
        raise ValueError("Ida e volta exige layout original (sem layout.json)")
    entries, dirs, paths = read_archive(base)
    tags = swf_bitmap_tags(entries[paths[SWF]][2])
    declared_tags = [asset["flash"]["swf_tag"] for asset in manifest["assets"]
                     if "swf_tag" in asset["flash"]]
    if tags != declared_tags:
        raise ValueError("Tags bitmap do SWF diferem do manifesto")
    if layout:
        from title_logo import patch_title
        entry = entries[paths[SWF]]
        entries[paths[SWF]] = (entry[0], entry[1], patch_title(entry[2]), entry[3])
    before = entries[paths[GROUP]][2]
    group = iwgroup.Group.load(before)
    li, ts = textures(group)
    assets = manifest["assets"]
    if len(ts) != len(assets):
        raise ValueError("Quantidade de recursos alterada")
    expected_pngs = {a["png"] for a in assets}
    actual_pngs = {p.relative_to(work).as_posix() for folder in ("assets", "previews")
                   for p in (work / folder).rglob("*") if p.suffix.lower() == ".png"}
    if actual_pngs != expected_pngs:
        raise ValueError("PNGs ausentes ou extras: " + str(sorted(actual_pngs ^ expected_pngs)))
    changed = []
    reconstructed = 0
    for i, (a, obj) in enumerate(zip(assets, ts)):
        original = obj["data"]
        info = texture_info(original)
        if (a["index"], a["list_index"], a["name_hash"], a["resource_sha256"]) != (i, li, f"{obj['name']:08x}", sha(original)):
            raise ValueError("Manifesto não corresponde ao recurso " + str(i))
        start, size = info["pixel_offset"], info["pixel_size"]
        original_pixels = original[start:start + size] if info["editable"] else None
        pixels = image_pixels(safe_path(work, a["png"]), info, original_pixels)
        if not info["editable"]:
            if sha(pixels) != a["pixels_sha256"]:
                raise ValueError(f"{a['png']}: ETC é somente prévia; edição recusada")
            continue
        start, size = info["pixel_offset"], info["pixel_size"]
        if len(pixels) != size:
            raise ValueError("Tamanho de pixels inválido")
        # Reconstruir SEMPRE, inclusive se o hash dos pixels não mudou.
        obj["data"] = original[:start] + pixels + original[start + size:]
        reconstructed += 1
        if obj["data"] != original:
            changed.append(dict(index=i, name=a["name"], png=a["png"],
                                before_sha256=sha(original), after_sha256=sha(obj["data"])))
    after = group.dump()
    if len(after) != len(before):
        raise ValueError("O grupo mudou de tamanho")
    if require_identical and after != before:
        raise ValueError("Teste de ida e volta exige PNGs sem alterações")
    old_group = iwgroup.Group.load(before)
    new_group = iwgroup.Group.load(after)
    change_ids = {a["index"] for a in changed}
    for list_index, (ol, nl) in enumerate(zip(old_group.lists(), new_group.lists())):
        for oi, (old, new) in enumerate(zip(ol.objects, nl.objects)):
            if list_index != li or oi not in change_ids:
                if old != new:
                    raise ValueError("Recurso não autorizado foi alterado")
    entry = entries[paths[GROUP]]
    entries[paths[GROUP]] = (entry[0], entry[1], after, entry[3])
    builds = work / "builds"
    builds.mkdir(exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".repack-", suffix=".dz", dir=builds)
    os.close(fd)
    try:
        with base.open("rb") as source:
            version = source.read(9)[8]
        dz.write(tmp, entries, version=version, dirs=dirs)
        # Reabrir o arquivo produzido, sem confiar somente no serializador.
        check, check_dirs, _ = read_archive(tmp)
        if check != entries or check_dirs != dirs:
            raise ValueError("OBB reaberto difere do conteúdo pretendido")
        # O OBB tem ~503 MiB. Liberar cópias de recursos antes da comparação
        # integral evita manter vários gigabytes vivos em máquinas pequenas.
        del check, entries, group, old_group, new_group, ts
        # Para esta base, offsets, tabela e todos os bytes fora do grupo devem ser iguais.
        old_data, files, chunks, _, _ = dz.read(str(base))
        allowed = []
        for resource in ([GROUP, SWF] if layout else [GROUP]):
            chunk_ids = files[paths[resource]][2]
            if len(chunk_ids) != 1:
                raise ValueError("Frontend deve ocupar blocos contíguos")
            offset, length, *_ = chunks[chunk_ids[0]]
            allowed.append((offset, offset + length))
        new_data = Path(tmp).read_bytes()
        if len(old_data) != len(new_data):
            raise ValueError("OBB mudou de tamanho")
        previous = 0
        for start, end in sorted(allowed):
            if old_data[previous:start] != new_data[previous:start]:
                raise ValueError("OBB mudou fora dos recursos frontend autorizados")
            previous = end
        if old_data[previous:] != new_data[previous:]:
            raise ValueError("OBB mudou fora dos recursos frontend autorizados")
        identical = old_data == new_data
        digest = sha(new_data)
        if not changed and not layout and not identical:
            raise ValueError("Sem edição, OBB deveria ser byte-idêntico")
        if require_identical and not identical:
            raise ValueError("Ida e volta não foi byte-idêntica")
        out_dir = builds / digest
        out_dir.mkdir(exist_ok=True)
        out = out_dir / "blackops_etc.dz"
        if out.exists():
            if file_sha(out) != digest:
                raise ValueError("Build existente corrompida: " + str(out))
        else:
            os.replace(tmp, out)
        report = dict(schema=1, source_sha256=manifest["source"]["sha256"],
                      output_sha256=digest, output=str(out), bytes=len(new_data),
                      byte_identical=identical, group_identical=before == after,
                      unchanged_outside_frontend=True, reconstructed_pngs=reconstructed,
                      preserved_compressed_resources=len(assets) - reconstructed,
                      changed=changed, layout=layout, runtime_tested=False,
                      validation="PNGs reconstruídos; grupo e DZ reabertos; comparação de bytes concluída.")
        write_json(out_dir / "report.json", report)
        write_json(work / "last-build.json", report)
        if require_identical:
            write_json(work / "roundtrip.json", report)
        print(f"Build validada: {len(changed)} imagens alteradas; {reconstructed} PNGs reconstruídos.")
        print("OBB byte-idêntico à base:", identical)
        print("SHA256:", digest)
        print(out)
        return out, report
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--work", type=Path, default=DEFAULT_WORK)
    ap.add_argument("--roundtrip", action="store_true", help="Exige OBB byte-idêntico à base")
    args = ap.parse_args()
    build(args.work, args.roundtrip)


if __name__ == "__main__":
    cli_error(main)
