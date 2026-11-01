// busca do CLevel pelo nome do mapa em 0x1dd568 (bl 0x26a250 com r1 = G+0x55)
Interceptor.attach(base.add(0x1dd568 + 1), function () {
    const c = this.context; let nm = '?';
    try { nm = c.r1.readCString(); } catch (e) {}
    send('busca CLevel nome="' + nm + '" r0=' + c.r0 + ' r2=' + c.r2);
});
Interceptor.attach(base.add(0x1dd56c + 1), function () { send('  resultado r0=' + this.context.r0); });
// a função toda: entrou?
Interceptor.attach(base.add(0x1dd57a + 1), function () { send('  criou CLevel ' + this.context.r0); });
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
