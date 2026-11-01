#!/usr/bin/env python3
"""Regenerate the independent yield trampoline (maintenance; no build dependency)."""
import argparse
import json
from pathlib import Path
from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    k = Ks(KS_ARCH_ARM, KS_MODE_THUMB)
    code = bytes(k.asm(Path(__file__).with_name('fps.s').read_text(), addr=0x410600)[0])
    assert len(code) <= 0x80
    hook = bytes(k.asm('bl 0x410600; nop', addr=0xfbc36)[0])
    assert len(hook) == 6
    items = [(0x410600, bytes(len(code)), code, 'FPS deadline/yield trampoline'),
             (0x410680, bytes(16), bytes(16), 'FPS timestamps/phase (reserved)'),
             (0xfbc36, bytes.fromhex('002039f7b4ee'), hook, 'FPS free-step yield hook')]
    patches = [{'offset': a, 'old': old.hex(), 'new': new.hex(), 'label': label}
               for a, old, new, label in items]
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'fps-patches.json').write_text(json.dumps(patches, indent=2) + '\n')
    s = ['# BEGIN GENERATED FPS (assemble_fps.py + fps.s)', 'FPS_CODE = [']
    for a, old, new, label in items:
        before = f'bytes({len(old)})' if not any(old) else f'bytes.fromhex("{old.hex()}")'
        chunks = [new.hex()[i:i + 96] for i in range(0, len(new.hex()), 96)]
        after = 'bytes.fromhex(\n' + ''.join(f'        "{c}"\n' for c in chunks) + '    )'
        s.append(f'    # {label}\n    ({a:#x}, {before}, {after}),')
    s.extend([']', '# END GENERATED FPS', ''])
    (args.out / 'fps-snippet.py').write_text('\n'.join(s))
    print(f'FPS code={len(code)} bytes; hook 0xfbc36: 002039f7b4ee -> {hook.hex()}')


if __name__ == '__main__':
    main()
