setTimeout(function () {
  const seen = {};
  function s(p) { try { return p.isNull() ? '' : p.readCString(); } catch (e) { return '?'; } }
  Interceptor.attach(base.add(0x10e27b), { onEnter(a) {
    const b = a[0], k = b.toString(); if (seen[k]) return; seen[k] = 1;
    const w = []; for (let i = 0; i < 12; i++) w.push(b.add(i * 4).readU32().toString(16));
    send(k + ' [' + w.join(' ') + '] p=' + s(b.add(0x1c).readPointer()) + ' r=' + s(b.add(0x20).readPointer()) + ' x=' + s(b.add(0x24).readPointer()));
  } });
  send('ok');
}, 10);
