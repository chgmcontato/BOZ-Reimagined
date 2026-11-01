// Mecânica do zumbi preso (patch_s3e.py, 0x410de0): lê a tabela em 0x40f200 a cada segundo e mostra, por zumbi
// acompanhado, o estado, há quanto tempo está sem sair do raio e a vida. Só leitura, sem hook no código novo.
// Uso: run.py build/frida/stuck_watch.js 1800
const TABLE = base.add(0x40f200), N = 32, SIZE = 24;
const log = (s) => send(new Date().toISOString().substr(11, 8) + ' ' + s);
let lastNow = 0;
setInterval(() => {
    let now = 0;
    for (let i = 0; i < N; i++) now = Math.max(now, TABLE.add(i * SIZE + 4).readU32());
    if (now === lastNow) return;   // fora da partida: nada atualiza
    lastNow = now;
    const rows = [];
    for (let i = 0; i < N; i++) {
        const e = TABLE.add(i * SIZE);
        const z = e.readPointer();
        if (z.isNull() || now - e.add(4).readU32() > 1000) continue;
        let st = '?';
        try { st = z.add(0xfd).readU8(); } catch (err) {}
        const still = ((now - e.add(8).readU32()) / 1000).toFixed(1);
        rows.push(z + ' st=' + st + ' parado=' + still + 's');
    }
    if (rows.length) log(rows.length + ' zumbis\n  ' + rows.join('\n  '));
}, 1000);
log('stuck_watch pronto, base=' + base);
