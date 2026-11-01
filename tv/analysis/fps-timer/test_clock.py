#!/usr/bin/env python3
"""Exercise the original CIwGameClock code with original/patched yield call.

Requires unicorn. Device clock, yield, render callbacks and timer resolution
are mocked; actual frame-rate conversion and scheduling instructions execute.
"""
import argparse
import json
from pathlib import Path

from unicorn.arm_const import *
from test_patches import Machine, BIAS, RET

CLOCK, GAME, VTABLE = 0x50001000, 0x50002000, 0x50003000
CALLBACKS = [0x50010000, 0x50010010, 0x50010020]


class ClockMachine(Machine):
    def __init__(self, image, patches, limited, work, early=False):
        super().__init__(image, patches)
        self.now, self.work, self.frames, self.early = 0xfffffff0, work, [], early
        if not limited:
            self.u.mem_write(BIAS + 0xfbc36, bytes.fromhex('002039f7b4ee'))
        self.put(BIAS + 0x49178c, CLOCK)
        game_slot = BIAS + 0x4460a4 + int.from_bytes(image[0xfbd60:0xfbd64], 'little')
        self.put(self.get(game_slot), GAME)
        self.put(GAME, VTABLE)
        for offset, callback in zip((0x5c, 0x70, 0x6c), CALLBACKS):
            self.put(VTABLE + offset, callback | 1)
        self.put(CLOCK + 8, self.now)
        self.put(CLOCK + 0x28, 1000)
        self.put(CLOCK + 0x30, 0x1000)

    def hook(self, u, a, size, data):
        if a == BIAS + 0x1c1e60:
            u.reg_write(UC_ARM_REG_PC, RET | 1)
        elif a == BIAS + 0x35288:
            self.ret(1)  # millisecond timer resolution
        elif a == BIAS + 0x359a4:
            wait = u.reg_read(UC_ARM_REG_R0)
            assert 0 <= wait <= 34
            self.now += max(1, wait // 2 if self.early else wait)
            self.ret()
        elif a in CALLBACKS:
            if a == CALLBACKS[-1]:
                self.frames.append(self.now)
                self.now += self.work
            self.ret()
        else:
            super().hook(u, a, size, data)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', required=True, type=Path)
    p.add_argument('--manifest', type=Path)
    args = p.parse_args()
    manifest = args.manifest or Path(__file__).with_name('timer-patches.json')
    patches = json.loads(manifest.read_text())
    fps_manifest = manifest.with_name('fps-patches.json')
    patches += json.loads(fps_manifest.read_text())
    image = (args.root / 'build/re/out/img.bin').read_bytes()
    for limited, work, early in [(False, 16, False), (True, 4, False),
                                 (True, 16, False), (True, 50, False), (True, 16, True)]:
        m = ClockMachine(image, patches, limited, work, early)
        args_init = [0, 0, 0, 0, 0, BIAS + 0x4460a4]
        m.call(BIAS + 0x1c1e54, args_init)
        assert m.get(CLOCK + 0x2c) >> 24 == 1  # original free-step mode preserved
        assert m.get(CLOCK + 0x10) == m.get(CLOCK + 0x14) == m.get(CLOCK + 0x18) == 33
        for _ in range(120):
            m.call(BIAS + 0xfbc18, (CLOCK,))
        interval = (m.frames[-1] - m.frames[0]) / (len(m.frames) - 1)
        if limited and work <= 16:
            assert 33.32 <= interval <= 33.35, interval
        elif not limited:
            assert interval == 17, interval
        else:
            assert interval >= 50
        print(f'PASS native clock limited={limited}, work={work}ms, early wake={early}, mean={interval:.3f}ms, {1000/interval:.3f} FPS (simulated)')


if __name__ == '__main__':
    main()
