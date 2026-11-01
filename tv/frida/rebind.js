setTimeout(function () {
  const P = Memory.allocUtf8String('+reloadChangeWeapon'), M = Memory.allocUtf8String('-reloadChangeWeapon');
  globalThis.keep = [P, M];
  let done = false;
  Interceptor.attach(base.add(0x10e27b), { onEnter(a) {
    if (done) return; const b = a[0];
    if (b.readU32() !== 9 && b.readU32() !== 15) return;
    const w = []; for (let i = 0; i < 12; i++) w.push(b.add(i * 4).readU32().toString(16));
    send('antes ' + w.join(' '));
    b.writeU32(9);
    b.add(0x10).writeU32(0);
    b.add(0x14).writeU32(0x80541817); b.add(0x18).writeU32(0xe208e499);
    b.add(0x1c).writePointer(ptr(0)); b.add(0x20).writePointer(P); b.add(0x24).writePointer(M);
    const w2 = []; for (let i = 0; i < 12; i++) w2.push(b.add(i * 4).readU32().toString(16));
    send('depois ' + w2.join(' ')); done = true;
  } });
  send('ok');
}, 10);
