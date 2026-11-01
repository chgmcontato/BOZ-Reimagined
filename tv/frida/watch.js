setTimeout(function () {
  const o = ptr('0x6d4dd668').sub(0x30); let last = '';
  setInterval(function () {
    const s = '78=' + o.add(0x78).readS32() + ' 7c=' + o.add(0x7c).readS32() + ' 80=' + o.add(0x80).readU8() + ' 51=' + o.add(0x51).readU8() +
      ' 54=' + o.add(0x54).readS32() + ' 58=' + o.add(0x58).readU8() + ' 42=' + o.add(0x42).readU8() + ' 43=' + o.add(0x43).readU8() + ' 90=' + o.add(0x90).readS32();
    if (s !== last) { last = s; send(Date.now() % 100000 + ' ' + s); }
  }, 50);
  send('ok');
}, 10);
