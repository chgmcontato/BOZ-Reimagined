// Diagnóstico da Death Machine: latch do Quick Revive (+0x2bd), gate do ApplyEffect e tipo sorteado.
// Só leitura: nenhum hook altera registradores nem chama funções do jogo.
//
// Como usar:
//   1. FRIDA=1 bash build/build.sh
//      adb -s 192.168.14.9:5555 install --no-incremental -r -g BOZ-TV.apk   (fecha o jogo)
//   2. Carlos abre o jogo e entra no Ascension. O agente roda:
//      adb -s 192.168.14.9:5555 forward tcp:27042 tcp:27042
//      PYTHONPATH=<venv frida 17.x> python3 build/frida/run.py build/frida/diag_drops.js 1800 | tee drops.log
//   3. Cenários (só controle, nunca adb shell input):
//      a) sem Quick Revive e sem energia: matar até cair Death Machine e passar por cima
//      b) ligar energia, comprar Quick Revive: esperar "latch +2bd=1"; pegar Death Machine
//      c) depois de (b), cair e ser revivido (perde o perk): +2bd deve continuar 1
//   4. Voltar a build sem Frida antes de deixar a TV.
//
// Offsets (imagem): sub_210f90 (chamado por vf24 0x2129b4 em 0x212adc) grava +0x2bc = tem perk 5 em 0x21111c,
// que salta ao latch em 0x410c20 e volta a 0x211120. Concessão de perk 0x201110 -> 0x410c50.
// ApplyEffect 0x207d28 -> gate 0x410ca0 (r2 = tipo); 0x410cae segue, 0x410cba bloqueia. dmok 0x410d90; o dcycle
// bloqueia a DM no ciclo (bit 6) via blkset 0x410daa quando !dmok.
// ChooseDropType 0x205eb0 -> dcycle 0x4109f8 (sem hook, ver item 3).

const T = (o) => base.add(o + 1);
const LEVELMGR = base.add(0x4958d0);   // ponteiro lido pelo gate (adr+literal no dmok, 0x410d90)
const HASHES = base.add(0x4a171c);     // hashes dos 9 tipos de pickup, preenchida em execução
const NAMES = ['InstaKill', 'DoublePoints', 'MaxAmmo', 'Nuke', 'Carpenter', 'FireSale', 'DeathMachine', '?7', 'Lightning'];
const hex = (p) => '0x' + p.toString(16);
const log = (s) => send(new Date().toISOString().substr(11, 12) + ' ' + s);

function power() {
    const lm = LEVELMGR.readPointer();
    return lm.isNull() ? 'lm=null' : 'energia(+23d)=' + lm.add(0x23d).readU8();
}
function typeOf(hash) {
    for (let i = 0; i < 9; i++) if (HASHES.add(i * 4).readU32() === (hash >>> 0)) return i + ' ' + NAMES[i];
    return '? hash=' + hex(hash >>> 0);
}

// 1. latch: estado por controller, log só na mudança (roda todo frame)
const seen = {};
Interceptor.attach(T(0x211120), function () {
    const c = this.context.r4, k = c.toString();
    const v = 'qr=' + (this.context.r0.toInt32() & 0xff) + ' +2bc=' + c.add(0x2bc).readU8() + ' +2bd=' + c.add(0x2bd).readU8();
    if (seen[k] !== v) { seen[k] = v; log('latch ctrl=' + c + ' ' + v); }
});
Interceptor.attach(T(0x410c50), function () {
    log('perk concedido id=' + this.context.r1.toInt32() + (this.context.r1.toInt32() === 5 ? ' (Quick Revive)' : ''));
});

// 2. ApplyEffect: gate do tipo 6 = dmok (energia ligada ou flag "teve QR na partida" em 0x410dd0)
const FLAG = base.add(0x410dd0);
const qr = () => 'flagQR=' + FLAG.readU8();
Interceptor.attach(T(0x410ca0), function () {
    const t = this.context.r2.toInt32();
    if (t !== 6) log('ApplyEffect tipo=' + t + ' ' + (NAMES[t] || '?'));
});
Interceptor.attach(T(0x410cae), function () {
    if (this.context.r2.toInt32() === 6) log('ApplyEffect DeathMachine ' + power() + ' ' + qr() + ' -> APLICOU');
});
Interceptor.attach(T(0x410cba), function () { log('ApplyEffect DeathMachine ' + power() + ' ' + qr() + ' -> BLOQUEOU'); });

// 3. sorteio: sem hooks no caminho do ChooseDropType/dcycle. Com o Interceptor na entrada do dcycle (0x4109f8) a
// pilha chegou 0x24 bytes deslocada em [sp+0x10]/[sp+0x14] e o jogo deu SIGSEGV em 0x410a2c (26/10).
let lastPower = null, lastQr = null;
setInterval(() => {
    const p = power(), q = qr();
    if (p !== lastPower || q !== lastQr) { lastPower = p; lastQr = q; log('estado ' + p + ' ' + q); }
}, 500);

log('diag_drops pronto, base=' + base + ' ' + power() + ' ' + qr());
