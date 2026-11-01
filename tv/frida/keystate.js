setTimeout(function () {
  const lib = Process.getModuleByName('libs3e_android.so');
  const last = {};
  Interceptor.attach(lib.base.add(0x67598), { onEnter(a) { this.k = a[0].toInt32(); },
    onLeave(r) { const k = this.k, v = r.toInt32(); if (last[k] !== v) { last[k] = v; if (k == 126 || k == 75 || k == 74 || k == 78) send({t: Date.now() % 100000, k: k, v: v}); } } });
  send('escutando teclas');
}, 10);
