// What our TwlBg tells the DS, each 3DS frame, in the DS clock's two alarm
// registers. The DS's ARM7 reads them with plain clock reads (commands 0x69
// and 0x6B, three bytes each): no handshake, no interrupt.
//
// Shared by the ARM11 payload (feed.c) and the DS side. Bytes are numbered in
// the order the DS reads them.

#pragma once

// alarm 1, byte 0: high nibble FEED_MARK (our TwlBg, this layout), then
// FEED_GYRO, FEED_WIDE and a 2-bit count that moves every 3DS frame.
#define FEED_MARK 0xB0
#define FEED_MARK_MASK 0xF0
#define FEED_GYRO 0x01 // the gyroscope answers; the angles below move
#define FEED_WIDE 0x02 // the top screen is shown 16:10
#define FEED_COUNT_SHIFT 2
// alarm 1, bytes 1-2: the circle pad, X then Y, the top 8 of TwlBg's 12 bits.
//
// alarm 2: two 12-bit angles, yaw then pitch: byte 0 yaw's top 8 bits,
// byte 1 yaw's low 4 then pitch's top 4, byte 2 pitch's low 8. Each is the
// sum of the gyroscope's rate over every 3DS frame so far, in 1/32 of its
// raw step, kept to 12 bits: the DS takes the change since its last read
// (a signed 12-bit difference), so a missed read loses nothing.
//
// The raw step is the chip's at its +-2000 degrees/s range: 14.375 a degree a
// second; summed once a 3DS frame (about 59.8 a second), one unit sent is
// 32 / 14.375 / 59.8, about 0.037 degrees.
#define FEED_ANGLE_SHIFT 5
