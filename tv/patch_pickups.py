#!/usr/bin/env python3
"""BOZ 1.0.11 pickups: guarded, idempotent image patches. No assembler needed at build time."""
import lzma
import struct
import sys
from pathlib import Path
IMAGE = 0x43f3
LATCH = 0x2bd
PATCHES = [[4262912, '000000000000000000000000000000000000000000000000', '84f8bc5284f8bd5203b471a04ff00001017003bcfaf5e5be', 'init: +2bc original, latch=0'], [2144734, '84f8bc52', '05f20fb9', 'constructor latch reset'], [4262944, '0000000000000000000000000000000000000000000000000000', '84f8bc0206b4002804d0012184f8bd1267a2117006bc00f673ba', 'vf24: preserve +2bc, latch if active'], [2167068, '84f8bc02', 'fff180bd', 'vf24 latch'], [4262992, '000000000000000000000000000000000000000000000000000000000000000000000000', 'f8b50fb4052909d15da201231370c068f7f57cfe10b1012180f8bd120fbc0125f0f550ba', 'acquisition: mark latch on perk 5 from any grant'], [2101520, 'f8b50125', '0ff29ebd', 'perk grant latch'], [4263072, '00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000', '2de90f50062a02d100f072f828b1bde80f502de9f041f7f539b8bde80f5070470000000000000000000000000000000000000000000000000000000000000000000000000000000000000000', 'ApplyEffect entry: gate only type 6, before all side effects'], [2129192, '2de9f041', '08f2babf', 'Death Machine recipient gate'], [4263312, '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000', '0da30d481844006810b190f83d0210b90ba0007800b10120704702b5fff7f0ffbde8024008b941f0400141f4807103a20248024491607047084b0800f4fcffff00000000', 'dmok (energia ou QR na partida) + blkset (DM sem dmok; raio sempre) + flag'], [4262404, '2ea29160', '00f0d1f9', 'dcycle: bloqueia a Death Machine no ciclo se !dmok']]

def apply(raw):
    raw = bytearray(raw)
    for address, before, after, label in PATCHES:
        old, new = bytes.fromhex(before), bytes.fromhex(after)
        off = IMAGE + address
        current = bytes(raw[off:off + len(old)])
        if current == new:
            continue
        if current != old:
            raise ValueError('Unexpected bytes at %#x (%s): %s' % (address, label, current.hex()))
        raw[off:off + len(new)] = new
        print('%#x %s: %s -> %s' % (address, label, before, after))
    return bytes(raw)

def main():
    path = Path(sys.argv[1])
    packed = path.read_bytes()
    raw = lzma.decompress(packed, format=lzma.FORMAT_ALONE)
    changed = apply(raw)
    if changed == raw:
        return
    dictionary = struct.unpack_from('<I', packed, 1)[0]
    output = lzma.compress(changed, format=lzma.FORMAT_ALONE,
        filters=[{'id': lzma.FILTER_LZMA1, 'dict_size': dictionary, 'lc': 3, 'lp': 0, 'pb': 2}])
    output = output[:5] + struct.pack('<Q', len(changed)) + output[13:]
    assert lzma.decompress(output, format=lzma.FORMAT_ALONE) == changed
    path.write_bytes(output)

if __name__ == '__main__':
    main()
