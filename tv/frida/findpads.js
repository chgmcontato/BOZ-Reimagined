setTimeout(function () {
  const vts = {CGui: base.add(0x42fc90), CDOUI: base.add(0x42ce80)};
  const found = [];
  for (const r of Process.enumerateRanges('rw-')) {
    if (r.size > 0x8000000) continue;
    for (const k in vts) {
      const p = vts[k]; const pat = [0,8,16,24].map(s => ('0' + ((p.toUInt32() >>> s) & 0xff).toString(16)).slice(-2)).join(' ');
      let hits; try { hits = Memory.scanSync(r.base, r.size, pat); } catch (e) { continue; }
      for (const h of hits) if (h.address.and(3).isNull()) found.push([k, h.address]);
    }
  }
  for (const [k, a] of found) {
    const w = []; for (let i = 0; i < 16; i++) w.push(a.add(i * 4).readU32().toString(16));
    send(k + ' ' + a + ' ' + w.join(' '));
  }
  send('total ' + found.length);
}, 10);
