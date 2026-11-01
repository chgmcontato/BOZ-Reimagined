// Observação dos zumbis para a mecânica anti-soft-lock (zumbi sem progresso por 15 s é morto).
// Só leitura: nenhum hook altera registradores nem chama funções do jogo.
//
// Como usar (build FRIDA=1 na TV, partida em andamento):
//   adb -s 192.168.14.9:5555 forward tcp:27042 tcp:27042
//   PYTHONPATH=<venv frida 17.x> python3 build/frida/run.py build/frida/zombie_watch.js 1800 | tee zombies.log
//
// O que registra:
//   calls   a cada 5 s, quantas vezes cada função do CAIControllerZombie rodou (acha o update por quadro)
//   z       a cada 2 s, por zumbi visto no último segundo: estado (+0xfd), +0x71, alvo (+0x11c), +0x190,
//           posição (transform +0x5c/+0x60/+0x64, pego quando o jogo chama CIsTransform::sub_f7474 com a
//           entidade do zumbi, [this+0xc])
//   nearest posição do jogador (transform da entidade do CPlayerController, [0x4958d0]+0x1a0 -> +0xc)

const T = (o) => base.add(o + 1);
const hex = (p) => '0x' + (p >>> 0).toString(16);
const log = (s) => send(new Date().toISOString().substr(11, 12) + ' ' + s);
const rel = (p) => { try { return 'img+' + hex(p.sub(base).toInt32()); } catch (e) { return String(p); } };

const FUNCS = {
    vf17: 0x1451a8, vf24: 0x1451b4, vf25: 0x1451d8, vf21: 0x1451f2, vf23: 0x145370, vf19: 0x147984,
    vf27: 0x147cec, vf22: 0x14c468, s1471b4: 0x1471b4, s1458a0: 0x1458a0, s145fa4: 0x145fa4, s1464ac: 0x1464ac,
};
const counts = {};
const zombies = {};            // this -> {seen, entity}
const entities = {};           // entity -> this
for (const [name, off] of Object.entries(FUNCS)) {
    counts[name] = 0;
    try { Interceptor.attach(T(off), function () {
        counts[name]++;
        const z = this.context.r0;
        const k = z.toString();
        let e;
        try { e = z.add(0xc).readPointer(); } catch (err) { return; }
        if (!zombies[k]) zombies[k] = { entity: e, transform: null };
        zombies[k].seen = Date.now();
        zombies[k].entity = e;
        entities[e.toString()] = k;
    }); } catch (err) { delete counts[name]; log('sem hook em ' + name + ': ' + err.message); }
}

// transform de cada entidade conhecida (zumbis e jogador)
const playerEntity = () => {
    try {
        const lm = base.add(0x4958d0).readPointer();
        const pc = lm.add(0x1a0).readPointer();
        return pc.isNull() ? null : pc.add(0xc).readPointer();
    } catch (e) { return null; }
};
let playerTransform = null;
Interceptor.attach(T(0xf7474), {
    onEnter(a) { this.e = a[0]; },
    onLeave(r) {
        const k = entities[this.e.toString()];
        if (k && zombies[k]) zombies[k].transform = ptr(r);
        const pe = playerEntity();
        if (pe && this.e.equals(pe)) playerTransform = ptr(r);
    }
});

function pos(t) {
    if (!t || t.isNull()) return null;
    try { return [t.add(0x5c).readFloat(), t.add(0x60).readFloat(), t.add(0x64).readFloat()]; } catch (e) { return null; }
}
const f = (v) => v === null ? '?' : v.map(x => x.toFixed(0)).join(',');

// (0x1499e6 não é início de função: a análise deu esse nome a um trecho do meio de outra)

setInterval(() => {
    log('calls ' + Object.entries(counts).map(([k, v]) => k + '=' + v).join(' '));
    for (const k in counts) counts[k] = 0;
}, 5000);

setInterval(() => {
    const now = Date.now();
    const p = pos(playerTransform);
    let line = 'player ' + f(p);
    for (const [k, z] of Object.entries(zombies)) {
        if (now - z.seen > 1000) { delete zombies[k]; continue; }
        const zp = ptr(k);
        let s;
        try {
            s = 'st=' + zp.add(0xfd).readU8() + ' b71=' + zp.add(0x71).readU8() + ' b9d=' + zp.add(0x9d).readU8() +
                ' tgt=' + zp.add(0x11c).readPointer() + ' p190=' + zp.add(0x190).readPointer() +
                ' i1a4=' + zp.add(0x1a4).readS32();
        } catch (e) { s = 'ilegível'; }
        const q = pos(z.transform);
        let d = '';
        if (p && q) d = ' dist=' + Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]).toFixed(0);
        line += '\n  z ' + k + ' ' + s + ' pos=' + f(q) + d;
    }
    log(line);
}, 2000);

log('zombie_watch pronto, base=' + base);
