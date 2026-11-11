@ What the ARM7 uploads to a 3DS's ARM11 through rtcom (Rtcom.cpp), never
@ run here. TWPatch's TwlBg copies it to 0x201000 and calls it once for each
@ byte of a 0x44 command: r0 the byte, r1 how many came before it; it returns
@ the answer in r0. ARMv4T only, which the ARM11 also runs, so the ARM7's
@ assembler builds it and no ARM11 toolchain is needed.
@
@ Call 0's byte says what the command is.
@
@ 0x00, a peek:
@   call 0     answers 0x5A.
@   calls 1-4  the address, low byte first; the 4th answers the byte there.
@   call n > 4 answers the byte at address + n - 4.
@
@ 0x50 ('P'), the circle pad (TwlBg's own reading, for its D-pad):
@   call 0     answers 0x5A.
@   call 1     answers X's low byte; the clock's second alarm gets X's high
@              byte, then Y's low and high (the DS's first byte at the top),
@              and is marked new. X and Y are signed 16-bit, 0 in the middle.
@              No pad found: answers 0 and the alarm reads FF FF FF.
@   The pad's place is found once: TwlBg's input routine, in the code it
@   runs at 0x100000, splits the pad's word into X and Y kept at +12 and +14
@   of a block it loads into r5 just before (measured on an Old 3DS through
@   tools/hwtest/rtcom: 0x12A5B8).

    .section .rodata.peek11, "a"
    .arm
    .align 2
    .global hlPeek11Start, hlPeek11End

hlPeek11Start:
    cmp     r1, #0
    bne     1f
    and     r0, r0, #0xFF
    str     r0, command
    str     r1, address
    mov     r0, #0x5A
    bx      lr
1:  ldr     r2, command
    cmp     r2, #0x50
    beq     pad
    ldr     r2, address
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

pad:
    cmp     r1, #1
    movne   r0, #0
    bxne    lr
    stmfd   sp!, {r4-r7}
    ldr     r4, padAt
    cmp     r4, #0
    bne     read
    @ Look for the split: r2 the place, r5 the bytes, r6 how many matched.
    ldr     r2, scanFrom
    ldr     r3, scanTo
    adr     r5, split
3:  mov     r6, #0
4:  ldrb    r7, [r2, r6]
    ldrb    r12, [r5, r6]
    cmp     r7, r12
    bne     5f
    add     r6, r6, #1
    cmp     r6, #10
    blt     4b
    @ Found: the nearest "ldr r5, [pc, #imm]" (0x4Dxx) before it.
    mov     r6, #2
6:  sub     r7, r2, r6
    ldrh    r12, [r7]
    mov     r0, r12, lsr #8
    cmp     r0, #0x4D
    beq     7f
    add     r6, r6, #2
    cmp     r6, #0x80
    blt     6b
    b       none
7:  and     r12, r12, #0xFF
    add     r7, r7, #4
    bic     r7, r7, #3
    ldr     r7, [r7, r12, lsl #2]
    add     r4, r7, #12
    str     r4, padAt
    b       read
5:  add     r2, r2, #2
    cmp     r2, r3
    blt     3b
none:
    mvn     r0, #0
    b       alarm
read:
    ldr     r0, [r4]
alarm:
    ldr     r12, rtc
    mov     r1, r0, lsr #8
    strb    r1, [r12, #0x36]
    mov     r1, r0, lsr #16
    strb    r1, [r12, #0x35]
    mov     r1, r0, lsr #24
    strb    r1, [r12, #0x34]
    ldrb    r1, [r12]
    orr     r1, r1, #0x40
    strb    r1, [r12]
    cmp     r4, #0
    andne   r0, r0, #0xFF
    moveq   r0, #0
    ldmfd   sp!, {r4-r7}
    bx      lr

    .align 2
command:
    .word   0
address:
    .word   0
padAt:
    .word   0
scanFrom:
    .word   0x100000
scanTo:
    .word   0x127B00 - 10
rtc:
    .word   0x1EC47100
split:
    .byte   0x61, 0x68, 0x08, 0x05, 0x00, 0x0D, 0xA8, 0x81, 0x08, 0x02
    .align 2
hlPeek11End:
