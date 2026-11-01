// granada no HUD grande: visibilidade (flag 2, getter 0x3342dc) do grenade_button, do grenade_icon e dos filhos
const getFlag = new NativeFunction(base.add(0x3342dc), 'int', ['pointer', 'int']);
const vis = o => getFlag(o, 2);
let inIcon = false, done = 0;
Interceptor.attach(base.add(0x1cbe58 + 1), { onEnter() { inIcon = true; }, onLeave() { inIcon = false; } });
Interceptor.attach(base.add(0x3328b0), {
    onEnter(a) { this.n = inIcon ? a[1].readCString() : null; },
    onLeave(r) {
        if (!this.n || r.isNull() || done > 6) return;
        done++;
        const o = r, n = o.add(0x20).readS32(), kids = [];
        for (let i = 0; i < n && i < 16; i++) {
            const c = o.add(0x1c).readPointer().add(i * 4).readPointer();
            kids.push(c.add(4).readU32().toString(16) + ':v' + vis(c));
        }
        send(this.n + ' ' + o + ' v' + vis(o) + ' xy=' + o.add(0x34).readS32() + ',' + o.add(0x38).readS32() +
             ' filhos[' + n + '] ' + kids.join(' '));
    }
});
send('pronto');
