setTimeout(function () {
  Interceptor.attach(base.add(0x10ec8d), { onEnter(a) {
    let s = ''; try { s = a[1].readCString(); } catch (e) {}
    send(Date.now() % 100000 + ' cmd ' + s + ' flag=' + a[2]);
  } });
  send('ok');
}, 10);
