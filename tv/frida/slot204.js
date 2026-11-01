// mostra o objeto global [img+0x4958d0] e o campo +0x204 (nulo no crash do moon) com o nome RTTI
function cls(p) {
    try { const vt = p.readPointer(), ti = vt.sub(4).readPointer(), nm = ti.add(4).readPointer().readCString();
          return nm + ' vt=img+' + vt.sub(base); } catch (e) { return '?'; }
}
let last = '';
setInterval(() => {
    let s;
    try { const g = base.add(0x4958d0).readPointer();
          if (g.isNull()) s = 'G nulo';
          else { const f = g.add(0x204).readPointer();
                 s = 'G=' + cls(g) + ' | +0x204=' + (f.isNull() ? 'NULO' : f + ' ' + cls(f)); }
    } catch (e) { s = 'erro ' + e; }
    if (s !== last) { send(s); last = s; }
}, 300);
