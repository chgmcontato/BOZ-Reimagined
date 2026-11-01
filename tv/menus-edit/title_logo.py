"""Patch only the title's bitmap rectangle, preserving SWF tag lengths and scripts.

The engine resolves bitmap 1228 to atlas texture 291. No image is embedded in SWF.
Existing fixed-width bit fields are retained to keep DZ offsets stable.
"""
import struct


class Bits:
    def __init__(self, data, bit=0):
        self.data, self.bit = data, bit

    def take(self, count, expected=None, replacement=None, signed=False):
        start = self.bit
        value = 0
        for _ in range(count):
            value = (value << 1) | ((self.data[self.bit // 8] >> (7 - self.bit % 8)) & 1)
            self.bit += 1
        if signed and count and value & (1 << (count - 1)):
            value -= 1 << count
        if expected is not None and value != expected:
            raise ValueError(f"Unexpected title field at bit {start}: {value} != {expected}")
        if replacement is not None:
            low, high = (-(1 << (count - 1)), 1 << (count - 1)) if signed else (0, 1 << count)
            if not low <= replacement < high:
                raise ValueError("Title value exceeds original field width")
            for i in range(count):
                pos = start + i
                mask = 1 << (7 - pos % 8)
                self.data[pos // 8] = (self.data[pos // 8] & ~mask) | (((replacement >> (count - i - 1)) & 1) * mask)
        return value

    def align(self):
        self.bit = (self.bit + 7) // 8 * 8


def patch_title(source):
    if source[:3] != b"FWS" or struct.unpack_from('<I', source, 4)[0] != len(source):
        raise ValueError("Requires uncompressed, intact SWF")
    out = bytearray(source)
    b = Bits(out, 64)
    n = b.take(5)
    b.take(n * 4)
    b.align()
    offset = b.bit // 8 + 4
    found = 0
    title_start = title_end = bitmap_end = None
    title_tags = 0
    while offset < len(out):
        tag_start = offset
        head = struct.unpack_from('<H', out, offset)[0]
        code, length = head >> 6, head & 63
        offset += 2
        if length == 63:
            length = struct.unpack_from('<I', out, offset)[0]
            offset += 4
        if offset + length > len(out):
            raise ValueError("Truncated SWF tag")
        if code == 2 and struct.unpack_from('<H', out, offset)[0] == 1224:
            found += 1
            title_start = tag_start
            title_tags = 3  # shape, sprite, title_screen export
            b = Bits(out, (offset + 2) * 8)
            b.take(5, 15)
            for old, new in zip((-15638, 16265, -8463, 10032), (-6400, 6400, -6800, 6000)):
                b.take(15, old, new, signed=True)
            b.align()
            b.take(8, 2)  # two bitmap fills, first is the engine sentinel
            for index in range(2):
                b.take(8, 67)
                # SWF IDs are little endian; Bits reads individual bytes.
                old_id, new_id = (65535, 65535) if index == 0 else (1223, 1228)
                b.take(8, old_id & 255, new_id & 255)
                b.take(8, old_id >> 8, new_id >> 8)
                b.take(1, 1)
                b.take(5, 22)
                for _ in range(2):
                    b.take(22, 1310720 if index == 0 else 1515080,
                           1310720 if index == 0 else 1638400, signed=True)
                b.take(1, 0)
                n = b.take(5, 0 if index == 0 else 15)
                if n:
                    b.take(n, -15638, -6400, signed=True)
                    b.take(n, -8463, -6800, signed=True)
                b.align()
            b.take(8, 0)  # no line styles
            b.take(4, 2)
            b.take(4, 0)
            b.take(6, 0b000101)  # MoveTo + FillStyle1
            b.take(5, 15)
            b.take(15, 16265, 6400, signed=True)
            b.take(15, 10032, 6000, signed=True)
            b.take(2, 2)
            for vertical, old, new in ((0, -31903, -12800), (1, -18495, -12800),
                                       (0, 31903, 12800), (1, 18495, 12800)):
                b.take(1, 1)
                b.take(1, 1)
                b.take(4, 14)
                b.take(1, 0)
                b.take(1, vertical)
                b.take(16, old, new, signed=True)
            b.take(6, 0)
            b.align()
            if b.bit // 8 != offset + length:
                raise ValueError("Unexpected trailing title shape data")
        if title_tags:
            title_tags -= 1
            if not title_tags:
                if code != 56:
                    raise ValueError("Expected title_screen export after title sprite")
                title_end = offset + length
        if code == 20 and struct.unpack_from('<H', out, offset)[0] == 1228:
            bitmap_end = offset + length
        offset += length
        if code == 0:
            break
    if found != 1:
        raise ValueError("Expected one title rectangle (shape 1224)")
    if not (title_start < title_end < bitmap_end):
        raise ValueError("Unexpected title/bitmap definition order")
    # Marmalade resolves shape bitmap references as it reads the SWF. Keep the
    # bitmap sequence unchanged (external atlas mapping), but defer the title
    # shape/sprite/export until its bitmap exists. No script bytes are changed.
    return bytes(out[:title_start] + out[title_end:bitmap_end]
                 + out[title_start:title_end] + out[bitmap_end:])
