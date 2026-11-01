setTimeout(function () {
  const vt = base.add(0x41e4e0);
  const pat = '2b 72 65 6c 6f 61 64 43 68 61 6e 67 65 57 65 61 70 6f 6e 00';
  function g(v) { const o = v.sub(base); return (o.compare(ptr(0)) >= 0 && o.compare(ptr(0x460000)) < 0) ? 'g+' + o.toString(16) : v.toString(); }
  for (const r of Process.enumerateRanges('rw-')) {
    if (r.size > 0x8000000) continue;
    let hits; try { hits = Memory.scanSync(r.base, r.size, pat); } catch (e) { continue; }
    for (const h of hits) {
      const o = h.address.sub(4);
      let v; try { v = o.readPointer(); } catch (e) { continue; }
      if (!v.equals(vt)) continue;
      const w = []; for (let i = 0; i < 24; i++) w.push(g(o.add(i * 4).readPointer()));
      send('obj ' + o + ': ' + w.join(' '));
    }
  }
  const vw = []; for (let i = 0; i < 10; i++) vw.push(g(vt.add(i * 4).readPointer()));
  send('vtable: ' + vw.join(' '));
  send('fim');
}, 10);
