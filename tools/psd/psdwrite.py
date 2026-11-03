"""Minimal layered PSD writer: RGBA raster layers, nested groups and guides (no dependencies but Pillow)."""
import struct


def _pascal(name, pad):
    raw = name.encode("latin-1", "replace")[:255]
    b = bytes([len(raw)]) + raw
    return b + b"\0" * ((pad - len(b) % pad) % pad)


def _block(key, data):
    if len(data) % 2:
        data += b"\0"
    return b"8BIM" + key + struct.pack(">I", len(data)) + data


def _unicode(name):
    return struct.pack(">I", len(name)) + name.encode("utf-16-be")


def _channels(im):
    """RGBA pillow image -> {channel id: raw data with the raw-compression header}."""
    r, g, b, a = im.split()
    return [(-1, a), (0, r), (1, g), (2, b)]


class Layer:
    def __init__(self, name, image=None, left=0, top=0, visible=True, section=0):
        self.name, self.image, self.left, self.top = name, image, left, top
        self.visible, self.section = visible, section


def write_psd(path, width, height, layers, composite, guides=()):
    """layers: bottom-first. section: 0 layer, 1 group start (open folder), 3 group end marker."""
    records = b""
    pixels = b""
    for l in layers:
        im = l.image
        w, h = (im.size if im else (0, 0))
        chans = _channels(im) if im else [(-1, None), (0, None), (1, None), (2, None)]
        rec = struct.pack(">iiii", l.top, l.left, l.top + h, l.left + w)
        rec += struct.pack(">H", len(chans))
        for cid, c in chans:
            size = 2 + (w * h if c is not None else 0)
            rec += struct.pack(">hI", cid, size)
            pixels += struct.pack(">H", 0) + (c.tobytes() if c is not None else b"")
        rec += b"8BIMnorm" + bytes([255, 0, 0 if l.visible else 2, 0])
        extra = struct.pack(">I", 0) + struct.pack(">I", 0)
        extra += _pascal(l.name, 4)
        extra += _block(b"luni", _unicode(l.name))
        if l.section:
            extra += _block(b"lsct", struct.pack(">I", l.section))
        rec += struct.pack(">I", len(extra)) + extra
        records += rec
    info = struct.pack(">h", len(layers)) + records + pixels
    if len(info) % 2:
        info += b"\0"
    layer_info = struct.pack(">I", len(info)) + info
    mask = struct.pack(">I", 0)
    misc = layer_info + mask
    # guides (resource 1032): 1/32 pixel units
    res = b""
    if guides:
        g = struct.pack(">IIII", 1, 576, 576, len(guides))
        for pos, vertical in guides:
            g += struct.pack(">IB", int(pos * 32), 0 if vertical else 1)
        res += b"8BIM" + struct.pack(">H", 1032) + b"\0\0" + struct.pack(">I", len(g)) + g + (b"\0" if len(g) % 2 else b"")
    with open(path, "wb") as f:
        f.write(b"8BPS" + struct.pack(">H", 1) + b"\0" * 6)
        f.write(struct.pack(">HIIHH", 4, height, width, 8, 3))
        f.write(struct.pack(">I", 0))
        f.write(struct.pack(">I", len(res)) + res)
        f.write(struct.pack(">I", len(misc)) + misc)
        r, g_, b, a = composite.convert("RGBA").split()
        f.write(struct.pack(">H", 0))
        for c in (r, g_, b, a):
            f.write(c.tobytes())
