setTimeout(function () {
  const seen = {};
  Interceptor.attach(base.add(0x10e225), { onEnter(a) {
    let s = ''; try { s = a[1].readCString(); } catch (e) {}
    if (seen[s]) return; seen[s] = 1;
    const o = a[0], w = [];
    for (let i = 0; i < 10; i++) { const v = o.add(i * 4).readPointer(); const off = v.sub(base);
      w.push(off.compare(ptr(0)) >= 0 && off.compare(ptr(0x460000)) < 0 ? 'g+' + off.toString(16) : v.toString()); }
    send(s + ' obj=' + o + ' [' + w.join(' ') + ']');
  } });
  send('ok');
}, 10);
