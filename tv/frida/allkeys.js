setTimeout(function () {
  const lib = Process.getModuleByName('libs3e_android.so');
  const last = {}, seen = {};
  Interceptor.attach(lib.base.add(0x67598), { onEnter(a) { this.k = a[0].toInt32(); },
    onLeave(r) { const k = this.k, v = r.toInt32();
      if (!seen[k]) { seen[k] = 1; send('consulta ' + k); }
      if (last[k] !== v) { last[k] = v; send({t: Date.now() % 100000, k: k, v: v}); } } });
  send('escutando todas as teclas');
}, 10);
