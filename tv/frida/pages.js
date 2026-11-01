// páginas de UI abertas (0x244f64 / r1 = nome) e quem chamou
Interceptor.attach(base.add(0x244f64 + 1), function (a) {
    let n = '?'; try { n = a[1].readCString(); } catch (e) {}
    send('pagina ' + n + ' de img+' + this.returnAddress.sub(base));
});
send('pronto');
