// amostra o pc de cada thread várias vezes (onde o jogo está preso); offsets relativos à imagem do boz.s3e
const end = base.add(0x460000);
const hits = {};
for (let k = 0; k < 40; k++) {
    for (const t of Process.enumerateThreads()) {
        const pc = t.context.pc, lr = t.context.lr;
        const f = a => (a.compare(base) >= 0 && a.compare(end) < 0) ? 'img+' + a.sub(base).toString(16) : (DebugSymbol.fromAddress(a).toString());
        const key = t.id + ' ' + f(pc) + ' <- ' + f(lr);
        hits[key] = (hits[key] || 0) + 1;
    }
    Thread.sleep(0.05);
}
for (const [k, v] of Object.entries(hits).sort((a, b) => b[1] - a[1]).slice(0, 25)) send(v + '  ' + k);
