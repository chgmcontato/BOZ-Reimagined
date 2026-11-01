// fica no jogo; no crash informa pc/lr/registradores relativos à imagem do boz.s3e e a pilha
const end = base.add(0x460000);
const rel = a => (a.compare(base) >= 0 && a.compare(end) < 0) ? 'img+' + a.sub(base).toString(16) : DebugSymbol.fromAddress(a).toString();
Process.setExceptionHandler(d => {
    const c = d.context;
    send('EXC ' + d.type + ' addr ' + d.address + ' pc ' + rel(c.pc) + ' lr ' + rel(c.lr));
    send('regs ' + ['r0','r1','r2','r3','r4','r5','r6','r7','r8','r9','r10','r11','r12','sp'].map(r => r + '=' + c[r]).join(' '));
    try {
        send('bt ' + Thread.backtrace(c, Backtracer.FUZZY).map(rel).join(' | '));
    } catch (e) { send('bt err ' + e); }
    // varre a pilha procurando endereços de retorno dentro da imagem
    const out = [];
    for (let i = 0; i < 256 && out.length < 24; i++) {
        try {
            const v = c.sp.add(i * 4).readPointer();
            if (v.compare(base) >= 0 && v.compare(end) < 0) out.push('img+' + v.sub(base).toString(16));
        } catch (e) { break; }
    }
    send('stack ' + out.join(' '));
    return false;
});
send('pronto, base ' + base);
