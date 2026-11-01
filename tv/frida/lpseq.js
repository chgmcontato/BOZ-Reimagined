setTimeout(function () {
  const C = [0x1c99bc, 0x1c974c, 0x1aac44, 0x1aa904];
  let old = null, a = null;
  function loop() {
    recv(function (m) {
      if (m.type === 'on') { a = base.add(C[m.k]); old = a.readByteArray(2);
        Memory.patchCode(a, 2, function (c) { c.writeU16(0x4770); }); send('on ' + C[m.k].toString(16)); }
      else if (m.type === 'off') { Memory.patchCode(a, 2, function (c) { c.writeByteArray(old); }); send('off'); }
      loop();
    });
  }
  loop(); send('pronto');
}, 10);
