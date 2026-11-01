// acha elementos do HUD na memória (vtable na imagem do jogo + hash do nome em +4) e despeja os campos;
// ponteiros para outros elementos saem como @hash (o python resolve os nomes)
const HASHES = HUD_HASHES;   // preenchido pelo run_hud.py
const lo = base, hi = base.add(0x500000);
function isObj(p) {
    try { const vt = p.readPointer(); return vt.compare(lo) > 0 && vt.compare(hi) < 0; } catch (e) { return false; }
}
setTimeout(() => {
    const out = [];
    const ranges = Process.enumerateRanges('rw-');
    for (const h of HASHES) {
        const pat = [0, 8, 16, 24].map(s => ('0' + ((h >>> s) & 255).toString(16)).slice(-2)).join(' ');
        for (const r of ranges) {
            if (r.size > 64 * 1024 * 1024) continue;
            let hits; try { hits = Memory.scanSync(r.base, r.size, pat); } catch (e) { continue; }
            for (const m of hits) {
                const o = m.address.sub(4);
                if (!isObj(o)) continue;
                const words = [];
                for (let i = 0; i < 0x60; i++) {
                    const v = o.add(i * 4).readU32();
                    let s = v.toString();
                    const p = ptr(v);
                    if (v > 0x10000 && isObj(p)) { try { s = '@' + p.add(4).readU32().toString(16); } catch (e) {} }
                    words.push(s);
                }
                out.push({h: h.toString(16), at: o.toString(), vt: o.readPointer().sub(base).toString(), w: words});
            }
        }
    }
    send(JSON.stringify(out));
}, 10);
