setTimeout(function () {
const NAMES = {"N7gameswf25button_character_instanceE": 3955345, "N7gameswf15sprite_instanceE": 3966099, "N7gameswf4rootE": 3965078, "N7gameswf9characterE": 3955698, "N7gameswf19edit_text_characterE": 3967297, "11CIwUIButton": 4216525, "12CIwUIElement": 4217826, "15CIsFlashMovie": 0, "15CFlashMenuState": 4071166};
const near = Process.enumerateRanges('r--').filter(r => r.base.compare(base.sub(0x1000)) >= 0 && r.base.compare(base.add(0x800000)) < 0);
function pat(p) { const b = []; let v = p; for (let i = 0; i < 4; i++) { b.push(('0' + v.and(0xff).toString(16)).slice(-2)); v = v.shr(8); } return b.join(' '); }
function scan(p) { const out = []; for (const r of near) { try { for (const h of Memory.scanSync(r.base, r.size, pat(p))) out.push(h.address); } catch (e) {} } return out; }
const res = {};
for (const [n, off] of Object.entries(NAMES)) {
  const tis = scan(base.add(off)).map(a => a.sub(4));
  const vts = [];
  for (const ti of tis) for (const a of scan(ti)) { try { if (a.sub(4).readU32() === 0) vts.push(a.add(4)); } catch (e) {} }
  res[n] = {ti: tis.map(t => '0x' + t.sub(base).toString(16)), vt: vts.map(v => '0x' + v.sub(base).toString(16))};
}
send(res);
}, 10);
