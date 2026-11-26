@ The routines the divide tricks probe times (Kuratius's ideas, 2026-11-26),
@ all in ITCM as the game's are, each behind the registers probeTime gives
@ it (in[0..3] in r0-r3) and takes back (out[0..3] from r0-r3).
@
@ The "count" is the dirty count: a word in DTCM a user of a unit adds 1 to
@ before it writes the unit, and looks at again after it has read it. If it
@ has moved, someone used the unit in between: the answer is theirs, so do
@ it again. The mode is always written, so that the time a sum takes never
@ hangs on what the last user left.

    .syntax unified
    .arch   armv5te
    .cpu    arm946e-s
    .arm

    @ From 0x04000280 (the divider) and 0x040002B0 (the root).
    .set    DIV_NUMER, 0x10
    .set    DIV_RESULT, 0x20
    .set    SQRT_RESULT, 0x04
    .set    SQRT_PARAM, 0x08

    .section .sbss.divtricks, "aw", %nobits
    .align  2
    .global divCount, sqrtCount, divDelay, sqrtDelay, rootWait
divCount:   .space 4
sqrtCount:  .space 4
divDelay:   .space 4
sqrtDelay:  .space 4
rootWait:   .space 4

    .section .itcm.text.divtricks, "ax", %progbits
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

    .global probeNull
    .type   probeNull, %function
probeNull:
    bx      lr

@ ---- The divider, 64 by 32 bits -----------------------------------------

@ The game's hardware divide before the soft one: 64 by 64, interrupts held
@ off by the CPU's own bit, the mode written only if it differs.
    .global divHwOld
    .type   divHwOld, %function
divHwOld:
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

@ (r1:r0) / r2, signed (r3 not looked at): quotient in r1:r0, remainder in
@ r3:r2, as __aeabi_ldivmod's. The busy bit polled, the count kept.
    .global divPollCount
    .type   divPollCount, %function
divPollCount:
    push    {r4-r6, lr}
    mov     r6, r2
    ldr     r12, =divCount
    mov     lr, #0x04000000
    add     lr, lr, #0x280
1:  ldr     r4, [r12]
    add     r4, r4, #1
    str     r4, [r12]
    mov     r3, #1                      @ 64 by 32
    strh    r3, [lr]
    add     r5, lr, #DIV_NUMER
    stm     r5, {r0, r1, r6}            @ the numerator, the divisor's low word
2:  ldrh    r3, [lr]
    tst     r3, #0x8000
    bne     2b
    add     r5, lr, #DIV_RESULT
    ldr     r3, [r12]
    cmp     r3, r4
    bne     1b                          @ (r0, r1 not yet touched)
    ldm     r5, {r0-r3}
    pop     {r4-r6, pc}

@ The same, the mode written after the numbers and only if it differs (as
@ libnds does): what always writing it costs.
    .global divPollCountMode
    .type   divPollCountMode, %function
divPollCountMode:
    push    {r4-r6, lr}
    mov     r6, r2
    ldr     r12, =divCount
    mov     lr, #0x04000000
    add     lr, lr, #0x280
1:  ldr     r4, [r12]
    add     r4, r4, #1
    str     r4, [r12]
    add     r5, lr, #DIV_NUMER
    stm     r5, {r0, r1, r6}
    ldrh    r3, [lr]
    and     r3, r3, #3
    cmp     r3, #1
    movne   r3, #1
    strhne  r3, [lr]
2:  ldrh    r3, [lr]
    tst     r3, #0x8000
    bne     2b
    add     r5, lr, #DIV_RESULT
    ldr     r3, [r12]
    cmp     r3, r4
    bne     1b
    ldm     r5, {r0-r3}
    pop     {r4-r6, pc}

@ The same, read after a wait of divDelay turns of a loop instead of polling.
    .global divTimedCount
    .type   divTimedCount, %function
divTimedCount:
    push    {r4-r7, lr}
    mov     r6, r2
    ldr     r7, =divDelay
    ldr     r7, [r7]
    ldr     r12, =divCount
    mov     lr, #0x04000000
    add     lr, lr, #0x280
1:  ldr     r4, [r12]
    add     r4, r4, #1
    str     r4, [r12]
    mov     r3, #1
    strh    r3, [lr]
    add     r5, lr, #DIV_NUMER
    stm     r5, {r0, r1, r6}
    movs    r3, r7
    beq     3f
2:  subs    r3, r3, #1
    bne     2b
3:  add     r5, lr, #DIV_RESULT
    ldr     r3, [r12]
    cmp     r3, r4
    bne     1b
    ldm     r5, {r0-r3}
    pop     {r4-r7, pc}

@ ---- The root, 64 bits ----------------------------------------------------

@ uint32_t of (r1:r0) in r0; polled, the count kept.
    .global rootPollCount
    .type   rootPollCount, %function
rootPollCount:
    push    {r4, lr}
    ldr     r12, =sqrtCount
    mov     lr, #0x04000000
    add     lr, lr, #0x2B0
1:  ldr     r4, [r12]
    add     r4, r4, #1
    str     r4, [r12]
    mov     r3, #1                      @ 64 bits
    strh    r3, [lr]
    add     r2, lr, #SQRT_PARAM
    stm     r2, {r0, r1}
2:  ldrh    r3, [lr]
    tst     r3, #0x8000
    bne     2b
    ldr     r2, [lr, #SQRT_RESULT]
    ldr     r3, [r12]
    cmp     r3, r4
    bne     1b
    mov     r0, r2
    pop     {r4, pc}

@ Read after sqrtDelay turns of a loop.
    .global rootTimedCount
    .type   rootTimedCount, %function
rootTimedCount:
    push    {r4, r5, lr}
    ldr     r5, =sqrtDelay
    ldr     r5, [r5]
    ldr     r12, =sqrtCount
    mov     lr, #0x04000000
    add     lr, lr, #0x2B0
1:  ldr     r4, [r12]
    add     r4, r4, #1
    str     r4, [r12]
    mov     r3, #1
    strh    r3, [lr]
    add     r2, lr, #SQRT_PARAM
    stm     r2, {r0, r1}
    movs    r3, r5
    beq     3f
2:  subs    r3, r3, #1
    bne     2b
3:  ldr     r2, [lr, #SQRT_RESULT]
    ldr     r3, [r12]
    cmp     r3, r4
    bne     1b
    mov     r0, r2
    pop     {r4, r5, pc}

@ The root's writes in three orders (Kuratius, 2026-11-26), each as the
@ game takes it: interrupts held off by the CPU's own bit, the answer read
@ after rootWait turns of a two-instruction loop. Which starts the unit
@ last decides when the wait has to run from.

@ The value, then the mode always (the game's since 25dff0a).
    .global rootValueMode
    .type   rootValueMode, %function
rootValueMode:
    mrs     r3, cpsr
    orr     r2, r3, #0x80
    msr     cpsr_c, r2
    ldr     r12, =rootWait
    ldr     r12, [r12]
    mov     r2, #0x04000000
    add     r2, r2, #0x2B8
    stm     r2, {r0, r1}
    mov     r0, #1
    strh    r0, [r2, #-8]
1:  subs    r12, r12, #1
    bpl     1b
    ldr     r0, [r2, #-4]
    msr     cpsr_c, r3
    bx      lr

@ The mode first, then the value.
    .global rootModeValue
    .type   rootModeValue, %function
rootModeValue:
    mrs     r3, cpsr
    orr     r2, r3, #0x80
    msr     cpsr_c, r2
    mov     r2, #0x04000000
    add     r2, r2, #0x2B8
    mov     r12, #1
    strh    r12, [r2, #-8]
    ldr     r12, =rootWait
    ldr     r12, [r12]
    stm     r2, {r0, r1}
1:  subs    r12, r12, #1
    bpl     1b
    ldr     r0, [r2, #-4]
    msr     cpsr_c, r3
    bx      lr

@ The value, then the mode only if the unit is not in it already: in the
@ game nothing else uses the root, so it stays in 64 bits and the write is
@ left out (the value alone starts the unit).
    .global rootValueCond
    .type   rootValueCond, %function
rootValueCond:
    mrs     r3, cpsr
    orr     r2, r3, #0x80
    msr     cpsr_c, r2
    ldr     r12, =rootWait
    ldr     r12, [r12]
    mov     r2, #0x04000000
    add     r2, r2, #0x2B8
    stm     r2, {r0, r1}
    ldrh    r0, [r2, #-8]
    tst     r0, #1
    moveq   r0, #1
    strheq  r0, [r2, #-8]
1:  subs    r12, r12, #1
    bpl     1b
    ldr     r0, [r2, #-4]
    msr     cpsr_c, r3
    bx      lr

@ The game's root (source/HwMath.s), with r1:r0 as probeTime gives it.
    .global rootGame
    .type   rootGame, %function
rootGame:
    b       hlHwSqrt64

@ ---- Two divides at once: the divider one, the soft divide the other ------

@ A walk's step: (d0 << 16) / distance and (d1 << 16) / distance, d0 in r0,
@ d1 in r1, the distance in r2: the quotients in r0 and r1. The divider
@ takes the first while the game's soft divide (its --wrap) does the
@ second; if the count moved meanwhile, the first is done soft too.
    .global pairHwSoft
    .type   pairHwSoft, %function
pairHwSoft:
    push    {r4-r8, lr}
    mov     r6, r0
    mov     r7, r2
    mov     r8, r1
    ldr     r12, =divCount
    ldr     r4, [r12]
    add     r4, r4, #1
    str     r4, [r12]
    mov     r5, #0x04000000
    add     r5, r5, #0x280
    mov     r3, #1
    strh    r3, [r5]
    mov     r1, r0, asr #16
    mov     r0, r0, lsl #16
    add     lr, r5, #DIV_NUMER
    stm     lr, {r0, r1, r2}
    @ The second, soft, while the divider works.
    mov     r0, r8, lsl #16
    mov     r1, r8, asr #16
    mov     r2, r7
    mov     r3, #0
    bl      __wrap___aeabi_ldivmod      @ (keeps r4-r8)
    mov     r8, r0
1:  ldrh    r3, [r5]
    tst     r3, #0x8000
    bne     1b
    ldr     r0, [r5, #DIV_RESULT]
    ldr     r12, =divCount
    ldr     r3, [r12]
    cmp     r3, r4
    bne     2f
    mov     r1, r8
    pop     {r4-r8, pc}
2:  mov     r0, r6, lsl #16
    mov     r1, r6, asr #16
    mov     r2, r7
    mov     r3, #0
    bl      __wrap___aeabi_ldivmod
    mov     r1, r8
    pop     {r4-r8, pc}

@ ---- A divide a bit at a time ---------------------------------------------

@ (r1:r0) / (r3:r2), signed, as __aeabi_ldivmod, where |d| < 2^31 and the
@ quotient fits 32 bits (the probe gives no other): k steps of three
@ instructions for a quotient of k bits at most, entered by a jump the
@ numbers' sizes make (shared/HlSoftDiv.hpp's shiftDivide, word for word).
    .global divShift
    .type   divShift, %function
divShift:
    push    {r4-r7, lr}
    mov     r4, r1, asr #31
    eor     r6, r0, r4
    eor     r7, r1, r4
    subs    r6, r6, r4
    sbc     r7, r7, r4
    mov     r5, r3, asr #31
    eor     r12, r2, r5
    sub     r12, r12, r5
.Lshift:
    @ k = 33 - clz64(n) + clz(d), held to 1..32.
    cmp     r7, #0
    clzne   r0, r7
    clzeq   r0, r6
    addeq   r0, r0, #32
    clz     r1, r12
    rsb     r0, r0, #33
    add     r0, r0, r1
    cmp     r0, #1
    movlt   r0, #1
    cmp     r0, #32
    movgt   r0, #32
    @ What is left to start with, n >> k (below d), and n's last k bits at
    @ the top of a word. (A shift by 32 makes 0.)
    rsb     r2, r0, #32
    mov     r3, r6, lsr r0
    orr     r3, r3, r7, lsl r2
    mov     r6, r6, lsl r2
    rsb     r1, r12, #0                 @ -d
    add     r2, r0, r0, lsl #1          @ 3 k instructions back from the end
    adr     r7, .Lshift_end
    cmn     r12, #0                     @ (no carry in)
    sub     pc, r7, r2, lsl #2
    .rept   32
    adcs    r6, r6, r6
    adcs    r3, r1, r3, lsl #1
    subcc   r3, r3, r1
    .endr
.Lshift_end:
    adc     lr, r6, r6                  @ the quotient
    @ The remainder with the numerator's sign, the quotient with both's.
    eor     r5, r5, r4
    eor     r2, r3, r4
    subs    r2, r2, r4
    sbc     r3, r4, r4
    eor     r0, lr, r5
    subs    r0, r0, r5
    sbc     r1, r5, r5
    pop     {r4-r7, pc}

@ The divide the game might have: a bit at a time for a quotient of
@ smartSteps bits or fewer (and |d| < 2^31), the game's soft divide (its
@ --wrap) for the rest.
    .global divSmart
    .type   divSmart, %function
divSmart:
    push    {r4-r7, lr}
    mov     r4, r1, asr #31
    eor     r6, r0, r4
    eor     r7, r1, r4
    subs    r6, r6, r4
    sbc     r7, r7, r4
    mov     r5, r3, asr #31
    eor     r12, r2, r5
    eor     lr, r3, r5
    subs    r12, r12, r5
    sbc     lr, lr, r5
    @ |d| below 2^31 (its high word 0, its top bit clear) and n's high word
    @ below it; then the quotient's size.
    cmp     lr, #0
    bne     .Lsmart_other
    cmp     r12, #0
    ble     .Lsmart_other               @ (0, or 2^31 and up)
    cmp     r7, r12
    bhs     .Lsmart_other
    cmp     r7, #0
    clzne   lr, r7
    clzeq   lr, r6
    addeq   lr, lr, #32
    rsb     lr, lr, #33
    push    {r0}
    clz     r0, r12
    add     lr, lr, r0
    ldr     r0, =smartSteps
    ldr     r0, [r0]
    cmp     lr, r0
    pop     {r0}
    ble     .Lshift
.Lsmart_other:
    pop     {r4-r7, lr}
    b       __wrap___aeabi_ldivmod

    .pool

    .section .data.divtricks, "aw", %progbits
    .align  2
    .global smartSteps
smartSteps: .word 12
