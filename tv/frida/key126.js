setTimeout(function () {
  const lib = Process.getModuleByName('libs3e_android.so');
  const hits = {};
  Interceptor.attach(lib.base.add(0x67598), { onEnter(a) { this.k = a[0].toInt32(); },
    onLeave(r) { const v = r.toInt32(); if (v) { const key = this.k + '=' + v; hits[key] = (hits[key] || 0) + 1; } } });
  setTimeout(() => send(hits), 5000);
}, 10);
