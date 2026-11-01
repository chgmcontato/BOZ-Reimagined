"""Primitivas do kit de menus. Não modifica ferramentas compartilhadas do port."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
PORT = HERE.parent
sys.path.insert(0, str(PORT / "maps/tools"))
import dz
import iwgroup
from PIL import Image

GROUP = "frontend/frontend.group.bin"
SWF = "frontend/fe_v4_cs4_swf.bin"
DEFAULT_WORK = HERE / "tv-current"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".writing-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def write_json(path, data):
    atomic_write(path, (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode())


def safe_path(root, relative):
    p = (Path(root) / relative).resolve()
    if Path(relative).is_absolute() or not p.is_relative_to(Path(root).resolve()):
        raise ValueError("Caminho fora do projeto: " + str(relative))
    return p


def entry_path(e):
    return (e[0] + "/" + e[1]).replace("\\", "/").lstrip("/")


def read_archive(path):
    entries, dirs = dz.entries_of(str(path))
    paths = [entry_path(e) for e in entries]
    if len(paths) != len(set(paths)):
        raise ValueError("OBB com caminhos repetidos não suportado")
    if GROUP not in paths or SWF not in paths:
        raise ValueError("OBB não contém os recursos frontend esperados")
    return entries, dirs, dict(zip(paths, range(len(paths))))


def textures(group):
    lists = [(li, lst) for li, lst in enumerate(group.lists())
             if lst.cls == iwgroup.djb2("CIwTexture")]
    if len(lists) != 1:
        raise ValueError("Esperada exatamente uma lista CIwTexture")
    return lists[0][0], lists[0][1].objects


def texture_names(group):
    objs = [o for lst in group.lists() for o in lst.objects
            if o["cls"] == iwgroup.djb2("CIsFlashTextureResource")]
    if len(objs) != 1:
        raise ValueError("Tabela de nomes Flash ausente/ambígua")
    b = objs[0]["data"]
    count, = struct.unpack_from("<I", b)
    parts = b[4:].split(b"\0")
    if len(parts) != count + 1 or parts[-1]:
        raise ValueError("Tabela de nomes Flash fora do perfil conhecido")
    return [v.decode("latin1") for v in parts[:-1]]


def texture_info(raw):
    if len(raw) < 24:
        raise ValueError("Textura truncada")
    fmt = raw[10]
    flags, w, h, pitch, stored_size = struct.unpack_from("<HHHHI", raw, 11)
    if not w or not h:
        raise ValueError("Textura vazia")
    if fmt in (14, 10):
        mode = "RGBA" if fmt == 14 else "RGB"
        if pitch != w * len(mode) or len(raw) != 24 + pitch * h or raw[-1] != 0:
            raise ValueError("Textura não comprimida fora do perfil validado")
        if stored_size not in (0, pitch * h):
            raise ValueError("Tamanho de pixels inconsistente")
        offset, size = 23, pitch * h
        name = "RGBA8888" if fmt == 14 else "RGB888"
    elif fmt == 35:
        # Envelope Marmalade: 23 B CIwTexture/CIwImage + 72 B ETC.
        subtype, version, total, glfmt, levels, cw, ch, size = struct.unpack_from("<HHIIIIII", raw, 23)
        if (subtype, version, glfmt, levels, cw, ch) != (39, 2, 0x8D64, 1, w, h):
            raise ValueError("Envelope ETC não suportado")
        if size != ((w + 3) // 4) * ((h + 3) // 4) * 8:
            raise ValueError("Tamanho ETC1 inválido")
        if total != size + 72 or stored_size != total or len(raw) != size + 96 or raw[-1] != 0:
            raise ValueError("Envelope ETC1 truncado/inconsistente")
        offset, mode, name = 95, "RGB", "ETC1_RGB8"
    else:
        raise ValueError("Formato não suportado: %#x" % fmt)
    return dict(format=name, format_id=fmt, mode=mode, width=w, height=h,
                pitch=pitch, image_flags=flags, pixel_offset=offset, pixel_size=size,
                editable=fmt in (14, 10))


def decode_etc1(data, w, h):
    """Decodificação vetorizada do bit layout ETC1, sem recompressão.

    Referência de formato: AOSP opengl/libs/ETC1/etc1.cpp e extensão Khronos
    OES_compressed_ETC1_RGB8_texture. Implementação por arrays NumPy.
    """
    import numpy as np
    blocks = np.frombuffer(data, dtype=">u4").reshape(-1, 2).astype(np.uint32)
    high, low = blocks[:, 0], blocks[:, 1]
    diff = (high & 2) != 0
    base = np.empty((len(blocks), 2, 3), dtype=np.int16)
    for c, shift in enumerate((24, 16, 8)):
        byte = (high >> shift) & 255
        a = (byte >> 3).astype(np.int16)
        delta = (byte & 7).astype(np.int16)
        delta = np.where(delta >= 4, delta - 8, delta)
        b = a + delta
        if np.any(diff & ((b < 0) | (b > 31))):
            raise ValueError("Bloco fora do ETC1 (delta inválido)")
        base[:, 0, c] = np.where(diff, (a << 3) | (a >> 2), (byte >> 4) * 17)
        base[:, 1, c] = np.where(diff, (b << 3) | (b >> 2), (byte & 15) * 17)
    mods = np.array(((2, 8, -2, -8), (5, 17, -5, -17), (9, 29, -9, -29),
                     (13, 42, -13, -42), (18, 60, -18, -60), (24, 80, -24, -80),
                     (33, 106, -33, -106), (47, 183, -47, -183)), dtype=np.int16)
    result = np.empty((len(blocks), 4, 4, 3), dtype=np.uint8)
    rows = np.arange(len(blocks))
    for y in range(4):
        for x in range(4):
            sub = np.where(high & 1, y // 2, x // 2)
            table = np.where(sub == 0, (high >> 5) & 7, (high >> 2) & 7)
            bit = x * 4 + y
            index = ((low >> bit) & 1) | (((low >> (bit + 16)) & 1) << 1)
            result[:, y, x, :] = np.clip(base[rows, sub] + mods[table, index, None], 0, 255)
    bh, bw = (h + 3) // 4, (w + 3) // 4
    pixels = result.reshape(bh, bw, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 3)
    return Image.fromarray(pixels[:h, :w])


def decode_texture(raw):
    info = texture_info(raw)
    p, size = info["pixel_offset"], info["pixel_size"]
    if info["editable"]:
        return Image.frombytes(info["mode"], (info["width"], info["height"]), raw[p:p + size])
    return decode_etc1(raw[p:p + size], info["width"], info["height"])


def image_pixels(path, info, original_pixels=None):
    # Não aplicar perfil de cor, EXIF, gamma, premultiplicação ou redimensionamento.
    with Image.open(path) as im:
        if im.format != "PNG" or im.size != (info["width"], info["height"]):
            raise ValueError(f"{path}: exige PNG {info['width']}x{info['height']}")
        if im.mode != info["mode"] or getattr(im, "n_frames", 1) != 1:
            raise ValueError(f"{path}: exige {info['mode']}, 8 bits/canal, sem paleta/animação")
        # Pillow reduz silenciosamente PNG RGB de 16 bits: conferir o IHDR.
        header = Path(path).read_bytes()[:29]
        expected_color = 6 if info["mode"] == "RGBA" else 2
        if header[:8] != b"\x89PNG\r\n\x1a\n" or header[24:26] != bytes((8, expected_color)):
            raise ValueError(f"{path}: profundidade/canais PNG inválidos")
        # Exported assets carry no colour profile. Reject tagged exports rather
        # than silently changing their appearance during raw-channel reinjection.
        if any(k in im.info for k in ("icc_profile", "gamma", "chromaticity", "srgb")):
            raise ValueError(f"{path}: exporte sem perfil ICC/gamma/sRGB embutido; edite em sRGB")
        pixels = im.tobytes()
        if original_pixels is not None and info["mode"] == "RGBA":
            if pixels[3::4] != original_pixels[3::4]:
                raise ValueError(f"{path}: máscara alfa alterada; preserve a transparência original")
        return pixels


def flash_map(xml_path):
    tags = ET.parse(xml_path).getroot().find("tags")
    bitmap_ids, definitions, parents, exports = [], {}, {}, {}
    for t in tags:
        typ = t.get("type", "")
        if typ in ("DefineBitsLosslessTag", "DefineBitsLossless2Tag"):
            if (t.get("bitmapWidth"), t.get("bitmapHeight")) != ("2", "1"):
                raise ValueError("SWF fora do perfil de bitmaps substitutos 2x1")
            bitmap_ids.append(int(t.get("characterID")))
        if typ.startswith("Define"):
            for key in ("characterID", "shapeId", "spriteId", "buttonId"):
                if key in t.attrib:
                    definitions[int(t.get(key))] = t
                    break
        if typ == "ExportAssetsTag":
            for cid, name in zip(t.findall("./tags/item"), t.findall("./names/item")):
                exports.setdefault(int(cid.text), []).append(name.text)
    for cid, t in definitions.items():
        for e in t.iter():
            if e is t:
                continue
            for key in ("bitmapId", "characterId", "characterID"):
                if key in e.attrib:
                    parents.setdefault(int(e.get(key)), set()).add(cid)
    # Rótulos/instâncias principais são evidência estática, não prova de visibilidade.
    stage = {}
    label = "title"
    for t in tags:
        if t.get("type") == "FrameLabelTag":
            label = t.get("name")
        if t.get("type", "").startswith("PlaceObject") and t.get("characterId"):
            stage.setdefault(int(t.get("characterId")), set()).add(t.get("name") or label)
    out = []
    for cid in bitmap_ids:
        seen, todo = set(), {cid}
        while todo:
            seen |= todo
            todo = {p for child in todo for p in parents.get(child, ())} - seen
        out.append(dict(bitmap_id=cid, ancestor_ids=sorted(seen - {cid}),
                        symbols=sorted({n for x in seen for n in exports.get(x, [])}),
                        root_instances=sorted({n for x in seen for n in stage.get(x, [])}),
                        evidence="Referências estáticas do SWF; visibilidade depende do ActionScript."))
    return out


def swf_bitmap_tags(data):
    """Return offsets and lossless bitmap metadata for the SWF's bitmap tags."""
    if len(data) < 12 or data[:3] != b"FWS":
        raise ValueError("SWF deve ser FWS íntegro para inventário de tags")
    declared, = struct.unpack_from("<I", data, 4)
    if declared != len(data):
        raise ValueError("Tamanho declarado do SWF não confere")
    rect_bits = 5 + 4 * (data[8] >> 3)
    offset = 8 + (rect_bits + 7) // 8 + 4
    bitmaps = []
    while offset < len(data):
        tag_offset = offset
        if offset + 2 > len(data):
            raise ValueError("Cabeçalho de tag SWF truncado")
        header, = struct.unpack_from("<H", data, offset)
        offset += 2
        code, length = header >> 6, header & 63
        if length == 63:
            if offset + 4 > len(data):
                raise ValueError("Tamanho de tag SWF truncado")
            length, = struct.unpack_from("<I", data, offset)
            offset += 4
        payload = offset
        end = payload + length
        if end > len(data):
            raise ValueError("Tag SWF excede o arquivo")
        if code in (20, 36):
            if length < 3:
                raise ValueError("Tag DefineBitsLossless truncada")
            bitmap = dict(tag_code=code,
                          tag_type="DefineBitsLossless2Tag" if code == 36 else "DefineBitsLosslessTag",
                          tag_offset=tag_offset, tag_length=end - tag_offset,
                          bitmap_id=struct.unpack_from("<H", data, payload)[0],
                          bitmap_format=data[payload + 2], width=None, height=None,
                          editable_pixels=False,
                          pixel_source="frontend.group.bin texture atlas")
            if bitmap["bitmap_format"] in (3, 4, 5):
                if length < 7:
                    raise ValueError("Dimensões de bitmap SWF truncadas")
                bitmap["width"], bitmap["height"] = struct.unpack_from("<HH", data, payload + 3)
            bitmaps.append(bitmap)
        offset = end
        if code == 0:
            break
    if offset != len(data):
        raise ValueError("Dados inesperados após o fim do SWF")
    return bitmaps


def load_project(work):
    work = Path(work).resolve()
    manifest_path = work / "manifest.json"
    expected = (work / ".base/manifest.sha256").read_text().strip()
    if file_sha(manifest_path) != expected:
        raise ValueError("Manifesto alterado. Edite apenas os PNGs; restaure manifest.json.")
    manifest = json.loads(manifest_path.read_text())
    base = work / ".base/blackops_etc.dz"
    if file_sha(base) != manifest["source"]["sha256"]:
        raise ValueError("Cópia de base modificada/corrompida")
    return work, manifest, base


def cli_error(fn):
    try:
        fn()
    except (ValueError, OSError, subprocess.SubprocessError, EOFError) as e:
        print("ERRO:", e, file=sys.stderr)
        raise SystemExit(1)
