setTimeout(function () {
// acha typeinfo e vtable de classes IwUI e conta objetos vivos
const NAMES = {CIwUIButton: 0x4056cd, CIwUIElement: 0x405be2, CIwUIImage: 0x406128, CIwUILabel: 0x406135};
const rw = Process.enumerateRanges('rw-');
function pat(p) { const b = []; let v = p; for (let i = 0; i < 4; i++) { b.push(('0' + v.and(0xff).toString(16)).slice(-2)); v = v.shr(8); } return b.join(' '); }
function scanAll(p) { const out = []; for (const r of rw) { try { for (const h of Memory.scanSync(r.base, r.size, pat(p))) out.push(h.address); } catch (e) {} } return out; }
const res = {};
for (const [n, off] of Object.entries(NAMES)) {
    const name = base.add(off);
    const tis = scanAll(name).map(a => a.sub(4));          // typeinfo = {vptr, name, ...}
    const vts = [];
    for (const ti of tis) for (const a of scanAll(ti)) vts.push(a.add(4));   // vtable[-1] = typeinfo
    let objs = [];
    for (const vt of vts.slice(0, 2)) objs = objs.concat(scanAll(vt));
    res[n] = {typeinfo: tis.map(String), vtable: vts.map(String), objetos: objs.length, amostra: objs.slice(0, 6).map(String)};
}
send(res);
}, 10);
