const NAMES = {"N7gameswf25button_character_instanceE": 3955345, "N7gameswf15sprite_instanceE": 3966099, "N7gameswf4rootE": 3965078, "N7gameswf19edit_text_characterE": 3967297, "N7gameswf17generic_characterE": 3959849, "N7gameswf6playerE": 3963213};
setTimeout(function () {
const rw = Process.enumerateRanges('rw-');
function pat(p) { const b = []; let v = p; for (let i = 0; i < 4; i++) { b.push(('0' + v.and(0xff).toString(16)).slice(-2)); v = v.shr(8); } return b.join(' '); }
function scanAll(p) { const out = []; for (const r of rw) { try { for (const h of Memory.scanSync(r.base, r.size, pat(p))) out.push(h.address); } catch (e) {} } return out; }
const res = {};
for (const [n, off] of Object.entries(NAMES)) {
    const tis = scanAll(base.add(off)).map(a => a.sub(4));
    const out = [];
    for (const ti of tis) {
        const vts = scanAll(ti).filter(a => { try { return a.sub(4).readU32() === 0; } catch (e) { return false; } }).map(a => a.add(4));
        for (const vt of vts) { const o = scanAll(vt); out.push({ti: String(ti), vt: String(vt), n: o.length, ex: o.slice(0, 4).map(String)}); }
    }
    res[n] = out;
}
send(res);
}, 10);
