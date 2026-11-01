setTimeout(function () {
  const near = Process.enumerateRanges('rw-').filter(r => r.base.compare(base) >= 0 && r.base.compare(base.add(0x800000)) < 0);
  send({base: String(base), rw_perto: near.map(r => [String(r.base.sub(base)), r.size])});
}, 10);
