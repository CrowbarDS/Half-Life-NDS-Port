@ The ways of driving the divider and the square-root unit that main.cpp
@ times. All in ITCM, as the game's are, all with the game's calling
@ convention: (r1:r0) / (r3:r2) gives the quotient in r1:r0 and the remainder
@ in r3:r2; a root takes r1:r0 and gives r0.

    .syntax unified
    .arch   armv5te
    .cpu    arm946e-s
    .arm

    .set    REG_IME,     0x04000208
    .set    REG_DIVCNT,  0x04000280
    .set    REG_NUMER,   0x04000290
    .set    REG_SQRTCNT, 0x040002B0
    .set    REG_PARAM,   0x040002B8

    .section .itcm.text.divmath, "ax", %progbits
    .align  2

@ uint32_t probeTime(const void *routine, const uint32_t in[4], uint32_t out[4])
@ Bus-clock ticks of one call, on timer 0, with what the call left in r0-r3.
    .global probeTime
    .type   probeTime, %function
probeTime:
    push    {r4-r7, lr}
    mov     r4, r0
    mov     r6, r2
    ldr     r7, =0x04000100
    ldm     r1, {r0-r3}
    ldrh    r5, [r7]
    blx     r4
    ldrh    r4, [r7]
    stm     r6, {r0-r3}
    sub     r0, r4, r5
    lsl     r0, r0, #16
    lsr     r0, r0, #16
    pop     {r4-r7, pc}

@ For the time probeTime itself takes.
    .global probeNull
    .type   probeNull, %function
probeNull:
    bx      lr

@ What the game did until now.
    .global divOld
    .type   divOld, %function
divOld:
    push    {r4, r5}
    ldr     r12, =REG_DIVCNT
    ldr     r4, =REG_IME
    ldrh    r5, [r4]
    strh    r4, [r4]
    mov     r4, #2
    strh    r4, [r12]
    str     r0, [r12, #0x10]
    str     r1, [r12, #0x14]
    str     r2, [r12, #0x18]
    str     r3, [r12, #0x1C]
1:  ldrh    r4, [r12]
    tst     r4, #0x8000
    bne     1b
    ldr     r0, [r12, #0x20]
    ldr     r1, [r12, #0x24]
    ldr     r2, [r12, #0x28]
    ldr     r3, [r12, #0x2C]
    ldr     r4, =REG_IME
    strh    r5, [r4]
    pop     {r4, r5}
    bx      lr

@ STM and LDM, the mode looked at once the unit is running, IME as the guard.
    .global divStmIme
    .type   divStmIme, %function
divStmIme:
    push    {r4}
    ldr     r12, =REG_NUMER
    ldrh    r4, [r12, #-0x88]
    strh    r12, [r12, #-0x88]          @ (bit 0 of the address is clear: off)
    stm     r12!, {r0-r3}
    ldrh    r0, [r12, #-0x20]
    and     r0, r0, #3
    cmp     r0, #2
    movne   r0, #2
    strhne  r0, [r12, #-0x20]
1:  ldrh    r0, [r12, #-0x20]
    tst     r0, #0x8000
    bne     1b
    ldm     r12, {r0-r3}
    strh    r4, [r12, #-0x98]
    pop     {r4}
    bx      lr

@ The same, the CPU's own interrupt bit as the guard.
    .global divStmCpsr
    .type   divStmCpsr, %function
divStmCpsr:
    push    {r4}
    mrs     r4, cpsr
    orr     r12, r4, #0x80
    msr     cpsr_c, r12
    ldr     r12, =REG_NUMER
    stm     r12!, {r0-r3}
    ldrh    r0, [r12, #-0x20]
    and     r0, r0, #3
    cmp     r0, #2
    movne   r0, #2
    strhne  r0, [r12, #-0x20]
1:  ldrh    r0, [r12, #-0x20]
    tst     r0, #0x8000
    bne     1b
    ldm     r12, {r0-r3}
    msr     cpsr_c, r4
    pop     {r4}
    bx      lr

@ No guard, no stack: what a divide written inline would run, less the call.
    .global divBare
    .type   divBare, %function
divBare:
    ldr     r12, =REG_NUMER
    stm     r12!, {r0-r3}
    ldrh    r0, [r12, #-0x20]
    and     r0, r0, #3
    cmp     r0, #2
    movne   r0, #2
    strhne  r0, [r12, #-0x20]
1:  ldrh    r0, [r12, #-0x20]
    tst     r0, #0x8000
    bne     1b
    ldm     r12, {r0-r3}
    bx      lr

@ No polling at all: a count of idle loops set by probeWaitLoops. The unit is
@ left in 64-by-64 mode by whoever calls this.
    .global divWait
    .type   divWait, %function
divWait:
    ldr     r12, =REG_NUMER
    stm     r12!, {r0-r3}
    ldr     r0, =probeWaitLoops
    ldr     r0, [r0]
1:  subs    r0, r0, #1
    bne     1b
    ldm     r12, {r0-r3}
    bx      lr

@ What the game's root did until now.
    .global sqrtOld
    .type   sqrtOld, %function
sqrtOld:
    ldr     r12, =REG_SQRTCNT
    ldr     r2, =REG_IME
    ldrh    r3, [r2]
    strh    r2, [r2]
    str     r0, [r12, #0x08]
    str     r1, [r12, #0x0C]
    mov     r0, #1
    strh    r0, [r12]
1:  ldrh    r0, [r12]
    tst     r0, #0x8000
    bne     1b
    ldr     r0, [r12, #0x04]
    strh    r3, [r2]
    bx      lr

@ STM, the mode looked at once the unit is running, the CPU's interrupt bit.
    .global sqrtStmCpsr
    .type   sqrtStmCpsr, %function
sqrtStmCpsr:
    mrs     r3, cpsr
    orr     r2, r3, #0x80
    msr     cpsr_c, r2
    ldr     r12, =REG_PARAM
    stm     r12, {r0, r1}
    ldrh    r0, [r12, #-8]
    tst     r0, #1
    moveq   r0, #1
    strheq  r0, [r12, #-8]
1:  ldrh    r0, [r12, #-8]
    tst     r0, #0x8000
    bne     1b
    ldr     r0, [r12, #-4]
    msr     cpsr_c, r3
    bx      lr

@ int hwIdiv(int, int), in the unit's 32-by-32 mode. The remainder in r1, so
@ this one is __aeabi_idivmod too.
    .global hwIdiv
    .type   hwIdiv, %function
hwIdiv:
    mrs     r3, cpsr
    orr     r2, r3, #0x80
    msr     cpsr_c, r2
    ldr     r12, =REG_NUMER
    str     r0, [r12]
    str     r1, [r12, #8]
    ldrh    r0, [r12, #-0x10]
    tst     r0, #3
    movne   r0, #0
    strhne  r0, [r12, #-0x10]
1:  ldrh    r0, [r12, #-0x10]
    tst     r0, #0x8000
    bne     1b
    ldr     r0, [r12, #0x10]
    ldr     r1, [r12, #0x18]
    msr     cpsr_c, r3
    bx      lr

    .pool
