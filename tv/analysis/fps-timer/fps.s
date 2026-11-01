// Independent FPS cap at image 0x410600; state at 0x410680.
// Replace only the free-step s3eDeviceYield(0) at 0xfbc36..0xfbc3c.
// Native free-step scheduling stays intact. Yield only the remaining budget.
.thumb
.equ fps_state, 0x410680
    push {r0-r4, lr}
    adr.w r4, fps_state
    movs r0, #0
    blx 0x359a4
fps_poll:
    blx 0x35d50
    ldr r2, [r4, #12]
    cbz r2, fps_store
    ldr r2, [r4]
    ldr r3, [r4, #4]
    subs r2, r0, r2
    sbcs r3, r1, r3
    bcc fps_store
    cbnz r3, fps_store
    ldr r3, [r4, #8]
    cmp r3, #2
    ite eq
    moveq r3, #34
    movne r3, #33
    cmp r2, r3
    bhs fps_store
    subs r0, r3, r2
    blx 0x359a4
    b fps_poll
fps_store:
    str r0, [r4]
    str r1, [r4, #4]
    ldr r2, [r4, #8]
    adds r2, r2, #1
    cmp r2, #3
    it hs
    movhs r2, #0
    str r2, [r4, #8]
    movs r2, #1
    str r2, [r4, #12]
    pop {r0-r4, pc}
