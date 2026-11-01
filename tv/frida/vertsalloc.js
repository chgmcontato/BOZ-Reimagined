// CIwModelBlockVerts::Serialise (ARM 0x277fd8): estado do bloco na entrada e na saída
let n = 0;
const flagp = () => null;
Interceptor.attach(base.add(0x277fd8), {
    onEnter(a) { this.b = a[0]; },
    onLeave() {
        const b = this.b, cnt = b.add(0xe).readU16(), p = b.add(0x30).readPointer();
        n++;
        if (cnt > 2000 || p.isNull()) send('verts nelem ' + cnt + ' pos ' + p + ' bloco ' + b);
    }
});
const end = base.add(0x460000);
const rel = a => (a.compare(base) >= 0 && a.compare(end) < 0) ? 'img+' + a.sub(base).toString(16) : DebugSymbol.fromAddress(a).toString();
Process.setExceptionHandler(d => { send('EXC pc ' + rel(d.context.pc) + ' (blocos ' + n + ')'); return false; });
send('pronto');
