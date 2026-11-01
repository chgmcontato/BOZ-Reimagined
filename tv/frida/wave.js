setTimeout(function () {
  const g = base.add(0x4958c4).readPointer();
  send({base: String(base), global: String(g), wave: g.isNull() ? null : g.add(0x78).readS32(),
        maps: Process.findRangeByAddress(base) ? Process.findRangeByAddress(base).protection + ' ' + Process.findRangeByAddress(base).base + ' size ' + Process.findRangeByAddress(base).size : null});
}, 10);
