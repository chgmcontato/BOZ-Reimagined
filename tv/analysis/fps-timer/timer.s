// BOZ 1.0.11, Thumb-2; assembled at image offset 0x410000.
// No absolute runtime pointers: branches, ADR and singleton literals are PIC.
// State at 0x410700 (see generated manifest); wrappers preserve all arguments.
.thumb
.equ state, 0x410700
get_stats:
    ldr r0, stats_delta
stats_pc:
    add r0, pc
    ldr r0, [r0]
    bx lr
.align 2
stats_delta: .word 0xfeed0001

get_profile:
    ldr r0, save_delta
save_pc:
    add r0, pc
    ldr r0, [r0]
    cmp r0, #0
    it ne
    ldrne.w r0, [r0, #0xf4]
    bx lr
.align 2
save_delta: .word 0xfeed0002

// r0 = stat hash -> r0 = node. Uses the existing get-or-create, table 0.
get_node:
    push {r4, lr}
    sub sp, #8
    mov r4, r0
    bl get_stats
    mov r1, r0
    mov r3, r4
    movs r2, #0
    mov r0, sp
    bl 0x231ba2
    ldr r0, [sp]
    add sp, #8
    pop {r4, pc}

refresh:
    push {r4-r6, lr}
    adr.w r4, state
    bl get_stats
    cbz r0, refresh_missing
    movw r0, #0x63ee
    movt r0, #0xaba8
    bl get_node
    ldr r5, [r0, #0x14]
    movw r0, #0x203f
    movt r0, #0x8ce1
    bl get_node
    ldr r6, [r0, #0x14]
    str r5, [r4, #20]
    str r6, [r4, #24]
    bl get_profile
    str r0, [r4, #28]
    pop {r4-r6, pc}
refresh_missing:
    movs r0, #0
    str r0, [r4, #20]
    str r0, [r4, #24]
    pop {r4-r6, pc}

start:
    push {r4, lr}
    adr.w r4, state
    ldr r0, [r4, #8]
    cmp r0, #1
    beq start_done
    bl refresh
    blx 0x35d50
    str r0, [r4, #40]
    str r1, [r4, #44]
    movs r0, #0
    str r0, [r4, #12]
    str r0, [r4, #16]
    str r0, [r4, #36]
    str r0, [r4, #32]
    movs r0, #1
    str r0, [r4, #8]
start_done:
    pop {r4, pc}

// Played time only: adds the interval since the previous tick ([40,44] = last instant), except while the
// pause flag is set ([48], written by Hud.java on in-match menu screens) or the gap is > 250 ms
// (stalled frames: pause, loading). Save & Quit/results use the accumulated value ([12,16]).
tick:
    push {r4, lr}
    adr.w r4, state
    ldr r0, [r4, #8]
    cmp r0, #1
    bne tick_done
    // Continue: Hud.java leaves the saved played time in [52,56] and sets [60] = 1
    ldr r0, [r4, #60]
    cmp r0, #1
    bne tick_now
    ldr r0, [r4, #52]
    ldr r1, [r4, #56]
    str r0, [r4, #12]
    str r1, [r4, #16]
    movs r0, #0
    str r0, [r4, #60]
tick_now:
    blx 0x35d50
    ldr r2, [r4, #40]
    ldr r3, [r4, #44]
    str r0, [r4, #40]
    str r1, [r4, #44]
    subs r0, r0, r2
    sbcs r1, r1, r3
    bcc tick_done
    cmp r1, #0
    bne tick_done
    cmp.w r0, #250
    bhi tick_done
    ldr r2, [r4, #48]
    cmp r2, #0
    bne tick_done
    ldr r2, [r4, #12]
    ldr r3, [r4, #16]
    adds r2, r2, r0
    adc r3, r3, #0
    str r2, [r4, #12]
    str r3, [r4, #16]
tick_done:
    pop {r4, pc}

// Unsigned 64-bit division by 1000; quotient r1:r0, remainder r2.
// Software only: ARMv7 devices need not implement the IDIV extension.
div1000:
    movs r2, #0
    movs r3, #64
div_loop:
    lsls r0, r0, #1
    adcs r1, r1, r1
    adcs r2, r2, r2
    cmp.w r2, #1000
    blo div_next
    subw r2, r2, #1000
    orr r0, r0, #1
div_next:
    subs r3, r3, #1
    bne div_loop
    bx lr

finish:
    push {r4-r8, lr}
    adr.w r4, state
    ldr r0, [r4, #8]
    cmp r0, #1
    bne finish_done
    bl tick
    movs r0, #0
    str r0, [r4, #8]
    str r0, [r4, #40]
    str r0, [r4, #44]
    bl get_stats
    cbz r0, finish_no_stats
    mov r8, r0
    bl get_profile
    ldr r1, [r4, #28]
    cmp r0, r1
    bne finish_wrong_profile
    movw r0, #0x63ee
    movt r0, #0xaba8
    bl get_node
    mov r5, r0
    movw r0, #0x203f
    movt r0, #0x8ce1
    bl get_node
    mov r6, r0
    ldr r2, [r6, #0x14]
    // Treat malformed remainder as zero; never corrupt unrelated stats.
    cmp.w r2, #1000
    it hs
    movhs r2, #0
    ldr r0, [r4, #12]
    ldr r1, [r4, #16]
    // Continue: [52,56] = time restored from the save, already added to the total at Save & Quit
    ldr r3, [r4, #52]
    subs r0, r0, r3
    ldr r3, [r4, #56]
    sbcs r1, r1, r3
    bcs finish_base_ok
    movs r0, #0
    movs r1, #0
finish_base_ok:
    movs r3, #0
    str r3, [r4, #52]
    str r3, [r4, #56]
    adds r0, r0, r2
    adc r1, r1, #0
    bl div1000
    movw r7, #0xffff
    movt r7, #0x7fff
    cmp r1, #0
    bne finish_saturate
    ldr r3, [r5, #0x14]
    adds r0, r0, r3
    bcs finish_saturate
    cmp r0, r7
    bls finish_store
finish_saturate:
    mov r0, r7
finish_store:
    str r0, [r5, #0x14]
    str r2, [r6, #0x14]
    str r0, [r4, #20]
    str r2, [r4, #24]
    mov r0, r8
    movs r1, #0
    bl 0x229fd8
    str r0, [r4, #36]
finish_done:
    pop {r4-r8, pc}
finish_no_stats:
    movs r0, #5
    str r0, [r4, #36]
    b finish_done
finish_wrong_profile:
    movs r0, #6
    str r0, [r4, #36]
    b finish_done

reset:
    push {r4, lr}
    bl finish
    adr.w r4, state
    movs r0, #0
    str r0, [r4, #8]
    str r0, [r4, #12]
    str r0, [r4, #16]
    str r0, [r4, #32]
    str r0, [r4, #40]
    str r0, [r4, #44]
    str r0, [r4, #52]
    str r0, [r4, #56]
    str r0, [r4, #60]
    pop {r4, pc}

// Native Flash screen query, independent of PixelCopy/menu heuristics.
profile:
    push {r4-r6, lr}
    mov r4, r0
    adr.w r5, state
    adr.w r1, profile_name
    bl 0x1bcf08
    mov r6, r0
    mov r0, r4
    adr.w r1, stats_name
    bl 0x1bcf08
    orrs r0, r6
    it ne
    movne r0, #1
    str r0, [r5, #32]
    bl refresh
    pop {r4-r6, pc}
profile_name: .asciz "player_profile"
stats_name: .asciz "profile_stats"
.align 2

// Generated wrappers are appended here. State is fixed at 0x410700.
