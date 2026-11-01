#!/usr/bin/env python3
"""Rastreia, no jogo rodando na TV (frida-gadget), quem abre a tela de reviver e a de Game Over.
Uso: PYTHONPATH=<frida> python3 trace_death.py <log>   (com adb forward tcp:27042 tcp:27042)"""
import sys
import time

import frida

# offsets na imagem do boz.s3e (a partir do byte 0x43f3 do arquivo descomprimido)
SIG_OFFSET = 0x1d4ae0
SIG = "2d e9 f0 43 1c 46 66 4b c1 b0 07 46 89 46 7b 44"
HOOKS = {
    "revive_enter": 0x1d722e,
    "results_enter": 0x1d8a94,
}

JS = """
const SIG = '%s', SIG_OFFSET = %d, HOOKS = %s;
let base = null;
for (const r of Process.enumerateRanges('r-x')) {
    const hits = Memory.scanSync(r.base, r.size, SIG);
    if (hits.length) { base = hits[0].address.sub(SIG_OFFSET); break; }
}
send({base: base ? base.toString() : null});
if (base) {
    for (const [name, off] of Object.entries(HOOKS)) {
        Interceptor.attach(base.add(off).or(1), {
            onEnter(args) {
                const bt = Thread.backtrace(this.context, Backtracer.FUZZY).slice(0, 12)
                    .map(a => { const o = a.sub(base); return o.compare(ptr('0x500000')) < 0 ? '0x' + o.toString(16) : a.toString(); });
                send({hook: name, this: args[0].toString(), bt: bt});
            }
        });
    }
}
""" % (SIG, SIG_OFFSET, HOOKS)

out = open(sys.argv[1], "a", buffering=1)


def on_message(msg, data):
    line = str(msg.get("payload", msg))
    print(line)
    out.write(time.strftime("%H:%M:%S ") + line + "\n")


dev = frida.get_device_manager().add_remote_device("127.0.0.1:27042")
session = dev.attach("Gadget")
script = session.create_script(JS)
script.on("message", on_message)
script.load()
while True:
    time.sleep(1)
