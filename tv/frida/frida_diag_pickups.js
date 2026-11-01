/* BOZ Reimagined 1.0.11 — diagnóstico somente leitura.
 * APK final não contém Gadget. Para Frida é necessário dispositivo com frida-server
 * compatível ou build de teste FRIDA=1 (não confundir com o APK final).
 * adb connect <IP>:5555  (instalar-na-tv.sh já conecta; NÃO executar para só diagnosticar)
 * frida -U -f com.activision.boz -l build/frida/frida_diag_pickups.js
 * Gadget: adb forward tcp:27042 tcp:27042
 * python3 build/frida/run.py build/frida/frida_diag_pickups.js 60
 * Testes: 1 energia off; 2 on sem revive; 3 on com revive; 4 revive perdido;
 * 5 COTD matar George (7 e 8); 6 COTD ciclo normal (sem 8).
 * 'permitido' significa passou pelo gate, não confirma sucesso ao equipar a arma.
 */
(function () {
  let imageBase = typeof base !== 'undefined' ? base : null;
  const signature = '2d e9 f0 43 1c 46 66 4b c1 b0 07 46 89 46 7b 44';
  function install() {
    if (!imageBase) {
      for (const range of Process.enumerateRanges('r-x')) {
        try {
          const hits = Memory.scanSync(range.base, range.size, signature);
          if (hits.length) { imageBase = hits[0].address.sub(0x1d4ae0); break; }
        } catch (_) {}
      }
    }
    if (!imageBase) return false;
    const at = offset => imageBase.add(offset);
    const thumb = offset => at(offset).or(1);
    const controllerOf = new NativeFunction(thumb(0x20895c), 'pointer', ['pointer']);
    const choose = new Map(), george = new Map();
    const log = (kind, value) => send(Object.assign({kind: kind}, value));
    function state(entity) {
      const level = at(0x4958d0).readPointer();
      const controller = entity.isNull() ? ptr(0) : controllerOf(entity);
      const manager = controller.isNull() ? ptr(0) : controller.add(0xa0).readPointer();
      const power = level.isNull() ? null : level.add(0x23d).readU8();
      const latch = controller.isNull() ? null : controller.add(0x2bd).readU8();
      return {entity: entity.toString(), controller: controller.toString(), power: power,
        everRevive: latch, activeMirror: controller.isNull() ? null : controller.add(0x2bc).readU8(),
        activeMask: manager.isNull() ? null : manager.add(0x58).readU32(),
        decision: power !== 1 ? 'bloqueou energia' : !latch ? 'bloqueou revive' : 'permitido'};
    }
    function mapName() {
      const level = at(0x4958d0).readPointer();
      return level.isNull() ? null : level.add(0x55).readCString();
    }
    function fields(p) { return {romero: p.toString(), suppress: p.add(0x116).readU8(),
      rewardIssued: p.add(0x1b0).readU8()}; }
    Interceptor.attach(thumb(0x207d28), {onEnter(args) {
      if (args[2].toInt32() !== 6) return;
      try { log('death-machine-gate', state(args[1])); } catch(e) { log('error', {where:'ApplyEffect', error:String(e)}); }
    }});
    Interceptor.attach(thumb(0x142ce0), {
      onEnter(args) { this.romero = args[0]; this.previous = george.get(this.threadId);
        george.set(this.threadId, this.romero); log('george-enter',fields(this.romero)); },
      onLeave() { log('george-leave',fields(this.romero));
        if (this.previous) george.set(this.threadId,this.previous); else george.delete(this.threadId); }
    });
    Interceptor.attach(thumb(0x205e3c), {onEnter(args) {
      const romero = george.get(this.threadId);
      if (romero) log('george-special-request',Object.assign({index:args[1].toInt32()},fields(romero)));
    }});
    Interceptor.attach(thumb(0x205eb0), {
      onEnter() { this.previous = choose.get(this.threadId); choose.set(this.threadId,true); },
      onLeave() { if(this.previous) choose.set(this.threadId,this.previous); else choose.delete(this.threadId); }
    });
    // ChooseDropType returns void. Observe its actual spawn request, including override/cycle result.
    Interceptor.attach(thumb(0x205d70), {onEnter(args) {
      if (!choose.get(this.threadId)) return;
      try {
        const hash = args[1].toUInt32(); let type = null;
        for(let i=0;i<9;i++) if(at(0x4a171c+i*4).readU32()===hash) type=i;
        log('normal-drop-request',{map:mapName(),type:type,hash:hash});
      } catch(e) { log('error',{where:'ChooseDropType',error:String(e)}); }
    }});
    log('ready',{imageBase:imageBase.toString(),latch:'CPlayerController+0x2bd'});
    return true;
  }
  if (!install()) { const timer=setInterval(()=>{if(install())clearInterval(timer);},500); }
})();
