#!/usr/bin/env python3
"""Remove COTD from the normal lightning pool; preserve all other console entries."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'maps/tools'))
import console_bin
import dz
KEY = b'LightningBoltLevels\0'
OLD = b'callofthedead|proto\0'
NEW = b'proto\0'

def patch(data):
    entries = console_bin.parse(data)
    matches = [(t, v) for name, t, v in entries if name == 'LightningBoltLevels']
    if matches == [(console_bin.STR, 'proto')]:
        return data
    if matches != [(console_bin.STR, 'callofthedead|proto')]:
        raise ValueError('Unexpected LightningBoltLevels: %r' % (matches,))
    pos = data.index(KEY) + len(KEY)
    assert data[pos:pos+len(OLD)] == OLD
    result = data[:pos] + NEW + data[pos+len(OLD):]
    expected = [(name, t, 'proto' if name == 'LightningBoltLevels' else value)
                for name, t, value in entries]
    # The existing parser can decode opaque fields as NaN; NaN != NaN.
    assert repr(console_bin.parse(result)) == repr(expected)
    return result

def main():
    root = Path(__file__).resolve().parents[1]
    path = root / 'maps/mobile/console.bin'
    old = path.read_bytes(); new = patch(old)
    if old != new:
        path.write_bytes(new)
    obb = root / 'obb/blackops_etc.dz'
    entries, dirs = dz.entries_of(str(obb))
    count = 0; changed = False; output = []
    for directory, name, data, index in entries:
        if name == 'console.bin':
            count += 1
            patched = patch(data)
            changed |= patched != data
            data = patched
        output.append((directory, name, data, index))
    assert count == 1, count
    if changed:
        dz.write(str(obb), output, dirs=dirs)
    print('LightningBoltLevels: proto (mobile console and OBB; DeathMachineLevels preserved)')

if __name__ == '__main__':
    main()
