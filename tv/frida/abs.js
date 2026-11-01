setTimeout(function () {
  const lib = Process.getModuleByName('libs3e_android.so');
  const t = lib.base.add(0x681e8 + 8 + 0x64c18);
  const out = {};
  for (let o = 0x1d0; o <= 0x220; o += 4) out['0x' + o.toString(16)] = t.add(o).readS32();
  send(out);
}, 10);
