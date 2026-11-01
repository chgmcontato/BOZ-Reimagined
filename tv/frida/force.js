const lib = Process.getModuleByName('libs3e_android.so');
let force = {};
Interceptor.attach(lib.base.add(0x67598), { onEnter(a) { this.k = a[0].toInt32(); },
  onLeave(r) { if (force[this.k]) r.replace(ptr(force[this.k])); } });
recv('f', function on(m) { force = m.v; send('force ' + JSON.stringify(force)); recv('f', on); });
send('pronto');
