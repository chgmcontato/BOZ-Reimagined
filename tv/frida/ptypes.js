setTimeout(function () {
  const reg = base.add(0x4d9054).readPointer();
  if (reg.isNull()) { send('registro vazio'); return; }
  const arr = reg.readPointer(), n = reg.add(4).readS32();
  send('tipos: ' + n);
  const out = [];
  for (let i = 0; i < n; i++) {
    const h = arr.add(8 * i).readU32(), fn = arr.add(8 * i + 4).readPointer();
    let ser = '?', vt = '?';
    try {
      const create = new NativeFunction(fn, 'pointer', ['uint32', 'uint32']);
      const obj = create(0x12345678, 0);
      if (!obj.isNull()) { const v = obj.readPointer(); vt = v.sub(base).toString(16); ser = v.add(0x14).readPointer().sub(base).toString(16); }
    } catch (e) { ser = 'erro ' + e; }
    out.push(h.toString(16) + ' fn=' + fn.sub(base).toString(16) + ' vt=' + vt + ' ser=' + ser);
  }
  send(out.join('\n'));
}, 10);
