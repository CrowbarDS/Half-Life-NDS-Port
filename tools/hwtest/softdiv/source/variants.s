@ The routines the soft divide probe times: the game's hardware divide
@ (source/HwMath.s's, its profiling left out) and the divide without the
@ divider (HlSoftDiv.hpp) behind the same registers -- numerator in r1:r0,
@ divisor in r3:r2; quotient in r1:r0, remainder in r3:r2. All in ITCM, as
@ the game's are.

    .syntax unified
    .arch   armv5te
    .cpu    arm946e-s
    .arm

    .section .itcm.text.softdiv_probe, "ax", %progbits
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

@ The game's: the unit, interrupts held off by the CPU's own bit.
    .global divHw
    .type   divHw, %function
divHw:
    push    {r4, lr}
    mrs     r4, cpsr
    orr     r12, r4, #0x80
    msr     cpsr_c, r12
    mov     r12, #0x04000000
    add     r12, r12, #0x290
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
    pop     {r4, pc}

@ The divide without the divider, signed: softDivmodC (soft.arm.cpp) gives
@ the quotient and writes the remainder below the stack.
    .global divSoft
    .type   divSoft, %function
divSoft:
    push    {r4, lr}
    sub     sp, sp, #16
    add     r12, sp, #8
    str     r12, [sp]
    bl      softDivmodC
    ldr     r2, [sp, #8]
    ldr     r3, [sp, #12]
    add     sp, sp, #16
    pop     {r4, pc}

@ ... unsigned.
    .global divSoftU
    .type   divSoftU, %function
divSoftU:
    push    {r4, lr}
    sub     sp, sp, #16
    add     r12, sp, #8
    str     r12, [sp]
    bl      softUdivmodC
    ldr     r2, [sp, #8]
    ldr     r3, [sp, #12]
    add     sp, sp, #16
    pop     {r4, pc}
