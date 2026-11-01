// pega o crash: pc/lr relativos à imagem do boz.s3e, registradores e pilha
send('base ' + base);
Process.setExceptionHandler(e => {
    const c = e.context, rel = p => { const o = ptr(p).sub(base); return o.compare(0x400000) < 0 ? 'img+' + o : String(p); };
    let regs = '';
    for (const r of ['r0','r1','r2','r3','r4','r5','r6','r7','r8','r9','r10','r11','r12','sp','lr','pc']) regs += r + '=' + rel(c[r]) + ' ';
    send('EXC ' + e.type + ' addr=' + e.address + ' mem=' + JSON.stringify(e.memory) + '\n' + regs);
    try {
        const sp = c.sp; let out = 'pilha:';
        for (let i = 0; i < 256; i++) { const v = sp.add(i * 4).readPointer(); const o = v.sub(base); if (o.compare(0x400000) < 0 && (o.toInt32() & 1)) out += ' ' + o; }
        send(out);
    } catch (x) { send('pilha falhou ' + x); }
    return false;
});
