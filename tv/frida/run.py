#!/usr/bin/env python3
"""Roda um script JS do Frida no jogo (frida-gadget), com `base` (início da imagem do boz.s3e) já definido.
Uso: PYTHONPATH=<frida> python3 run.py script.js [segundos]   (com adb forward tcp:27042 tcp:27042)"""
import sys
import time

import frida

PRELUDE = """
const SIG = '2d e9 f0 43 1c 46 66 4b c1 b0 07 46 89 46 7b 44', SIG_OFFSET = 0x1d4ae0;
let base = null;
for (const r of Process.enumerateRanges('r-x')) {
    const hits = Memory.scanSync(r.base, r.size, SIG);
    if (hits.length) { base = hits[0].address.sub(SIG_OFFSET); break; }
}
if (!base) throw new Error('imagem do jogo não encontrada');
"""

js = PRELUDE + open(sys.argv[1]).read()
wait = float(sys.argv[2]) if len(sys.argv) > 2 else 3
dev = frida.get_device_manager().add_remote_device("127.0.0.1:27042")
session = dev.attach("Gadget")
script = session.create_script(js)
script.on("message", lambda m, d: print(m.get("payload", m) if m["type"] == "send" else m, flush=True))
script.load()
time.sleep(wait)
