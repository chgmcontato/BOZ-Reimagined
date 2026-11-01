#!/usr/bin/env python3
"""Prepares the game folder for codboz-pc from files you own.

    python3 tools/install.py --apk Call-of-duty-black-opsv1.0.11.apk \\
                             --obb blackops_etc.dz [--out game] [--vanilla]

What it does:
  1. extracts assets/ (boz.s3e, blackops_loader.dz, music) from the Android APK 1.0.11;
  2. decompresses boz.s3e and applies the boz-reimagined gameplay patches
     (BO1 horde pace, BO1 drop cycle, HUD without touch buttons, no COD Points...);
     --vanilla skips them;
  3. rewrites touch prompts ("Tap to ...") as button prompts in blackops_loader.dz;
  4. links (or copies, with --copy-obb) the OBB archive next to them.

Nothing is downloaded and no game data ships with codboz-pc.
"""
import argparse
import hashlib
import lzma
import os
import re
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import boz_patches  # noqa: E402

APK_SHA256 = "348c3ca53291b28380eca44f25198cc64e2e8ce9d56ed4ac170310aff3a86747"
S3E_SHA256 = "f458c15a7111779ad320af377d0bb751294119788ba06d430bee0cc977539fee"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def patch_image(raw):
    """Apply the boz-reimagined tables; every edit checks the original bytes first."""
    raw = bytearray(raw)
    for old, new in boz_patches.CONFIG:
        assert len(old) == len(new)
        if raw.count(new) and not raw.count(old):
            continue
        if raw.count(old) != 1:
            raise SystemExit("unexpected boz.s3e config (%r)" % old)
        i = raw.index(old)
        raw[i:i + len(old)] = new
    applied = 0
    for off, old, new in boz_patches.CODE:
        at = boz_patches.IMAGE + off
        cur = bytes(raw[at:at + len(old)])
        if cur == new:
            continue
        if cur != old:
            raise SystemExit("unexpected bytes at %#x: %s" % (off, cur.hex()))
        raw[at:at + len(new)] = new
        applied += 1
    for address, before, after, label in boz_patches.PICKUPS:
        old, new = bytes.fromhex(before), bytes.fromhex(after)
        at = boz_patches.IMAGE + address
        cur = bytes(raw[at:at + len(old)])
        if cur == new:
            continue
        if cur != old:
            raise SystemExit("unexpected bytes at %#x (%s)" % (address, label))
        raw[at:at + len(new)] = new
        applied += 1
    print("  %d gameplay/HUD patches applied" % applied)
    return bytes(raw)


TEXTS = [
    ("Touch Screen to Start", "Press A to Start"),
    ("touch screen to start", "press a to start"),
    ("Double tap to swap your weapon.", "Press [Y] to swap your weapon."),
]
TAP = re.compile(rb"(?:Tap|TAP|tap) (to|TO) ([^\0]*)\0")


def same_size(old, new):
    pad = len(old) - len(new)
    left = pad // 2
    return (" " * left + new + " " * (pad - left)).encode()


def patch_texts(data, button):
    count = 0
    for old, new in TEXTS:
        count += data.count(old.encode())
        data = data.replace(old.encode(), same_size(old, new))
    tag = ("[%s]" % button).encode()
    data, n = TAP.subn(lambda m: tag + b" " + m.group(1) + b" " + m.group(2) + b"\0", data)
    return data, count + n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--apk", required=True, help="Android APK 1.0.11 (com.activision.boz)")
    ap.add_argument("--obb", required=True, help="OBB archive blackops_etc.dz")
    ap.add_argument("--out", default="game", help="game folder to create (default: game)")
    ap.add_argument("--vanilla", action="store_true", help="skip the boz-reimagined patches")
    ap.add_argument("--copy-obb", action="store_true", help="copy the OBB instead of linking it")
    ap.add_argument("--prompt-button", default="E",
                    help='button shown in "Tap to ..." prompts (default: E)')
    args = ap.parse_args()

    assets = os.path.join(args.out, "assets")
    os.makedirs(assets, exist_ok=True)

    print("checking %s" % args.apk)
    if sha256_file(args.apk) != APK_SHA256:
        print("  warning: APK hash differs from the known 1.0.11 build; trying anyway")
    with zipfile.ZipFile(args.apk) as apk:
        names = [n for n in apk.namelist() if n.startswith("assets/") and not n.endswith("/")]
        for name in names:
            rel = name[len("assets/"):]
            if rel in ("su.save",):
                continue
            target = os.path.join(assets, rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with apk.open(name) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst)
        print("  %d files extracted" % len(names))

    packed_path = os.path.join(assets, "boz.s3e")
    raw = lzma.decompress(open(packed_path, "rb").read(), format=lzma.FORMAT_ALONE)
    if hashlib.sha256(open(packed_path, "rb").read()).hexdigest() != S3E_SHA256:
        print("  warning: boz.s3e differs from the known 1.0.11 build")
    if args.vanilla:
        print("boz.s3e: vanilla (no patches)")
    else:
        print("boz.s3e: applying boz-reimagined patches")
        raw = patch_image(raw)
    with open(os.path.join(assets, "boz.s3e.unpacked"), "wb") as f:
        f.write(raw)

    loader = os.path.join(assets, "blackops_loader.dz")
    if os.path.exists(loader) and not args.vanilla:
        data, n = patch_texts(open(loader, "rb").read(), args.prompt_button)
        open(loader, "wb").write(data)
        print("blackops_loader.dz: %d prompts rewritten" % n)

    obb_target = os.path.join(assets, "blackops_etc.dz")
    print("checking %s" % args.obb)
    if os.path.getsize(args.obb) != 526783202:
        print("  warning: OBB size differs from the known archive")
    if os.path.lexists(obb_target):
        os.remove(obb_target)
    if args.copy_obb or os.name == "nt":
        shutil.copyfile(args.obb, obb_target)
    else:
        os.symlink(os.path.abspath(args.obb), obb_target)
    print("\nready: run  codboz-pc --root %s" % args.out)


if __name__ == "__main__":
    main()
