#!/usr/bin/env python3
"""Zumbi preso: monta o código da mecânica anti-soft-lock (manutenção; a build usa os bytes já gerados).

Pedido do usuário (26/10): zumbi que não consegue chegar ao jogador por 15 s é morto, e a morte conta para o
round (o BO1 da Steam faz isso com 30 s). Regra: o cronômetro só corre nos estados "andando" do zumbi (+0xfd = 2
ou 16, os mesmos que o próprio vf27 trata); se nesse tempo ele não sair de um raio de 64 unidades da posição de
referência, recebe dano igual à vida + 1000 pelo CHealth do jogo (morte normal, conta na onda).

Hook: entrada vf27 da vtable do CAIControllerZombie (0x423744, update por quadro, this = controller) -> stuck.
Tabela de 32 entradas de 24 bytes em 0x40f200: controller, visto por último, início, x, y, z (posição de ref.).
Funções do jogo: s3eTimerGetMs 0x35d50 (ARM, blx), CIsTransform::sub_f7474(entidade) -> transform (+0x5c x,
+0x60 y, +0x64 z), CHealth::sub_1ce7a4(entidade) -> CHealth (+0x48 vida, float), CHealth::sub_1cea14(vida, dano).
Uso: python3 assemble_stuck.py  (imprime a entrada CODE para o patch_s3e.py)"""
import struct

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB

CODE, TABLE, VTSLOT, VF27 = 0x410de0, 0x40f200, 0x423744, 0x147cec
BIAS = 0x4a000000 - 0x35090
ENTRIES, SIZE = 32, 24
LIMIT_MS, RADIUS2 = 15000, 64.0 * 64.0

SRC = f"""
stuck:
    push {{r4, r5, r6, r7, lr}}
    mov r4, r0
    blx #0x35d50
    mov r6, r0                      @ agora (ms, 32 bits)
    ldr r5, lit_table
tbl_pc:
    add r5, pc                      @ r5 = tabela
    @ procura a entrada do controller; se nao ha, usa a primeira livre ou parada ha mais de 2 s
    movs r3, #0                     @ r3 = candidata livre
    movs r2, #{ENTRIES}
find:
    ldr r1, [r5]
    cmp r1, r4
    beq found
    cbnz r3, next
    cbz r1, take
    ldr r1, [r5, #4]
    subs r1, r6, r1
    cmp.w r1, #2000
    bls next
take:
    mov r3, r5
next:
    adds r5, #{SIZE}
    subs r2, #1
    bne find
    cmp r3, #0
    beq done                    @ tabela cheia: nao acompanha este zumbi
    mov r5, r3
    str r4, [r5]
    movs r1, #0
    str r1, [r5, #4]                @ visto ha "muito tempo": a referencia e refeita abaixo
found:
    ldr r0, [r4, #0xc]
    cmp r0, #0
    beq done
    bl #0xf7474
    movs r7, r0                     @ transform
    beq done
    ldr r1, [r5, #4]
    str r6, [r5, #4]
    subs r1, r6, r1
    cmp.w r1, #1000
    bhi reset                       @ nao visto no ultimo segundo (zumbi novo ou reaproveitado)
    ldrb.w r1, [r4, #0xfd]
    cmp r1, #2
    beq moving
    cmp r1, #16
    bne reset                       @ janela, ataque, morte...: nao conta
moving:
    vldr s0, [r7, #0x5c]
    vldr s1, [r7, #0x60]
    vldr s2, [r7, #0x64]
    vldr s3, [r5, #12]
    vldr s4, [r5, #16]
    vldr s5, [r5, #20]
    vsub.f32 s3, s0, s3
    vsub.f32 s4, s1, s4
    vsub.f32 s5, s2, s5
    vmul.f32 s3, s3, s3
    vmla.f32 s3, s4, s4
    vmla.f32 s3, s5, s5
    movw r1, #{struct.unpack('<I', struct.pack('<f', RADIUS2))[0] & 0xffff}
    movt r1, #{struct.unpack('<I', struct.pack('<f', RADIUS2))[0] >> 16}
    vmov s6, r1
    vcmpe.f32 s3, s6
    vmrs APSR_nzcv, fpscr
    bgt reset                       @ andou: nova referencia
    ldr r1, [r5, #8]
    subs r1, r6, r1
    movw r2, #{LIMIT_MS}
    cmp r1, r2
    bls done
    @ preso: dano = vida + 1000 pelo CHealth (morte normal do jogo)
    ldr r0, [r4, #0xc]
    bl #0x1ce7a4
    cbz r0, reset
    vldr s0, [r0, #0x48]
    movw r1, #0
    movt r1, #0x447a                @ 1000.0
    vmov s1, r1
    vadd.f32 s0, s0, s1
    vmov r1, s0
    bl #0x1cea14
reset:
    str r6, [r5, #8]
    ldr r0, [r7, #0x5c]
    str r0, [r5, #12]
    ldr r0, [r7, #0x60]
    str r0, [r5, #16]
    ldr r0, [r7, #0x64]
    str r0, [r5, #20]
done:
    mov r0, r4
    pop.w {{r4, r5, r6, r7, lr}}
    b.w #{VF27:#x}
.align 2
lit_table:
    .word 0x11111111
"""


def main():
    # cbz so alcanca 126 bytes para frente e o keystone encurta sem avisar: saltos longos usam cmp + beq
    ks = Ks(KS_ARCH_ARM, KS_MODE_THUMB)
    code = bytearray(ks.asm(SRC, CODE)[0])
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    insns = list(md.disasm(bytes(code), CODE))
    add = [i for i in insns if i.mnemonic == 'add' and i.op_str == 'r5, pc']
    assert len(add) == 1
    pos = code.index(struct.pack('<I', 0x11111111))
    code[pos:pos + 4] = struct.pack('<I', (TABLE - (add[0].address + 4)) & 0xffffffff)
    for i in md.disasm(bytes(code[:pos]), CODE):
        print('  %#x %-10s %s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
    assert CODE + len(code) <= 0x411090
    vt_old = struct.pack('<I', BIAS + VF27 + 1)
    vt_new = struct.pack('<I', BIAS + CODE + 1)
    print('    (%#x, bytes(%d), bytes.fromhex("%s")),' % (CODE, len(code), code.hex()))
    print('    (%#x, bytes.fromhex("%s"), bytes.fromhex("%s")),   # vtable vf27 -> stuck' % (VTSLOT, vt_old.hex(), vt_new.hex()))


if __name__ == '__main__':
    main()
