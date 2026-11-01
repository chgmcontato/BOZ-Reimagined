#!/usr/bin/env python3
"""Regenerate the timer manifest/snippet (maintenance only; build needs no assembler).

Requires keystone-engine and capstone. Writes only to --out.
"""
import argparse
import json
import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB

BASE, STATE, BIAS = 0x410000, 0x410700, 0x4a000000 - 0x35090
HOOKS = [
    (0x42f714, 0x1c6cf4, 'reset', 'classic_enter'),
    (0x431180, 0x1d52e0, 'start', 'classic_play'),
    (0x42f71c, 0x1c7cf4, 'tick', 'classic_tick'),
    (0x431548, 0x1d8a94, 'finish', 'classic_results'),
    (0x42f718, 0x1c67e0, 'finish', 'classic_exit'),
    (0x42754c, 0x172afc, 'reset', 'deadops_enter'),
    (0x4285c8, 0x17c66c, 'start', 'deadops_play'),
    (0x427554, 0x1725a4, 'tick', 'deadops_tick'),
    (0x428708, 0x17c84c, 'finish', 'deadops_results'),
    (0x427550, 0x1723dc, 'finish', 'deadops_exit'),
    (0x42ec90, 0x1bd5bc, 'profile', 'profile_screen'),
    (0x1e0430, 0x1df7e8, 'reset', 'classic_restart'),
    (0x17b4ce, 0x1843d8, 'reset', 'deadops_restart'),
]
DIRECT_CALLS = {'classic_restart': bytes.fromhex('fff7daf9'),
                'deadops_restart': bytes.fromhex('08f083ff')}


def assemble():
    source = Path(__file__).with_name('timer.s').read_text()
    ks = Ks(KS_ARCH_ARM, KS_MODE_THUMB)
    for _, original, action, name in HOOKS:
        source += f'''
.align 2
wrap_{name}:
    push {{r0-r3, r12, lr}}
    adr.w r12, state
    ldr r3, [r12, #4]
    adds r3, r3, #1
    str r3, [r12, #4]
    dmb ish
    bl {action}
    dmb ish
    adr.w r12, state
    ldr r3, [r12, #4]
    adds r3, r3, #1
    str r3, [r12, #4]
    pop {{r0-r3, r12, lr}}
    b.w {original:#x}
'''
    code = bytearray(ks.asm(source, addr=BASE)[0])
    assert len(code) < STATE - BASE, len(code)
    # Keystone expressions in .word use section coordinates. Resolve the two
    # PIC singleton literals explicitly from the actual add-PC instructions.
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    instructions = list(md.disasm(bytes(code[:40]), BASE))
    adds = [i.address for i in instructions if i.mnemonic == 'add' and i.op_str == 'r0, pc']
    assert len(adds) == 2, adds
    for marker, target, pc in zip((0xfeed0001, 0xfeed0002), (0x49610c, 0x4958d8), adds):
        old = struct.pack('<I', marker)
        assert code.count(old) == 1
        pos = code.index(old)
        code[pos:pos + 4] = struct.pack('<I', target - (pc + 4))
    # Find wrappers by their unique, common push/ADR prefix, including after
    # embedded strings (linear disassembly would misinterpret the strings).
    prefix = bytes(ks.asm('push {r0-r3, r12, lr}', addr=BASE)[0])
    wrappers, pos = [], 0
    while True:
        pos = code.find(prefix, pos)
        if pos < 0:
            break
        wrappers.append(BASE + pos)
        pos += len(prefix)
    assert len(wrappers) == len(HOOKS), wrappers
    # Assemble each wrapper independently. Keystone's relocation bookkeeping
    # across .asciz data otherwise shifts numeric ADR/B targets by that data's
    # length. Internal label branches above are unaffected.
    for (_, original, _, _), address in zip(HOOKS, wrappers):
        offset = address - BASE
        insns = list(md.disasm(bytes(code[offset:offset + 52]), address))
        calls = [i for i in insns if i.mnemonic == 'bl']
        assert len(calls) == 1
        action_address = int(calls[0].op_str.removeprefix('#'), 16)
        wrapper = f'''
.equ state, {STATE:#x}
push {{r0-r3, r12, lr}}
adr.w r12, state
ldr r3, [r12, #4]
adds r3, r3, #1
str r3, [r12, #4]
dmb ish
bl {action_address:#x}
dmb ish
adr.w r12, state
ldr r3, [r12, #4]
adds r3, r3, #1
str r3, [r12, #4]
pop {{r0-r3, r12, lr}}
b.w {original:#x}
'''
        result = bytes(ks.asm(wrapper, addr=address)[0])
        assert len(result) == 52
        code[offset:offset + 52] = result
    patches = [(BASE, bytes(len(code)), bytes(code), 'timer code')]
    patches.append((STATE, bytes(64), struct.pack('<16I', 0x54494d31, *([0] * 15)), 'timer state v1'))
    entries = {}
    for (slot, original, _, name), address in zip(HOOKS, wrappers):
        if name in DIRECT_CALLS:
            patches.append((slot, DIRECT_CALLS[name], bytes(ks.asm(f'bl {address:#x}', addr=slot)[0]), name))
        else:
            patches.append((slot, struct.pack('<I', BIAS + original + 1),
                            struct.pack('<I', BIAS + address + 1), name))
        entries[name] = {'entry': address, 'original': original}
    return patches, entries


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', required=True, type=Path)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    patches, entries = assemble()
    manifest = [{'offset': a, 'old': old.hex(), 'new': new.hex(), 'label': label, **entries.get(label, {})}
                for a, old, new, label in patches]
    (args.out / 'timer-patches.json').write_text(json.dumps(manifest, indent=2) + '\n')
    lines = ['# BEGIN GENERATED TIMER (assemble_timer.py + timer.s)', 'TIMER_CODE = [']
    for address, old, new, label in patches:
        before = f'bytes({len(old)})' if not any(old) else f'bytes.fromhex("{old.hex()}")'
        chunks = [new.hex()[i:i + 96] for i in range(0, len(new.hex()), 96)]
        after = 'bytes.fromhex(\n' + ''.join(f'        "{chunk}"\n' for chunk in chunks) + '    )'
        lines.append(f'    # {label}\n    ({address:#x}, {before}, {after}),')
    lines.extend([']', '# END GENERATED TIMER', ''])
    (args.out / 'timer-snippet.py').write_text('\n'.join(lines))
    print(f'{len(patches)} guarded patches; code={len(patches[0][2])} bytes; state={STATE:#x}')


if __name__ == '__main__':
    main()
