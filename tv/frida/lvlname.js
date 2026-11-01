// quem grava o nome do mapa em CLevelManager+0x55 (strcpy 0x39dd88 / strncpy 0x39ae50 / strcat 0x39d898)
const dst = () => base.add(0x4958d0).readPointer().add(0x55);
for (const [off, nm] of [[0x39dd88, 'strcpy'], [0x39ae50, 'strncpy'], [0x39d898, 'strcat']]) {
    Interceptor.attach(base.add(off), {
        onEnter(a) {
            let g; try { g = dst(); } catch (e) { return; }
            if (!a[0].equals(g)) return;
            let s = '?'; try { s = a[1].readCString(); } catch (e) {}
            send(nm + ' -> nome="' + s + '" src=' + a[1] + '\n  ' +
                 Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 6).map(x => 'img+' + x.sub(base)).join(' '));
        }
    });
}
send('pronto');
