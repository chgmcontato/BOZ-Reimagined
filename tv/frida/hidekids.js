setTimeout(function () {
  const E = ['0x6b35d7e8','0x6b35dbe0','0x6b35dfd8','0x6b35e3d0','0x6b39f5f0','0x6b39f9e8','0x6b39fde0','0x6b3a01d8'].map(ptr);
  for (const e of E) {
    const node = e.add(0x1c).readPointer();
    const k = node.readPointer();
    const f = k.add(0x2e).readU16();
    k.add(0x2e).writeU16(f & ~2);
    send(e + ' filho ' + k + ' vt g+' + k.readPointer().sub(base).toString(16) + ' +2e ' + f.toString(16) + ' -> ' + k.add(0x2e).readU16().toString(16));
  }
}, 10);
