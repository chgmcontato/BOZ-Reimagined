setTimeout(function () {
  const m = Process.getModuleByName('libs3e_android.so');
  let q = m.base.add(0x77950); for (let i = 0; i < 20; i++) { const ins = Instruction.parse(q); send(q.sub(m.base).toString(16) + ' ' + ins); q = ins.next; }
}, 10);
