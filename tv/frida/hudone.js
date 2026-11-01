// versão leve: 1 nome (Score_Points), e sobe pelos ponteiros que apontam para outros elementos
const H = 0x5cfa74dd, lo = base, hi = base.add(0x500000);
function isObj(p) { try { const v = p.readPointer(); return v.compare(lo) > 0 && v.compare(hi) < 0; } catch (e) { return false; } }
function dump(o) {
    const w = [];
    for (let i = 0; i < 0x40; i++) {
        const v = o.add(i * 4).readU32(), p = ptr(v);
        w.push(v > 0x10000 && isObj(p) ? '@' + p.add(4).readU32().toString(16) + '=' + p : String(v | 0));
    }
    return w;
}
setTimeout(() => {
    const pat = 'dd 74 fa 5c';
    for (const r of Process.enumerateRanges('rw-')) {
        if (r.size > 16 * 1024 * 1024) continue;
        let hits; try { hits = Memory.scanSync(r.base, r.size, pat); } catch (e) { continue; }
        for (const m of hits) {
            const o = m.address.sub(4);
            if (!isObj(o)) continue;
            send('OBJ ' + o + ' vt=img+' + o.readPointer().sub(base) + ' | ' + dump(o).map((x, i) => i + ':' + x).join(' '));
        }
    }
    send('fim');
}, 10);
