// Pilha de chamadas quando um zumbi entra no estado 15 (morrendo): acha o caminho de morte/dano do jogo.
// sub_1453b8 (0x1453b8) é o setter de NetworkProperty<u8>: r0 = propriedade (zumbi+0xf4 = estado, byte em +9),
// r1 = valor novo. Só leitura. Uso: run.py build/frida/zombie_death.js 900
const T = (o) => base.add(o + 1);
const rel = (p) => { try { return 'img+' + (p.sub(base).toInt32() >>> 0).toString(16); } catch (e) { return String(p); } };
let n = 0;
Interceptor.attach(T(0x1453b8), function () {
    if (this.context.r1.toInt32() !== 15 || n >= 6) return;
    let cur;
    try { cur = this.context.r0.add(9).readU8(); } catch (e) { return; }
    if (cur === 15) return;
    n++;
    send('morte ' + n + ' prop=' + this.context.r0 + ' zumbi=' + this.context.r0.sub(0xf4) + ' de ' + cur + '\n  ' +
         Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 12).map(rel).join(' '));
});
send('zombie_death pronto, base=' + base);
