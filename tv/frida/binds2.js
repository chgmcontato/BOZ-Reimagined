setTimeout(function () {
  const seen = {};
  function s(p) { try { return p.isNull() ? '' : p.readCString(); } catch (e) { return '?'; } }
  Interceptor.attach(base.add(0x10e305), { onEnter(a) {
    const b = a[0], k = b.toString() + ':' + a[1]; if (seen[k]) return; seen[k] = 1;
    const w = []; for (let i = 0; i < 4; i++) w.push(b.add(i * 4).readU32().toString(16));
    send('ctx=' + a[1].toString(16) + ' key=' + w[0] + ' b4=' + w[1] + ' mask=' + w[2] + ' d=' + w[3] + ' ' + s(b.add(0x1c).readPointer()) + s(b.add(0x20).readPointer()));
  } });
  send('ok');
}, 10);
