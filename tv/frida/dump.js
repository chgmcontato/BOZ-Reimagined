setTimeout(function () {
  const r = Process.findRangeByAddress(base.add(0x348c1c));
  send('range ' + (r ? r.base.sub(base).toString(16) + ' size ' + r.size.toString(16) + ' ' + r.protection : 'none'));
  send({b: Array.from(new Uint8Array(base.add(0x348a00).readByteArray(0x300))).map(x => ('0' + x.toString(16)).slice(-2)).join('')});
  send({c: Array.from(new Uint8Array(base.add(0x341900).readByteArray(0x300))).map(x => ('0' + x.toString(16)).slice(-2)).join('')});
}, 10);
