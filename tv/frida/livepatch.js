setTimeout(function () {
  const a = base.add(ADDR);
  const old = a.readByteArray(2);
  Memory.patchCode(a, 2, function (code) { code.writeU16(0x4770); });
  send('patched ' + a);
  setTimeout(function () { Memory.patchCode(a, 2, function (code) { code.writeByteArray(old); }); send('restored'); }, 4000);
}, 10);
