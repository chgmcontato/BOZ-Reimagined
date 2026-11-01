// Quais funções do CBodyDamageController rodam ao acertar/matar zumbis, e com que argumentos (acha a função
// de dano para a mecânica anti-soft-lock). Só leitura. Uso: run.py build/frida/zombie_damage.js 900
const T = (o) => base.add(o + 1);
const hex = (v) => '0x' + (v >>> 0).toString(16);
const F = { vf17: 0x157910, vf19: 0x157abc, s157b50: 0x157b50, vf22: 0x157b98, s157bf8: 0x157bf8, s157c5c: 0x157c5c,
            s157d50: 0x157d50, s157d9c: 0x157d9c, s157db0: 0x157db0, s157ddc: 0x157ddc, s157fe8: 0x157fe8, vf23: 0x158178 };
const n = {}, shown = {};
for (const [k, o] of Object.entries(F)) {
    n[k] = 0; shown[k] = 0;
    try {
        Interceptor.attach(T(o), function () {
            n[k]++;
            if (shown[k] < 4 && k !== 'vf23') {
                shown[k]++;
                const c = this.context;
                let f = '';
                try { f = ' r1f=' + c.r1.toInt32() + '/' + new Float32Array(new Uint32Array([c.r1.toInt32()]).buffer)[0].toFixed(2); } catch (e) {}
                send(k + ' this=' + c.r0 + ' r1=' + hex(c.r1.toInt32()) + ' r2=' + hex(c.r2.toInt32()) + ' r3=' + hex(c.r3.toInt32()) + f);
            }
        });
    } catch (e) { send('sem hook ' + k + ': ' + e.message); }
}
setInterval(() => {
    const s = Object.entries(n).filter(([k, v]) => v).map(([k, v]) => k + '=' + v).join(' ');
    if (s) send(new Date().toISOString().substr(11, 8) + ' ' + s);
    for (const k in n) n[k] = 0;
}, 2000);
send('zombie_damage pronto, base=' + base);
