setTimeout(function () {
  const lib = Process.getModuleByName('libs3e_android.so');
  const gs = lib.base.add(0x67598), seen = {};
  const W = {126:1, 201:1, 210:1, 15:1, 74:1, 75:1};
  Interceptor.attach(lib.base.add(0x77554), { onEnter(a) {
      if (!this.context.r12.equals(gs)) return;
      const k = a[0].toInt32(); if (!W[k]) return;
      const ra = this.context.sp.readPointer(); const key = k + ':' + ra;
      if (!seen[key]) { seen[key] = 1; send(k + ' <- g+' + ra.sub(base).toString(16)); } } });
  send('ok');
}, 10);
