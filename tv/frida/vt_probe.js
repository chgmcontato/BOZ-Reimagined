const VTS = {sprite: 0x419bd0, button: 0x418a58, root: 0x419a00};
const counts = {}; let phase = 'off'; const seen = {};
for (const [n, vo] of Object.entries(VTS)) {
    const vt = base.add(vo);
    for (let i = 0; i < 90; i++) {
        const f = vt.add(i * 4).readPointer().sub(base).toInt32();
        if (f < 0 || f > 0x41d970) break;
        const key = n + '[' + i + '] 0x' + (f & ~1).toString(16);
        if (seen[f]) { seen[f].push(key); continue; }
        seen[f] = [key];
        try { Interceptor.attach(base.add(f), { onEnter() { if (phase !== 'off') { const k = phase + '|' + f; counts[k] = (counts[k] || 0) + 1; } } }); } catch (e) {}
    }
}
let hooks = 0;
recv('phase', function onp(m) { phase = m.v; send('fase ' + m.v + ' hooks=' + Object.keys(seen).length); if (m.v === 'dump') send({counts: counts, seen: seen}); recv('phase', onp); });
send("pronto, funcoes=" + Object.keys(seen).length);
