setTimeout(function () {
  const lib = Process.getModuleByName('libs3e_android.so');
  const seen = {};
  Interceptor.attach(lib.base.add(0x67598), { onEnter(a) { const k = a[0].toInt32(); seen[k] = (seen[k] || 0) + 1; } });
  setTimeout(() => send(seen), 4000);
}, 10);
