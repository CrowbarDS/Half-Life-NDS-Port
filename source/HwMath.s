@ The compiler's 64-bit divide, and the square root the fixed-point maths
@ uses, done by the console's own divider and square-root unit.
@
@ The compiler turns every 64-bit "/" and "%" into a call to __aeabi_ldivmod
@ (or the unsigned one): in the library that is a loop of a bit at a time,
@ some hundreds of cycles. The hardware takes 34 of the bus's. The link asks
@ for --wrap on those names (build.py), so every call comes here first, and
@ __real_ is still the library's.
@ Nothing in the shared code changes, so the host's tests go on testing the
@ same arithmetic -- and the answers are the same: a divide that rounds
@ towards zero, a root rounded down.
@
@ What the unit would answer otherwise, or cannot do, goes to the library, so
@ that the contract is the library's whole: a divide by zero (the unit says +1
@ or -1, the library the largest number of that sign), a divide by -1 (the one
@ that can overflow), and an unsigned number with its top bit set.
@ tools/hwtest/divcontract/ holds this against the library, on the console.
@
@ Interrupts are held off from the first operand to the last word of the
@ answer: an interrupt handler that divided in between would leave its own
@ answer in the unit. The CPU's own bit does that without a trip on the bus,
@ which REG_IME would take three of.
@
@ The mode is looked at after the operands: libnds leaves the unit in other
@ modes, but the last operand has started it by then, so the look costs what
@ waiting would have. Only a wrong mode costs anything: writing it starts again.
@
@ In ITCM: the code that divides most (the collision trace) is there too.

    .syntax unified
    .arch   armv5te
    .cpu    arm946e-s
    .arm

    @ From the registers' 0x04000000.
    .set    REG_DIV_NUMER,  0x290       @ -0x10 control, +0x08 denominator,
                                        @ +0x10 quotient, +0x18 remainder
    .set    REG_SQRT_PARAM, 0x2B8       @ -0x08 control, -0x04 root

    .section .itcm.text.hwmath, "ax", %progbits
    .align  2

@ (r1:r0) / (r3:r2), signed. Quotient in r1:r0, remainder in r3:r2.
    .global __wrap___aeabi_ldivmod
    .type   __wrap___aeabi_ldivmod, %function
__wrap___aeabi_ldivmod:
    orrs    r12, r2, r3
    beq     .Lsigned_library            @ by zero
    and     r12, r2, r3
    cmn     r12, #1
    beq     .Lsigned_library            @ by -1
.Lhardware:
    push    {r4, lr}
#ifdef HLDS_PROFILE
    ldr     r12, =hlHwCount             @ (a build that measures counts them)
    ldr     r4, [r12]
    add     r4, r4, #1
    str     r4, [r12]
#endif
    mrs     r4, cpsr
    orr     r12, r4, #0x80
    msr     cpsr_c, r12
    mov     r12, #0x04000000            @ (two instructions and no literal)
    add     r12, r12, #REG_DIV_NUMER
    stm     r12!, {r0-r3}               @ (r12 is left at the quotient)
    ldrh    r0, [r12, #-0x20]
    and     r0, r0, #3
    cmp     r0, #2                      @ 64 by 64
    movne   r0, #2
    strhne  r0, [r12, #-0x20]
1:  ldrh    r0, [r12, #-0x20]
    tst     r0, #0x8000
    bne     1b
    ldm     r12, {r0-r3}
    msr     cpsr_c, r4
    pop     {r4, pc}                    @ (on ARMv5 a pop to pc returns to Thumb too)
.Lsigned_library:
    ldr     r12, =__real___aeabi_ldivmod
    bx      r12
    .size   __wrap___aeabi_ldivmod, . - __wrap___aeabi_ldivmod

@ The same, unsigned. The unit divides signed numbers, which is the same
@ thing while neither has its top bit set (nothing in the game has).
    .global __wrap___aeabi_uldivmod
    .type   __wrap___aeabi_uldivmod, %function
__wrap___aeabi_uldivmod:
    orrs    r12, r1, r3
    bmi     .Lunsigned_library
    orrs    r12, r2, r3
    bne     .Lhardware
.Lunsigned_library:
    ldr     r12, =__real___aeabi_uldivmod
    bx      r12
    .size   __wrap___aeabi_uldivmod, . - __wrap___aeabi_uldivmod

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
@ How many divides and how many roots, since whoever reads it last cleared it.
    .section .bss.hlHwCount, "aw", %nobits
    .align  2
    .global hlHwCount
hlHwCount:
    .space  8
#endif
