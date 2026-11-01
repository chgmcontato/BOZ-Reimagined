// quem lê/grava o tático do jogador ([+0x1f0] item, [+0x1d0] quantidade) no Save & Quit / Continue
const SITES = '506e8 5089c 53582 53652 5366c 5367c 5384c 53880 5483e 54862 71510 7151c 7547e 754f4 759b2 759d0 75b20 7d5f6 7e69c 7e70e 7f79a 7f868 7fd5e 7fd62 91108 11335a 14845a 1484a4 14a24c 14a25e 14a8f4 14a948 18f3f6 18feee 1975e2 19767e 197684 1c3b98 1c4f60 1c4fd6 1cadf4 1d6db0 1d7abc 1d7f22 1d864c 1d8650 1d86a2 1d8b78 1f0586 20492c 2049f6 20d7a0 20d838 210abe 210b60 25132c 252eaa 252ee0 252ee6 252eec 252ef4 253e90 2540c4 254116 254124 25449e 2544a4 25558c 2555be 2555ce 255858 255866 25586c 25587c 255a96 255ba8 256502 256516 2567b2 2567b8 2570e0 257d42 257e74 25a9b2 25ac5e 25af14 25af28 25b110 '.trim().split(/\s+/).map(s => parseInt(s, 16));
const end = base.add(0x460000);
const rel = a => (a.compare(base) >= 0 && a.compare(end) < 0) ? 'g+' + a.sub(base).toString(16) : '?';
const seen = {};
for (const s of SITES) {
    try {
        Interceptor.attach(base.add(s + 1), function () {
            const k = s.toString(16) + ' <- ' + Thread.backtrace(this.context, Backtracer.FUZZY).slice(0, 4).map(rel).join(' ');
            if (!seen[k]) { seen[k] = 1; send(Date.now() % 1000000 + ' ' + k); }
        });
    } catch (e) { send('falhou ' + s.toString(16) + ' ' + e); }
}
send('pronto: ' + SITES.length + ' pontos');
