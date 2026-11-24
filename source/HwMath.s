@ The compiler's 64-bit divide, and the square root the fixed-point maths
@ uses.
@
@ The compiler turns every 64-bit "/" and "%" into a call to __aeabi_ldivmod
@ (or the unsigned one): in the library that is a loop of a bit at a time,
@ some hundreds of cycles. The link asks for --wrap on those names
@ (build.py), so every call comes here first, and __real_ is still the
@ library's. Nothing in the shared code changes, so the host's tests go on
@ testing the same arithmetic -- and the answers are the same: a divide that
@ rounds towards zero, its remainder with the numerator's sign.
@
@ The divide is done without the divider, by multiplies alone: Kuratius's
@ method (shared/HlSoftDiv.hpp says how, and test_softdiv holds that code to
@ the host's divide; this is the same steps by hand). It takes every divide
@ whose divisor fits 32 bits and whose quotient does -- the game's: a trace's
@ fraction, a vector made unit, a clip's place in its span -- and leaves the
@ rest to the library (a divide by zero, the one by -1 that overflows, a
@ quotient of more than 32 bits: a few in a thousand). Neither touches the
@ divider, so a divide may be made anywhere: in an interrupt handler, in a
@ thread switched away from in the middle of it, with nothing to hold off
@ and nothing shared to save (tools/irq-divider.py now looks for the root
@ alone). On a DSi it is also faster than the divider was, 40-45 bus ticks to
@ 54; on a DS slower, 80-90 to 62 (tools/hwtest/softdiv).
@ tools/hwtest/divcontract/ holds these routines against the library, on the
@ console.
@
@ The root is still the square-root unit's, interrupts held off from its
@ operand to its answer (an interrupt handler that took a root in between
@ would leave its own there). The CPU's own bit does that without a trip on
@ the bus, which REG_IME would take three of. The mode is looked at after the
@ operand: libnds leaves the unit in another, but the operand has started it
@ by then, so the look costs what waiting would have.
@
@ In ITCM: the code that divides most (the collision trace) is there too.

    .syntax unified
    .arch   armv5te
    .cpu    arm946e-s
    .arm

    @ From the registers' 0x04000000.
    .set    REG_SQRT_PARAM, 0x2B8       @ -0x08 control, -0x04 root

    .section .itcm.text.hwmath, "ax", %progbits
    .align  2

@ (r1:r0) / (r3:r2), signed. Quotient in r1:r0, remainder in r3:r2.
    .global __wrap___aeabi_ldivmod
    .type   __wrap___aeabi_ldivmod, %function
__wrap___aeabi_ldivmod:
    push    {r4-r7, lr}
#ifdef HLDS_PROFILE
    bl      .Lcount
#endif
    @ The signs (0 or -1) and the sizes: |n| in r7:r6, |d| in lr:r12.
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
    @ This way if |d| fits 32 bits and the quotient does: |n| < |d| << 32
    @ (which a |d| of 0 never passes).
    cmp     lr, #0
    bne     .Lsigned_library
    cmp     r7, r12
    bhs     .Lsigned_library
    b       .Lsoft
.Lsigned_library:
#ifdef HLDS_PROFILE
    bl      .Lcount_library
#endif
    pop     {r4-r7, lr}
    ldr     r12, =__real___aeabi_ldivmod
    bx      r12
    .size   __wrap___aeabi_ldivmod, . - __wrap___aeabi_ldivmod

@ The same, unsigned.
    .global __wrap___aeabi_uldivmod
    .type   __wrap___aeabi_uldivmod, %function
__wrap___aeabi_uldivmod:
    push    {r4-r7, lr}
#ifdef HLDS_PROFILE
    bl      .Lcount
#endif
    mov     r4, #0
    mov     r5, #0
    mov     r6, r0
    mov     r7, r1
    mov     r12, r2
    cmp     r3, #0
    bne     .Lunsigned_library
    cmp     r7, r12
    blo     .Lsoft
.Lunsigned_library:
#ifdef HLDS_PROFILE
    bl      .Lcount_library
#endif
    pop     {r4-r7, lr}
    ldr     r12, =__real___aeabi_uldivmod
    bx      r12

@ |n| (r7:r6) by |d| (r12), the quotient within 32 bits; the signs in r4
@ (the numerator's) and r5 (the divisor's). r4-r7 and lr are on the stack.
.Lsoft:
    @ A reciprocal of d, of 2^62 / b0 with b0 = d << c its top bit set
    @ (c = d's leading zeros): b0 >> 1 in [2^30, 2^31) multiplied by
    @ (1 + 2^-i) for i = 1..8, each while it stays below 2^31; 2^31 by the
    @ same factors is the estimate.
    clz     r0, r12
    cmp     r0, #31
    beq     .Lby_one
    mov     r1, r12, lsl r0             @ b0
    mov     r2, r1, lsr #1              @ b
    mov     r3, #0x80000000             @ the estimate
    adds    lr, r2, r2, lsr #1
    movpl   r2, lr
    addpl   r3, r3, r3, lsr #1
    adds    lr, r2, r2, lsr #2
    movpl   r2, lr
    addpl   r3, r3, r3, lsr #2
    adds    lr, r2, r2, lsr #3
    movpl   r2, lr
    addpl   r3, r3, r3, lsr #3
    adds    lr, r2, r2, lsr #4
    movpl   r2, lr
    addpl   r3, r3, r3, lsr #4
    adds    lr, r2, r2, lsr #5
    movpl   r2, lr
    addpl   r3, r3, r3, lsr #5
    adds    lr, r2, r2, lsr #6
    movpl   r2, lr
    addpl   r3, r3, r3, lsr #6
    adds    lr, r2, r2, lsr #7
    movpl   r2, lr
    addpl   r3, r3, r3, lsr #7
    adds    lr, r2, r2, lsr #8
    movpl   r2, lr
    addpl   r3, r3, r3, lsr #8
    @ The hidden Newton step: (2^32 - b) is 2^31 (1 + e).
    rsb     r2, r2, #0
    umull   lr, r2, r2, r3              @ r2: the reciprocal, 2^61 / b0's scale
    @ One more: r (2 - r b0), in 2^62 / b0's.
    mov     r2, r2, lsl #1
    umull   r3, lr, r2, r1              @ r b0
    rsbs    r3, r3, #0
    rsc     lr, lr, #0                  @ t = -(r b0), lr:r3
    umull   r1, r3, r2, r3              @ (r * t's low word) >> 32
    mov     r1, #0
    umlal   r3, r1, r2, lr              @ r1: (r t) >> 64, the reciprocal
    @ The quotient: n r >> (62 - c), never above the true one.
    umull   r3, lr, r6, r1
    mov     r2, #0
    umlal   lr, r2, r7, r1              @ r2:lr = n r >> 32
    rsb     r0, r0, #30                 @ 30 - c: 0..30
    mov     lr, lr, lsr r0
    rsb     r3, r0, #32
    orr     lr, lr, r2, lsl r3          @ (a shift by 32 makes 0)
    @ Put right from the remainder: n - q d, then up a step at a time while
    @ it is d or more (or down while it is below 0, which test_softdiv has
    @ never seen).
    umull   r3, r2, lr, r12
    subs    r6, r6, r3
    sbcs    r7, r7, r2
    bmi     .Ldown
    cmp     r7, #0
    cmpeq   r6, r12
    bhs     .Lup
.Ldone:
    @ The remainder (r6) with the numerator's sign, the quotient (lr) with
    @ both's, each made 64 bits.
    eor     r2, r6, r4
    subs    r2, r2, r4
    sbc     r3, r4, r4
    eor     r5, r5, r4
    eor     r0, lr, r5
    subs    r0, r0, r5
    sbc     r1, r5, r5
    pop     {r4-r7, pc}                 @ (on ARMv5 a pop to pc returns to Thumb too)
.Lup:
    add     lr, lr, #1
    subs    r6, r6, r12
    sbc     r7, r7, #0
    cmp     r7, #0
    cmpeq   r6, r12
    bhs     .Lup
    b       .Ldone
.Ldown:
    sub     lr, lr, #1
    adds    r6, r6, r12
    adcs    r7, r7, #0
    bmi     .Ldown
    b       .Ldone
.Lby_one:
    @ By 1: the numerator itself (it fits 32 bits), nothing left.
    mov     lr, r6
    mov     r6, #0
    b       .Ldone
    .size   __wrap___aeabi_uldivmod, . - __wrap___aeabi_uldivmod

#ifdef HLDS_PROFILE
@ A build that measures counts every divide (hlHwCount[0]) and where each
@ comes from (hlProfileSite, DebugLog.cpp: its third argument is 0 when both
@ numbers fit 32 bits), with interrupts held off for the tables' sake; and
@ those left to the library (hlHwCount[2]). Each keeps every register but r12.
.Lcount:
    push    {r0-r4, lr}
    ldr     r12, =hlHwCount
    ldr     r4, [r12]
    add     r4, r4, #1
    str     r4, [r12]
    mrs     r4, cpsr
    orr     r12, r4, #0x80
    msr     cpsr_c, r12
    eor     r12, r1, r0, asr #31
    eor     r3, r3, r2, asr #31
    orr     r2, r12, r3
    ldr     r0, [sp, #24 + 16]          @ the caller's lr, as the wrap pushed it
    mov     r1, #0
    ldr     r12, =hlProfileSite
    blx     r12
    msr     cpsr_c, r4
    pop     {r0-r4, pc}
.Lcount_library:
    ldr     r12, =hlHwCount
    ldr     r4, [r12, #8]
    add     r4, r4, #1
    str     r4, [r12, #8]
    bx      lr
#endif

@ uint32_t hlHwSqrt64(uint64_t value): its square root, rounded down.
    .global hlHwSqrt64
    .type   hlHwSqrt64, %function
hlHwSqrt64:
#ifdef HLDS_PROFILE
    ldr     r12, =hlHwCount
    ldr     r2, [r12, #4]
    add     r2, r2, #1
    str     r2, [r12, #4]
#endif
    mrs     r3, cpsr
    orr     r2, r3, #0x80
    msr     cpsr_c, r2
#ifdef HLDS_PROFILE
    push    {r0, r1, r3, lr}            @ (where from, as the divide's)
    mov     r0, lr
    mov     r1, #1
    mov     r2, #0
    ldr     r12, =hlProfileSite
    blx     r12
    pop     {r0, r1, r3, lr}
#endif
    mov     r12, #0x04000000
    add     r12, r12, #REG_SQRT_PARAM
    stm     r12, {r0, r1}
    ldrh    r0, [r12, #-8]
    tst     r0, #1                      @ of 64 bits
    moveq   r0, #1
    strheq  r0, [r12, #-8]
1:  ldrh    r0, [r12, #-8]
    tst     r0, #0x8000
    bne     1b
    ldr     r0, [r12, #-4]
    msr     cpsr_c, r3
    bx      lr
    .size   hlHwSqrt64, . - hlHwSqrt64

    .pool

#ifdef HLDS_PROFILE
@ How many divides, how many roots, and how many of the divides were left to
@ the library, since whoever reads it last cleared it.
    .section .bss.hlHwCount, "aw", %nobits
    .align  2
    .global hlHwCount
hlHwCount:
    .space  12
#endif
