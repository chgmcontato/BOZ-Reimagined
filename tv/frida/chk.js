setTimeout(function () {
  const E = ['0x6b35d7e8','0x6b35dbe0','0x6b35dfd8','0x6b35e3d0','0x6b39f5f0','0x6b39f9e8','0x6b39fde0','0x6b3a01d8'].map(ptr);
  for (const e of E) {
    // filhos: IwUI guarda ponteiro p/ lista? mostra palavras 0x30..0x80
    const w = []; for (let i = 0; i < 40; i++) w.push(e.add(i * 4).readU32().toString(16));
    send(e + ' +2e=' + e.add(0x2e).readU16().toString(16) + ' | ' + w.join(' '));
  }
}, 10);
