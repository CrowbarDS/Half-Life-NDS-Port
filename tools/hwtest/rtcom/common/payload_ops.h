// What the probe's ARM11 program (arm11/payload.c) understands. rtcom calls
// it once for each byte of a 0x44 command: the first call gets the command
// byte with an iterator of 0, each 0x81 that follows the next byte and the
// iterator counting up; what it returns is the answer byte.
//
// Three more bytes go each way in the clock's second alarm: the ARM7 writes
// or reads them with commands 0x6A / 0x6B, the ARM11 finds them at
// 0x1EC47134..36, the ARM7's first byte at the highest address.

#pragma once

// iter 0: returns kHello. Each byte after: returns it inverted, and the
// alarm's three bytes come back one higher.
#define OP_ECHO 0x01
// iter 1-4: an address, low byte first. At 4: the word is read; returns its
// low byte, its other three go in the alarm. iter 5-7 return them again,
// one a call (to check the alarm path against).
#define OP_PEEK 0x02
// iter 1: the device (8-bit address), 2: register, 3: how many (1-16): the
// read happens then; returns 1 done, 0 not answered. iter 4..: the bytes.
#define OP_I2C_READ 0x04
// iter 1: the device, 2: register, 3: the value: written then; returns 1 or 0.
#define OP_I2C_WRITE 0x05

// iter 1: the device. Returns the control byte seen after its address was
// sent (bit 4 set: it answered; 0xFF: the bus never went idle).
#define OP_I2C_PROBE 0x06
// iter 1: which bus, 0-2 (0x10161000, 0x10144000, 0x10148000). The third
// until told otherwise. Returns 1.
#define OP_I2C_BUS 0x07

#define PAYLOAD_HELLO 0xA5
#define PAYLOAD_UNKNOWN 0xEE
