@ What the ARM7 uploads to a 3DS's ARM11 through rtcom (Rtcom.cpp): a byte
@ reader, never run here. TWPatch's TwlBg copies it to 0x201000 and calls it
@ once for each byte of a 0x44 command: r0 the byte, r1 how many came
@ before it; it returns the answer in r0. ARMv4 only, which the ARM11
@ also runs, and nothing outside it but what it is asked to read, so the
@ ARM7's assembler builds it and no ARM11 toolchain is needed.
@
@   call 0     (the command byte, any): the address cleared; answers 0x5A.
@   calls 1-4  the address, low byte first; the 4th answers the byte there.
@   call n > 4 answers the byte at address + n - 4.

    .section .rodata.peek11, "a"
    .arm
    .align 2
    .global hlPeek11Start, hlPeek11End

hlPeek11Start:
    cmp     r1, #0
    bne     1f
    str     r1, address
    mov     r0, #0x5A
    bx      lr
1:  ldr     r2, address
    cmp     r1, #4
    bgt     2f
    and     r0, r0, #0xFF
    sub     r3, r1, #1
    mov     r3, r3, lsl #3
    orr     r2, r2, r0, lsl r3
    str     r2, address
    cmp     r1, #4
    movne   r0, #0
    bxne    lr
    ldrb    r0, [r2]
    bx      lr
2:  sub     r3, r1, #4
    ldrb    r0, [r2, r3]
    bx      lr
    .align 2
address:
    .word   0
hlPeek11End:
