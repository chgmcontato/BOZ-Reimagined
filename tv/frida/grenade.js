// granada no HUD grande: registra a escolha do ícone (0x1cbe58) e o setter do CUIMultiElement (0x2449b2)
const nm = p => { try { return p.isNull() ? 'null' : p.readCString(); } catch (e) { return '?'; } };
let inIcon = false;
Interceptor.attach(base.add(0x1cbe58 + 1), {
    onEnter(a) {
        const o = a[0]; inIcon = true;
        send('icon: obj=' + o + ' [0x68]=' + o.add(0x68).readU8() + ' [0x64]=' + o.add(0x64).readPointer());
    },
    onLeave() { inIcon = false; }
});
// 0x3328b0: procura filho pelo nome (r1 = nome)
Interceptor.attach(base.add(0x3328b0), {
    onEnter(a) { this.n = inIcon ? nm(a[1]) : null; this.p = a[0]; },
    onLeave(r) { if (this.n) send('  find ' + this.n + ' em ' + this.p + ' -> ' + r); }
});
Interceptor.attach(base.add(0x2449b2 + 1), {
    onEnter(a) {
        const o = a[0], h = a[1].toUInt32();
        send('  set multi=' + o + ' hash=' + h.toString(16) + ' atual[0xdc]=' + o.add(0xdc).readS32());
        this.o = o;
    },
    onLeave() { send('  -> [0xdc]=' + this.o.add(0xdc).readS32()); }
});
send('pronto');
