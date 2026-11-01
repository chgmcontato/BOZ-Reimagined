// todas as ocorrências do hash de Score_Points: mostra 8 words antes (procura a vtable)
const lo = base, hi = base.add(0x600000);
setTimeout(() => {
    let n = 0;
    for (const r of Process.enumerateRanges('rw-')) {
        if (r.size > 16 * 1024 * 1024) continue;
        let hits; try { hits = Memory.scanSync(r.base, r.size, 'dd 74 fa 5c'); } catch (e) { continue; }
        for (const m of hits) {
            if (n++ > 40) break;
            const w = [];
            for (let i = -12; i < 4; i++) {
                const v = ptr(m.address.add(i * 4).readU32());
                w.push(i + ':' + (v.compare(lo) > 0 && v.compare(hi) < 0 ? 'img+' + v.sub(base) : v));
            }
            send(m.address + ' ' + w.join(' '));
        }
    }
    send('fim ' + n);
}, 10);
