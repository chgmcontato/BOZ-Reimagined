#!/usr/bin/env python3
"""Testes locais de pixels, formatos, tags SWF e publicação offline."""
import sys
sys.dont_write_bytecode = True
import argparse
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch

from kit import (DEFAULT_WORK, Image, decode_etc1, image_pixels, sha,
                 swf_bitmap_tags, texture_info)
from push import publish


class FormatTests(unittest.TestCase):
    def test_etc1_individual_channels_and_selectors(self):
        # Vermelho à esquerda, verde à direita. Índice 0 soma 2; índice 3 subtrai 8.
        block = bytes.fromhex("f00f000000000000")
        image = decode_etc1(block, 4, 4)
        self.assertEqual(image.getpixel((0, 0)), (255, 2, 2))
        self.assertEqual(image.getpixel((3, 0)), (2, 255, 2))
        image = decode_etc1(bytes.fromhex("f00f0000ffffffff"), 4, 4)
        self.assertEqual(image.getpixel((0, 0)), (247, 0, 0))

    def test_etc1_differential_and_flip(self):
        # Base 16/16/16, delta +1/+1/+1, divisão horizontal, modificador +2.
        image = decode_etc1(bytes.fromhex("8181810300000000"), 4, 4)
        self.assertEqual(image.getpixel((0, 0)), (134, 134, 134))
        self.assertEqual(image.getpixel((0, 3)), (142, 142, 142))

    def test_etc1_block_order_and_crop(self):
        data = bytes.fromhex("f00f000000000000") + bytes.fromhex("00f0000000000000")
        image = decode_etc1(data, 5, 3)
        self.assertEqual(image.size, (5, 3))
        self.assertEqual(image.getpixel((4, 0)), (2, 255, 2))

    def test_rgba_hidden_rgb_roundtrip_and_rejections(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "a.png"
            pixels = bytes((31, 55, 73, 0, 200, 100, 25, 127))
            info = dict(width=2, height=1, mode="RGBA")
            Image.frombytes("RGBA", (2, 1), pixels).save(path)
            self.assertEqual(image_pixels(path, info), pixels)
            Image.new("RGBA", (3, 1)).save(path)
            with self.assertRaises(ValueError): image_pixels(path, info)
            Image.new("P", (2, 1)).save(path)
            with self.assertRaises(ValueError): image_pixels(path, info)
            Image.new("RGB", (2, 1)).save(path)
            with self.assertRaises(ValueError): image_pixels(path, info)

    def test_alpha_and_colour_profile_rejected(self):
        from PIL.PngImagePlugin import PngInfo
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "a.png"
            info = dict(width=1, height=1, mode="RGBA")
            original = bytes((10, 20, 30, 127))
            Image.frombytes("RGBA", (1, 1), original).save(path)
            self.assertEqual(image_pixels(path, info, original), original)
            Image.new("RGBA", (1, 1), (40, 50, 60, 128)).save(path)
            with self.assertRaisesRegex(ValueError, "alfa"):
                image_pixels(path, info, original)
            im = Image.frombytes("RGBA", (1, 1), original)
            im.save(path, icc_profile=b"unapproved profile")
            with self.assertRaisesRegex(ValueError, "perfil"):
                image_pixels(path, info, original)
            meta = PngInfo()
            meta.add(b"gAMA", struct.pack(">I", 45455))
            im.save(path, pnginfo=meta)
            with self.assertRaisesRegex(ValueError, "perfil"):
                image_pixels(path, info, original)

    def test_texture_format_and_pitch_guard(self):
        header = bytearray(23)
        header[10] = 14
        struct.pack_into("<HHHHI", header, 11, 0, 2, 1, 8, 0)
        raw = bytes(header) + bytes(8) + b"\0"
        self.assertTrue(texture_info(raw)["editable"])
        header[17] = 7
        with self.assertRaises(ValueError): texture_info(bytes(header) + bytes(9))


class TitleTests(unittest.TestCase):
    def test_title_patch_preserves_length_and_rejects_unknown_layout(self):
        from title_logo import patch_title
        source = DEFAULT_WORK / ".base/frontend.swf"
        if not source.exists():
            self.skipTest("No extracted frontend fixture")
        original = source.read_bytes()
        patched = patch_title(original)
        self.assertEqual(len(patched), len(original))
        self.assertGreater(sum(a != b for a, b in zip(original, patched)), 34)
        with self.assertRaises(ValueError):
            patch_title(b"CWS" + original[3:])
        with self.assertRaises(ValueError):
            patch_title(patched)  # guard against patching an unknown/already changed shape


class SwfInventoryTests(unittest.TestCase):
    def setUp(self):
        source = DEFAULT_WORK / ".base/frontend.swf"
        if not source.exists():
            self.skipTest("No extracted frontend fixture")
        self.source = source.read_bytes()

    def test_lossless_tags_are_indexed_but_not_misreported_as_art(self):
        tags = swf_bitmap_tags(self.source)
        self.assertEqual(len(tags), 316)
        self.assertTrue(all(t["bitmap_format"] == 4 for t in tags))
        self.assertTrue(all((t["width"], t["height"]) == (2, 1) for t in tags))
        self.assertTrue(all(not t["editable_pixels"] for t in tags))

    def test_malformed_and_compressed_swf_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "FWS"):
            swf_bitmap_tags(b"CWS" + self.source[3:])
        with self.assertRaises(ValueError):
            swf_bitmap_tags(self.source[:-1])


class PublishTests(unittest.TestCase):
    def test_publish_copies_obb_and_only_prints_manual_adb_command(self):
        from contextlib import redirect_stdout
        from io import StringIO

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = root / "validated.dz"
            target.write_bytes(b"validated obb bytes")
            digest = sha(target.read_bytes())
            output = root / "out"
            report = {"output_sha256": digest}
            stdout = StringIO()
            with patch("push.build", return_value=(target, report)):
                with redirect_stdout(stdout):
                    result = publish(root / "work", output)
            self.assertEqual(result.read_bytes(), target.read_bytes())
            self.assertEqual(sha(result.read_bytes()), digest)
            self.assertIn("adb push", stdout.getvalue())
            self.assertIn("não executado", stdout.getvalue())
            self.assertFalse((output / "blackops_etc.dz").exists())


def integration(work):
    """Cópia isolada: uma mudança real e tentativas inválidas. Sem ADB."""
    from kit import GROUP, file_sha, read_archive, write_json
    from repack import build
    import os
    validation = work.parent / "validation"
    validation.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="integration-", dir=validation) as tmp:
        isolated = Path(tmp)
        (isolated / ".base").mkdir()
        # Base imutável pode ser compartilhada; PNGs são cópias independentes.
        os.link(work / ".base/blackops_etc.dz", isolated / ".base/blackops_etc.dz")
        shutil.copyfile(work / ".base/manifest.sha256", isolated / ".base/manifest.sha256")
        shutil.copyfile(work / "manifest.json", isolated / "manifest.json")
        for folder in ("assets", "previews"):
            shutil.copytree(work / folder, isolated / folder)
        manifest = json.loads((isolated / "manifest.json").read_text())
        start = next(a for a in manifest["assets"] if a["name"] == "img_butt_start")
        target = isolated / start["png"]
        image = Image.open(target).copy()
        old_pixel = image.getpixel((128, 64))
        image.putpixel((128, 64), ((old_pixel[0] + 1) % 256, *old_pixel[1:]))
        image.save(target)
        output, report = build(isolated)
        assert [a["index"] for a in report["changed"]] == [start["index"]]
        base = (isolated / ".base/blackops_etc.dz").read_bytes()
        changed = output.read_bytes()
        import numpy as np
        differences = int(np.count_nonzero(np.frombuffer(base, dtype=np.uint8) != np.frombuffer(changed, dtype=np.uint8)))
        assert differences == 1, differences
        del base, changed
        rejected = []
        for kind in ("dimensions", "palette", "missing", "etc_preview"):
            image.save(target)
            preview = isolated / next(a["png"] for a in manifest["assets"] if not a["editable"])
            preview_before = preview.read_bytes()
            if kind == "dimensions": Image.new("RGBA", (1, 1)).save(target)
            elif kind == "palette": image.convert("P").save(target)
            elif kind == "missing": target.unlink()
            else:
                im = Image.open(preview).copy()
                p = im.getpixel((0, 0))
                im.putpixel((0, 0), ((p[0] + 1) % 256, *p[1:]))
                im.save(preview)
            try:
                build(isolated)
            except ValueError:
                rejected.append(kind)
            else:
                raise AssertionError("Entrada inválida não recusada: " + kind)
            finally:
                preview.write_bytes(preview_before)
        assert len(rejected) == 4
        write_json(validation / "integration.json", dict(source_sha256=file_sha(work / ".base/blackops_etc.dz"),
                   changed_asset=start["png"], changed_obb_bytes=differences, rejected=rejected,
                   unchanged_outside_frontend=report["unchanged_outside_frontend"], adb_used=False,
                   user_pngs_modified=False, passed=True))
    print("Integração: alteração de 1 canal = exatamente 1 byte alterado no OBB; entradas inválidas recusadas.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--integration", action="store_true")
    parser.add_argument("--work", type=Path, default=DEFAULT_WORK)
    args, remaining = parser.parse_known_args()
    if args.integration:
        integration(args.work)
    else:
        unittest.main(argv=[sys.argv[0], *remaining], verbosity=2)
