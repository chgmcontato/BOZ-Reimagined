setTimeout(function () {
const TIS = {CIwUIButton: ptr('0x6ebf9db0'), CIwUIElement: ptr('0x6ebfa388'), CIwUIImage: ptr('0x6ebfad58'), CIwUILabel: ptr('0x6ebfae88')};
const rw = Process.enumerateRanges('rw-');
function pat(p) { const b = []; let v = p; for (let i = 0; i < 4; i++) { b.push(('0' + v.and(0xff).toString(16)).slice(-2)); v = v.shr(8); } return b.join(' '); }
function scanAll(p) { const out = []; for (const r of rw) { try { for (const h of Memory.scanSync(r.base, r.size, pat(p))) out.push(h.address); } catch (e) {} } return out; }
const res = {};
for (const [n, ti] of Object.entries(TIS)) {
    const vts = scanAll(ti).filter(a => { try { return a.sub(4).readU32() === 0; } catch (e) { return false; } }).map(a => a.add(4));
    const per = {};
    for (const vt of vts) { const o = scanAll(vt); if (o.length) per[vt] = o.length + ' ex:' + o.slice(0, 3).join(','); }
    res[n] = {vtables: vts.map(String), objetos: per};
}
send(res);
}, 10);
