setTimeout(function () {
  const p = base.add(0xa410b2c);
  const m = Process.findModuleByAddress(p);
  send(m ? m.name + '+' + p.sub(m.base).toString(16) : 'sem modulo ' + JSON.stringify(Process.findRangeByAddress(p)));
  send(Instruction.parse(p.sub(4)).toString());
  let q = p.sub(0x20); for (let i = 0; i < 16; i++) { const ins = Instruction.parse(q); send(q.sub(m.base).toString(16) + ' ' + ins); q = ins.next; }
}, 10);
