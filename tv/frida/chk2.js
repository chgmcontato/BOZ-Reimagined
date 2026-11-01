setTimeout(function () {
  function g(v) { const o = v.sub(base); return (o.compare(ptr(0)) >= 0 && o.compare(ptr(0x460000)) < 0) ? 'g+' + o.toString(16) : v.toString(16); }
  const P = ['0x6b35d988','0x6b35dd80','0x6b35d6d0','0x6b39f790'].map(ptr);
  for (const e of P) {
    const w = []; for (let i = 0; i < 28; i++) w.push(g(e.add(i * 4).readPointer()));
    send(e + ': ' + w.join(' '));
  }
  // vtables das classes de UI conhecidas
  send('vt CGui runtime = ' + base.add(0x42fc90));
}, 10);
