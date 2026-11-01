#!/usr/bin/env python3
"""Emulate actual Thumb hooks and native stat trees/serializer (I/O primitives mocked).

Usage: PYTHONPATH=<unicorn installation> python3 test_patches.py --root <Port>
"""
import argparse
import json
import struct
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import *

BIAS = 0x4a000000 - 0x35090
STATS, SAVE, STACK, HEAP, RET = 0x50000000, 0x50006000, 0x501ff000, 0x50020000, 0x501fe000
SECONDS, REMAINDER = 0xaba863ee, 0x8ce1203f
REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12]


class Machine:
    def __init__(self, image, patches):
        self.u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.u.mem_map(BIAS & ~4095, 0x600000)
        self.u.mem_map(STATS, 0x200000)
        self.u.mem_write(BIAS, image)
        for item in patches:
            a = item['offset']
            old, new = bytes.fromhex(item['old']), bytes.fromhex(item['new'])
            assert image[a:a + len(old)] == old, (hex(a), item['label'])
            self.u.mem_write(BIAS + a, new)
        self.now, self.heap, self.saves, self.save_status = 10000, HEAP, 0, 0
        self.page, self.expected, self.seen = '', None, []
        self.serial, self.read_serial, self.serial_pos = None, False, 0
        self.u.hook_add(UC_HOOK_CODE, self.hook)
        self.put(BIAS + 0x49610c, STATS)
        self.put(BIAS + 0x4958d8, SAVE)
        self.put(SAVE + 0xf4, 0)
        self.put(BIAS + 0x44f82c, 0xffffffff)
        self.init_stats()
        self.originals = {BIAS + p['original'] + 1: p['label']
                          for p in patches if 'original' in p}
        self.patches = {p['label']: p for p in patches}

    def put(self, a, value):
        self.u.mem_write(a, struct.pack('<I', value & 0xffffffff))

    def get(self, a):
        return struct.unpack('<I', self.u.mem_read(a, 4))[0]

    def init_stats(self):
        self.u.mem_write(STATS, bytes(0x60))
        for i in range(6):
            head = self.heap
            self.heap += 32
            self.u.mem_write(head, bytes(32))
            self.put(head + 8, head)
            self.put(head + 12, head)
            self.put(STATS + 0x10 + i * 12, head)

    def state(self, offset):
        return self.get(BIAS + 0x410700 + offset)

    def ret(self, value=None):
        if value is not None:
            self.u.reg_write(UC_ARM_REG_R0, value)
        self.u.reg_write(UC_ARM_REG_PC, self.u.reg_read(UC_ARM_REG_LR))

    def hook(self, u, a, size, data):
        if a == RET:
            u.emu_stop()
            return
        if (a | 1) in self.originals and self.expected is not None:
            label = self.originals[a | 1]
            assert label == self.expected, (label, self.expected)
            for register, value in zip(REGS, self.values):
                assert u.reg_read(register) == value, (label, register)
            assert u.reg_read(UC_ARM_REG_SP) == STACK
            self.seen.append(label)
            self.ret()
            return
        off = a - BIAS
        if off == 0x35d50:
            u.reg_write(UC_ARM_REG_R0, self.now & 0xffffffff)
            u.reg_write(UC_ARM_REG_R1, self.now >> 32)
            self.ret()
        elif off in (0x37d3b8, 0x37d3b0, 0x4b802):
            size = u.reg_read(UC_ARM_REG_R0)
            result = self.heap
            self.heap += (size + 15) & ~15
            self.u.mem_write(result, bytes(size))
            self.ret(result)
        elif off == 0x28ae60:
            self.ret(123)
        elif off == 0x229fd8:
            assert u.reg_read(UC_ARM_REG_R0) == STATS
            assert u.reg_read(UC_ARM_REG_R1) == 0
            self.saves += 1
            self.ret(self.save_status)
        elif off == 0x1bcf08:
            p = u.reg_read(UC_ARM_REG_R1)
            name = bytes(u.mem_read(p, 32)).split(b'\0')[0].decode()
            self.ret(int(name == self.page))
        elif self.serial is not None and off in (0x28cc28, 0x28cd34, 0x28d3b0):
            p = u.reg_read(UC_ARM_REG_R0)
            count = u.reg_read(UC_ARM_REG_R1)
            stride = u.reg_read(UC_ARM_REG_R3)
            for i in range(count):
                if self.read_serial:
                    value = self.serial[self.serial_pos]
                    self.serial_pos += 1
                    u.mem_write(p + i * stride, value.to_bytes(stride, 'little'))
                else:
                    self.serial.append(int.from_bytes(u.mem_read(p + i * stride, stride), 'little'))
            self.ret()

    def call(self, address, args=()):
        self.u.reg_write(UC_ARM_REG_SP, STACK)
        self.u.reg_write(UC_ARM_REG_LR, RET | 1)
        for reg, value in zip(REGS, args):
            self.u.reg_write(reg, value)
        self.u.emu_start(address | 1, RET, count=1000000)
        assert self.u.reg_read(UC_ARM_REG_PC) == RET
        assert self.u.reg_read(UC_ARM_REG_SP) == STACK

    def event(self, name):
        p = self.patches[name]
        self.expected = name
        self.values = [0x50008000, 0x123, 0x234, 0x345] + list(range(0x404, 0x40d))
        count = len(self.seen)
        self.call(BIAS + p['entry'], self.values)
        assert len(self.seen) == count + 1
        assert self.state(4) % 2 == 0
        self.expected = None

    def node(self, key, table=0):
        self.call(BIAS + 0x231ba2, (STACK - 128, STATS, table, key))
        return self.get(STACK - 128)

    def value(self, key):
        return self.get(self.node(key) + 0x14)

    def serialise(self, read=False):
        self.read_serial, self.serial_pos = read, 0
        if not read:
            self.serial = []
        self.call(BIAS + 0x231f4e, (STATS,))
        result = self.serial
        if read:
            assert self.serial_pos == len(result)
        self.serial = None
        return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--manifest', type=Path)
    args = p.parse_args()
    manifest = args.manifest or Path(__file__).with_name('timer-patches.json')
    patches = json.loads(manifest.read_text())
    image = (args.root / 'build/re/out/img.bin').read_bytes()
    for kind in ('classic', 'deadops'):
        m = Machine(image, patches)
        m.put(m.node(0x12345678) + 0x14, 987)
        m.event(kind + '_enter')
        m.event(kind + '_play')
        assert m.state(8) == 1 and m.state(12) == 0
        m.now += 120125
        m.event(kind + '_tick')
        assert m.state(12) == 120125
        # Resume from pause must preserve the original timestamp.
        m.event(kind + '_play')
        m.now += 250
        m.event(kind + '_results')
        assert (m.value(SECONDS), m.value(REMAINDER), m.saves) == (120, 375, 1)
        m.event(kind + '_exit')
        assert m.saves == 1
        m.event(kind + '_enter')
        m.event(kind + '_play')
        assert m.state(12) == 0
        m.now += 299625
        m.event(kind + '_exit')
        assert (m.value(SECONDS), m.value(REMAINDER), m.saves) == (420, 0, 2)
        assert m.state(40) == m.state(44) == 0
        assert m.value(0x12345678) == 987
        wire = m.serialise()
        m.init_stats()
        m.serial = wire
        m.serialise(read=True)
        assert (m.value(SECONDS), m.value(REMAINDER), m.value(0x12345678)) == (420, 0, 987)
        for page, show in [('', 0), ('player_profile', 1), ('profile_stats', 1), ('main_menu', 0)]:
            m.page = page
            m.event('profile_screen')
            assert (m.state(32), m.state(20)) == (show, 420)
        m.event(kind + '_play')
        m.now += 10000
        m.event(kind + '_restart')
        assert m.value(SECONDS) == 430 and m.state(12) == 0 and m.state(8) == 0
        m.event(kind + '_play')
        assert m.state(12) == 0 and m.state(8) == 1
        print('PASS', kind, '120s timer, pause/resume, results+exit dedupe, new match, quit, native stat tree + serializer round trip, profile')
    for duration in (0, 999, 1000, 3600123, 0xffffffff, 0x100000001, 2147483647999):
        m = Machine(image, patches)
        m.now = 0xffffff00
        m.event('classic_play')
        m.now += duration
        m.event('classic_exit')
        assert (m.value(SECONDS), m.value(REMAINDER)) == (min(duration // 1000, 0x7fffffff), duration % 1000), duration
    print('PASS 7 time boundaries, 64-bit timer wrap and saturating profile total')
    m = Machine(image, patches)
    m.event('classic_play')
    m.put(SAVE + 0xf4, 1)
    m.now += 10000
    m.event('classic_exit')
    assert m.state(36) == 6 and m.saves == 0 and m.value(SECONDS) == 0
    m = Machine(image, patches)
    m.event('classic_play')
    m.now += 2000
    m.save_status = 4
    m.event('classic_exit')
    assert m.state(36) == 4 and m.saves == 1 and m.value(SECONDS) == 2
    print('PASS profile isolation and native save error surfaced')


if __name__ == '__main__':
    main()
