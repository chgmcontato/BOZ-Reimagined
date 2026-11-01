setTimeout(function () {
  const lib = Process.getModuleByName('libs3e_android.so');
  // LoaderThread.onMotionEvent(IIII) nativo: (id, x, y, ação)
  Interceptor.attach(lib.base.add(0x3cd14), { onEnter(a) {
    send({t: Date.now() % 100000, a2: a[2].toInt32(), a3: a[3].toInt32(), s0: this.context.sp.readS32(), s1: this.context.sp.add(4).readS32()}); } });
  send('escutando toques');
}, 10);
