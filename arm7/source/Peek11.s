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
@ 0x50 ('P'), the circle pad and the gyroscope, once a frame:
@   call 0     answers 0x5A.
@   call 1     answers the pad's X; the clock's second alarm gets its Y, then
@              the gyroscope's X rate, low byte first. (The alarm's three
@              bytes: the DS's first at the top. Each call marks it new.)
@   call 2     answers the gyroscope's Y rate's low byte; the alarm gets its
@              high byte, then the Z rate, low byte first.
@   The pad's X and Y: TwlBg's own reading (for its D-pad), calibrated, 0
@   in the middle, made 8-bit (-127..127; measured on an Old 3DS: about
@   -105..105). No pad found: Y reads -128 (0x80), never a reading.
@   The rates: the chip's, signed 16-bit, 14.375 a degree a second. No
@   gyroscope (or not yet started, or given up): all three 0x8000.
@
@ The pad's place is found once: TwlBg's input routine, in the code it runs
@ at 0x100000, splits the pad's word into X and Y kept at +12 and +14 of a
@ block it loads into r5 just before (measured on an Old 3DS through
@ tools/hwtest/rtcom: 0x12A5B8).
@
@ The gyroscope (an ITG-3200's register map; on an Old 3DS at 0xD0 on the
@ third I2C bus) sleeps in DSi mode: the first frame wakes it (0x3E <- 0,
@ 0x16 <- 0x18: +-2000 degrees a second), the next ones read its rates
@ (0x1D on, X, Y, Z, high byte first). Every wait on the bus has an end:
@ TwlBg's picture is never held. Eight failures in a row and it is left
@ alone for good.

    .syntax unified
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
    beq     padFirst
    cmp     r1, #2
    beq     padSecond
    mov     r0, #0
    bx      lr

@ Call 1: the pad, and the gyroscope read for both calls.
padFirst:
    stmfd   sp!, {r4-r8, lr}
    ldr     r4, padAt
    cmp     r4, #0
    bleq    padFind
    ldr     r12, rtc
    cmp     r4, #0
    bne     3f
    mov     r1, #0x80
    strb    r1, [r12, #0x36]
    mov     r0, #0
    b       5f
3:  ldr     r0, [r4]
    bl      clamp8
    mov     r5, r0
    ldr     r0, [r4]
    mov     r0, r0, asr #16
    bl      clamp8
    mov     r6, r0
    bl      gyro
    ldr     r12, rtc
    ldr     r0, gyroXY
    strb    r6, [r12, #0x36]
    strb    r0, [r12, #0x35]
    mov     r1, r0, lsr #8
    strb    r1, [r12, #0x34]
    mov     r0, r5
5:  ldrb    r1, [r12]
    orr     r1, r1, #0x40
    strb    r1, [r12]
    ldmfd   sp!, {r4-r8, lr}
    bx      lr

@ Call 2: the rest of the gyroscope's reading.
padSecond:
    ldr     r0, gyroXY
    ldr     r2, gyroZ
    ldr     r12, rtc
    mov     r1, r0, lsr #24
    strb    r1, [r12, #0x36]
    strb    r2, [r12, #0x35]
    mov     r1, r2, lsr #8
    strb    r1, [r12, #0x34]
    ldrb    r1, [r12]
    orr     r1, r1, #0x40
    strb    r1, [r12]
    mov     r0, r0, lsr #16
    and     r0, r0, #0xFF
    bx      lr

@ r0's low 16 bits, signed, to -127..127, as a byte.
clamp8:
    mov     r0, r0, lsl #16
    mov     r0, r0, asr #16
    cmp     r0, #127
    movgt   r0, #127
    cmn     r0, #127
    mvnlt   r0, #126
    and     r0, r0, #0xFF
    bx      lr

@ The pad's place into r4 and padAt, or r4 = 0. (r0-r3, r5-r7, r12.)
padFind:
    ldr     r2, scanFrom
    ldr     r3, scanTo
    adr     r5, split
6:  mov     r6, #0
7:  ldrb    r7, [r2, r6]
    ldrb    r12, [r5, r6]
    cmp     r7, r12
    bne     9f
    add     r6, r6, #1
    cmp     r6, #10
    blt     7b
    @ Found: the nearest "ldr r5, [pc, #imm]" (0x4Dxx) before it.
    mov     r6, #2
8:  sub     r7, r2, r6
    ldrh    r12, [r7]
    mov     r0, r12, lsr #8
    cmp     r0, #0x4D
    beq     10f
    add     r6, r6, #2
    cmp     r6, #0x80
    blt     8b
    mov     r4, #0
    bx      lr
10: and     r12, r12, #0xFF
    add     r7, r7, #4
    bic     r7, r7, #3
    ldr     r7, [r7, r12, lsl #2]
    add     r4, r7, #12
    str     r4, padAt
    bx      lr
9:  add     r2, r2, #2
    cmp     r2, r3
    blt     6b
    mov     r4, #0
    bx      lr

@ --- The gyroscope ------------------------------------------------------------
@ r8: the bus. idle, send and receive use r0, r1, r3.

@ r0 = 1 when the bus is idle, 0 when it never was.
idle:
    ldr     r3, waitLimit
11: ldrb    r0, [r8, #1]
    tst     r0, #0x80
    moveq   r0, #1
    bxeq    lr
    subs    r3, r3, #1
    bne     11b
    mov     r0, #0
    bx      lr

@ r0 out with flags r1; r0 = 1 if the device acknowledged.
send:
    stmfd   sp!, {lr}
    strb    r0, [r8]
    orr     r1, r1, #0xC0
    strb    r1, [r8, #1]
    bl      idle
    cmp     r0, #0
    beq     12f
    ldrb    r0, [r8, #1]
    tst     r0, #0x10
    movne   r0, #1
    bne     12f
    mov     r0, #0xC5               @ an error and a stop: the bus let go
    strb    r0, [r8, #1]
    bl      idle
    mov     r0, #0
12: ldmfd   sp!, {lr}
    bx      lr

@ A byte in, flags r1 (0x10 more to come, 0x01 the last); r0 = it, or -1.
receive:
    stmfd   sp!, {lr}
    orr     r1, r1, #0xE0
    strb    r1, [r8, #1]
    bl      idle
    cmp     r0, #0
    ldrbne  r0, [r8]
    mvneq   r0, #0
    ldmfd   sp!, {lr}
    bx      lr

@ The device, then register r0 (kept in r7): 1 if all acknowledged.
begin:
    stmfd   sp!, {lr}
    mov     r7, r0
    bl      idle
    cmp     r0, #0
    beq     13f
    mov     r0, #0xD0
    mov     r1, #0x02
    bl      send
    cmp     r0, #0
    beq     13f
    mov     r0, r7
    mov     r1, #0
    bl      send
13: ldmfd   sp!, {lr}
    bx      lr

@ Register r0 <- r1 (kept in r6): 1 if done.
write:
    stmfd   sp!, {lr}
    mov     r6, r1
    bl      begin
    cmp     r0, #0
    beq     14f
    mov     r0, r6
    mov     r1, #0x01
    bl      send
14: ldmfd   sp!, {lr}
    bx      lr

@ One step: started the first time, read after; gyroXY and gyroZ set.
@ (r0-r3, r6-r8.)
gyro:
    stmfd   sp!, {r6, lr}
    ldr     r8, i2c
    ldr     r0, gyroState
    cmp     r0, #2
    beq     gyroNone
    cmp     r0, #1
    beq     gyroRead
    bl      idle
    mov     r0, #2                  @ the device may hold the clock
    strh    r0, [r8, #2]
    mov     r0, #0x500
    strh    r0, [r8, #4]
    mov     r0, #0x3E
    mov     r1, #0
    bl      write
    cmp     r0, #0
    beq     gyroFail
    mov     r0, #0x16
    mov     r1, #0x18
    bl      write
    cmp     r0, #0
    beq     gyroFail
    mov     r0, #1
    str     r0, gyroState
    mov     r0, #0
    str     r0, gyroFails
    b       gyroNone                @ (its first rates next frame)
gyroRead:
    mov     r0, #0x1D
    bl      begin
    cmp     r0, #0
    beq     gyroFail
    mov     r0, #0xD1
    mov     r1, #0x02
    bl      send
    cmp     r0, #0
    beq     gyroFail
    adr     r2, raw
    mov     r6, #0
15: cmp     r6, #5
    moveq   r1, #0x01
    movne   r1, #0x10
    bl      receive
    cmn     r0, #1
    beq     gyroFail
    strb    r0, [r2, r6]
    add     r6, r6, #1
    cmp     r6, #6
    blt     15b
    ldrb    r0, [r2]
    ldrb    r1, [r2, #1]
    orr     r0, r1, r0, lsl #8
    ldrb    r1, [r2, #2]
    ldrb    r3, [r2, #3]
    orr     r1, r3, r1, lsl #8
    orr     r0, r0, r1, lsl #16
    str     r0, gyroXY
    ldrb    r0, [r2, #4]
    ldrb    r1, [r2, #5]
    orr     r0, r1, r0, lsl #8
    str     r0, gyroZ
    mov     r0, #0
    str     r0, gyroFails
    ldmfd   sp!, {r6, lr}
    bx      lr
gyroFail:
    ldr     r0, gyroFails
    add     r0, r0, #1
    str     r0, gyroFails
    cmp     r0, #8
    movge   r0, #2
    strge   r0, gyroState
gyroNone:
    ldr     r0, none
    str     r0, gyroXY
    str     r0, gyroZ
    ldmfd   sp!, {r6, lr}
    bx      lr

    .align 2
command:
    .word   0
address:
    .word   0
padAt:
    .word   0
gyroState:
    .word   0                       @ 0 not started, 1 running, 2 given up
gyroFails:
    .word   0                       @ in a row
gyroXY:
    .word   0x80008000              @ X low half, Y high half
gyroZ:
    .word   0x80008000              @ low half
none:
    .word   0x80008000
raw:
    .space  8
scanFrom:
    .word   0x100000
scanTo:
    .word   0x127B00 - 10
rtc:
    .word   0x1EC47100
i2c:
    .word   0x1EC48000
waitLimit:
    .word   20000
split:
    .byte   0x61, 0x68, 0x08, 0x05, 0x00, 0x0D, 0xA8, 0x81, 0x08, 0x02
    .align 2
hlPeek11End:
