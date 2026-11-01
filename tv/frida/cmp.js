setTimeout(function () {
  const vt = base.add(0x41e4e0);
  function g(v) { const o = v.sub(base); return (o.compare(ptr(0)) >= 0 && o.compare(ptr(0x460000)) < 0) ? 'g+' + o.toString(16) : v.toString(); }
  function dump(p, n) { const w = []; for (let i = 0; i < n; i++) { try { w.push(g(p.add(i * 4).readPointer())); } catch (e) { w.push('?'); } } return w.join(' '); }
  const names = ['+reloadChangeWeapon', '-reloadChangeWeapon', '+shoot', '-shoot', 'changeWeapon', '+actionSprint'];
  for (const r of Process.enumerateRanges('rw-')) {
    if (r.size > 0x8000000) continue;
    for (const nm of names) {
      const pat = Array.from(nm + '\0').map(c => ('0' + c.charCodeAt(0).toString(16)).slice(-2)).join(' ');
      let hits; try { hits = Memory.scanSync(r.base, r.size, pat); } catch (e) { continue; }
      for (const h of hits) { const o = h.address.sub(4); let v; try { v = o.readPointer(); } catch (e) { continue; }
        if (!v.equals(vt)) continue;
        const l = o.add(0x3c).readPointer();
        send(nm + ' obj ' + o + ' +24..: ' + dump(o.add(0x24), 8) + ' || lista ' + l + ': ' + (l.isNull() ? '' : dump(l, 12)));
      }
    }
  }
  send('fim');
}, 10);
