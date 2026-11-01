// quem grava CLevelManager+0x204 (fase atual): marca cada str #0x204 e mostra a pilha de chamadas
const SITES = [0x54872,0x71528,0x7b438,0x7b488,0x7f874,0x146e18,0x146f38,0x1484bc,0x148fbc,0x18ff34,0x1972a6,0x197be0,
               0x1dd57a,0x1de70c,0x204a86,0x20b92c,0x20d8b4,0x21511a,0x257764,0x25ad42];
for (const s of SITES) {
    try {
        Interceptor.attach(base.add(s + 1), function () {
            const g = base.add(0x4958d0).readPointer();
            send('hit img+' + s.toString(16) + ' this=' + this.context.r4 + ' G=' + g + '\n  ' +
                 Thread.backtrace(this.context, Backtracer.FUZZY).slice(0, 8).map(a => 'img+' + a.sub(base)).join(' '));
        });
    } catch (e) { send('falhou ' + s.toString(16) + ' ' + e); }
}
send('pronto');
