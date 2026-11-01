setTimeout(function () {
  const L = ptr('0x6d4dd668'), vt = L.readPointer();
  const w = []; for (let i = 0; i < 8; i++) { const v = vt.add(i * 4).readPointer(); w.push(v.sub(base).toString(16)); }
  send('vt=g+' + vt.sub(base).toString(16) + ' fns ' + w.join(' '));
}, 10);
