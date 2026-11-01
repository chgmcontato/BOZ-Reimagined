// texto dos erros do motor (s3eDebugErrorShow / s3eDebugAssertShow) antes de cair
for (const n of ['s3eDebugErrorShow', 's3eDebugAssertShow', 's3eDebugErrorPrintf', 's3eDebugTracePrintf']) {
    const f = Process.getModuleByName('libs3e_android.so').findExportByName(n);
    if (!f) continue;
    Interceptor.attach(f, {
        onEnter(a) {
            let s = [];
            for (let i = 0; i < 3; i++) { try { const t = a[i].readUtf8String(); if (t && t.length > 2) s.push(t); } catch (e) {} }
            send(n + ': ' + s.join(' | ').slice(0, 600));
        }
    });
}
Process.setExceptionHandler(d => { send('EXC ' + DebugSymbol.fromAddress(d.context.pc)); return false; });
send('pronto');
