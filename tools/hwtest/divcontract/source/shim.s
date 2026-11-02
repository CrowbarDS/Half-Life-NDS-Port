@ void probeCall(routine, const uint32_t in[4], uint32_t out[6])
@ Calls `routine` as compiled code would, with the registers it must keep set
@ to a pattern. out: r0-r3 as it left them, how many of r4-r11 it changed, and
@ the interrupt-disable bit afterwards.
    .syntax unified
    .arch   armv5te
    .cpu    arm946e-s
    .arm
    .section .text.probe, "ax", %progbits
    .align  2
    .global probeCall
    .type   probeCall, %function
probeCall:
    push    {r4-r11, lr}
    push    {r2}
    mov     r12, r0
    ldm     r1, {r0-r3}
    ldr     r4, =0x44444444
    ldr     r5, =0x55555555
    ldr     r6, =0x66666666
    ldr     r7, =0x77777777
    ldr     r8, =0x88888888
    ldr     r9, =0x99999999
    ldr     r10, =0xAAAAAAAA
    ldr     r11, =0xBBBBBBBB
    blx     r12
    ldr     r12, [sp]
    stm     r12!, {r0-r3}
    mov     r0, #0
    ldr     r1, =0x44444444
    cmp     r4, r1
    addne   r0, r0, #1
    ldr     r1, =0x55555555
    cmp     r5, r1
    addne   r0, r0, #1
    ldr     r1, =0x66666666
    cmp     r6, r1
    addne   r0, r0, #1
    ldr     r1, =0x77777777
    cmp     r7, r1
    addne   r0, r0, #1
    ldr     r1, =0x88888888
    cmp     r8, r1
    addne   r0, r0, #1
    ldr     r1, =0x99999999
    cmp     r9, r1
    addne   r0, r0, #1
    ldr     r1, =0xAAAAAAAA
    cmp     r10, r1
    addne   r0, r0, #1
    ldr     r1, =0xBBBBBBBB
    cmp     r11, r1
    addne   r0, r0, #1
    mrs     r1, cpsr
    and     r1, r1, #0x80
    stm     r12, {r0, r1}
    add     sp, sp, #4
    pop     {r4-r11, lr}
    bx      lr
    .size   probeCall, . - probeCall
    .pool

@ uint32_t probeMask(uint32_t disable): sets the CPU's interrupt-disable bit as
@ asked, and says what it was.
    .global probeMask
    .type   probeMask, %function
probeMask:
    mrs     r2, cpsr
    bic     r1, r2, #0x80
    orr     r1, r1, r0
    msr     cpsr_c, r1
    and     r0, r2, #0x80
    bx      lr
    .size   probeMask, . - probeMask
