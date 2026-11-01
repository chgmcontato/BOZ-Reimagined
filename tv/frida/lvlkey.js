// chave da busca do CLevel (0x26a250: mapa em this+0x44, chave r1 = CLevelManager+0x55) e o resultado
Interceptor.attach(base.add(0x26a250 + 1), {
    onEnter(a) {
        this.on = this.returnAddress.sub(base).toInt32() === 0x1dd56d;
        if (!this.on) return;
        const k = a[1];
        send('busca CLevel: chave @G+0x55 = ' + hexdump(k.sub(0x15), {length: 0x60, header: false, ansi: false}));
    },
    onLeave(r) { if (this.on) send('  resultado ' + r); }
});
send('pronto');
// pega o crash: pc/lr relativos à imagem do boz.s3e, registradores e pilha
send('base ' + base);
Process.setExceptionHandler(e => {
    const c = e.context, rel = p => { const o = ptr(p).sub(base); return o.compare(0x400000) < 0 ? 'img+' + o : String(p); };
    let regs = '';
    for (const r of ['r0','r1','r2','r3','r4','r5','r6','r7','r8','r9','r10','r11','r12','sp','lr','pc']) regs += r + '=' + rel(c[r]) + ' ';
    send('EXC ' + e.type + ' addr=' + e.address + ' mem=' + JSON.stringify(e.memory) + '\n' + regs);
    try {
        const sp = c.sp; let out = 'pilha:';
        for (let i = 0; i < 64; i++) { const v = sp.add(i * 4).readPointer(); const o = v.sub(base); if (o.compare(0x400000) < 0 && (o.toInt32() & 1)) out += ' ' + o; }
        send(out);
    } catch (x) { send('pilha falhou ' + x); }
    return false;
});
