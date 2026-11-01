import sys,json
from pathlib import Path
# Requires unicorn: use an environment with unicorn installed.
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import *
p=json.loads(Path(__file__).with_name('patches.json').read_text())
image=bytearray((Path(__file__).resolve().parents[3] / 'build/re/out/img.bin').read_bytes())
for a,old,new,label in p:image[a:a+len(bytes.fromhex(new))]=bytes.fromhex(new)
REGS=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R12]
CTRL=0x550000; LEVEL=0x540000; ENTITY=0x560000; MGR=0x570000; STACK=0x5f0000; RET=0x530000
count=0
def machine():
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.mem_map(0,0x600000);u.mem_write(0,bytes(image));u.reg_write(UC_ARM_REG_SP,STACK);u.reg_write(UC_ARM_REG_LR,RET|1)
 u.mem_write(0x4958d0,LEVEL.to_bytes(4,'little'));u.mem_write(MGR+0xc,ENTITY.to_bytes(4,'little'))
 return u
for power in [0,1]:
 for latch in [0,1]:
  for active in [0,1]:
   for typ in range(9):
    u=machine();u.mem_write(LEVEL+0x23d,bytes([power]));u.mem_write(CTRL+0x2bd,bytes([latch]));u.mem_write(CTRL+0x2bc,bytes([active]))
    vals=[MGR,ENTITY,typ,0x12,0x14,0x15,0x16,0x17,0x18,0x1c]
    for r,v in zip(REGS,vals):u.reg_write(r,v)
    stop=[]
    def h(uc,a,size,data):
     if a==0x20895c:uc.reg_write(UC_ARM_REG_R0,CTRL);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
     elif a in [0x207d2c,RET]:stop.append(a);uc.emu_stop()
    u.hook_add(UC_HOOK_CODE,h);u.emu_start(0x207d29,0,count=100)
    allow=typ!=6 or (power and latch)
    assert stop==[0x207d2c if allow else RET],(power,latch,typ,stop)
    for r,v in zip(REGS,vals):assert u.reg_read(r)==v
    assert u.reg_read(UC_ARM_REG_SP)==STACK-(24 if allow else 0)
    count+=1
for active in [0,1]:
 for before in [0,1]:
  u=machine();u.reg_write(UC_ARM_REG_R4,CTRL);u.reg_write(UC_ARM_REG_R0,active);u.reg_write(UC_ARM_REG_R1,0x123);u.mem_write(CTRL+0x2bd,bytes([before]));u.emu_start(0x21111d,0x211120,count=100)
  assert u.mem_read(CTRL+0x2bc,1)==bytes([active]);assert u.mem_read(CTRL+0x2bd,1)==bytes([active or before]);assert u.reg_read(UC_ARM_REG_R1)==0x123
  count+=1
u=machine();u.reg_write(UC_ARM_REG_R4,CTRL);u.reg_write(UC_ARM_REG_R5,0);u.mem_write(CTRL+0x2bc,b'\x01\x01');u.emu_start(0x20b9df,0x20b9e2,count=100);assert u.mem_read(CTRL+0x2bc,2)==b'\0\0';count+=1
for perk in range(7):
 u=machine();u.reg_write(UC_ARM_REG_R0,MGR);u.reg_write(UC_ARM_REG_R1,perk);u.reg_write(UC_ARM_REG_R2,1)
 def h(uc,a,size,data):
  if a==0x20895c:assert uc.reg_read(UC_ARM_REG_R0)==ENTITY;uc.reg_write(UC_ARM_REG_R0,CTRL);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
 u.hook_add(UC_HOOK_CODE,h);u.emu_start(0x201111,0x201114,count=100)
 assert u.mem_read(CTRL+0x2bd,1)==bytes([int(perk==5)]);assert u.reg_read(UC_ARM_REG_R0)==MGR;assert u.reg_read(UC_ARM_REG_R1)==perk;assert u.reg_read(UC_ARM_REG_R2)==1;count+=1
print('PASS:',count,'ARM emulation scenarios; registers and stack checked')
