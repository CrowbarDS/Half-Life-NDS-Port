// Shared between the two processors of the rtcom probe: what the ARM9 asks
// for and what the ARM7 found. It lives in main RAM; the ARM9 goes through
// the uncached mirror, the ARM7 has no cache.
//
// The questions (the 3DS plan, phase 0):
//  1. Is this a 3DS (the MCU test TWiLight uses), and does the TwlBg answer
//     rtcom (TWPatch's channel to the 3DS's ARM11)?
//  2. Does a program of ours uploaded to the ARM11 run, and do the three
//     extra bytes (the clock's second alarm) go both ways?
//  3. What do the top screen's scaler and window registers hold, with the
//     plain TwlBg and with the wide one? (How the game tells them apart.)
//  4. Is the gyroscope on the ARM11's third I2C bus, at which address, and
//     what does it give?
//  5. Can the ARM7 read the circle pad itself, through the codec's 3DS
//     stream (page 0xFB), as Linux for the 3DS does?

#pragma once

#include <stdint.h>

enum ProbeCommand
{
    PROBE_IDLE = 0,
    PROBE_AUTO,      // 1-4 once: detect, upload, echo, display registers, gyro scan
    PROBE_GYRO_LIVE, // the found gyro, woken and read every frame
    PROBE_GYRO_DUMP, // the found gyro's registers 0x00-0x7F
    PROBE_PAD_SCAN,  // 5: the codec's 3DS stream (page FB, reg 01), decoded
    PROBE_PAD_ON,    // ... after setting page 67's sampling bits (may stop touch)
    PROBE_HID_LIVE,  // the 3DS's own button register, read every frame
    PROBE_BUS_SCAN,  // every address of I2C bus 3: which answer
    PROBE_STOP,
};

enum GyroKind
{
    GYRO_NONE = 0,
    GYRO_ITG,  // WHO_AM_I at 0x00, rates at 0x1D (ITG-3200 family)
    GYRO_MPU,  // WHO_AM_I at 0x75, rates at 0x43 (MPU family)
};


typedef struct ProbeShared
{
    volatile uint32_t command;  // ProbeCommand, from the ARM9
    volatile uint32_t running;  // what the ARM7 is doing
    volatile uint32_t step;     // a counter that moves while it works
    volatile char where[32];    // the step it is on, for a hang

    // 1
    volatile int32_t dsiMode;
    volatile int32_t mcuBefore, mcuAfter; // MCU 0x71 read back after writing 0xD2
    volatile int32_t is3ds;
    volatile int32_t pingStatus;   // what status 1 got back (0x82 expected), -1 none
    volatile uint32_t pingTicks;   // bus ticks to the answer

    // 2
    volatile int32_t uploadOk;
    volatile uint32_t uploadBytes;
    volatile uint32_t uploadTicks;
    volatile int32_t uploadFailAt; // byte that failed, -1 none
    volatile int32_t helloByte;    // 0xA5 from our payload
    volatile int32_t echoOk;       // 8 echoes right
    volatile int32_t echoExtraOk;  // ... and the 3 alarm bytes back +1
    volatile uint32_t echoExtra[3];
    volatile uint32_t exchangeTicksMin, exchangeTicksMax; // one 0x81 round trip
    volatile uint32_t fastTicks;   // one exchange with the short bit delay

    // 3
    volatile int32_t peekOk;       // the alarm-path word matches the byte-path one
    volatile uint32_t scaler[4];   // 0x1EC11000 +0, +4, +8, +C
    volatile uint32_t xmatrix[2];  // 0x1EC11300 +0, +4
    volatile uint32_t window[2];   // 0x1ED02200 +18, +1C
    volatile uint32_t hid;         // 0x1EC46000

    // 4
    volatile int32_t gyroAck[2][2]; // [0xD0, 0xD2][reg 0x00, reg 0x75]: -1 nak, else value
    volatile int32_t gyroKind;
    volatile int32_t gyroAddr;
    volatile int16_t gyro[3];
    volatile int32_t gyroReads, gyroFails;
    volatile uint8_t gyroRegs[128];
    volatile int32_t gyroDumpRan;   // 0 never, else 1 + chunks of 16 read
    volatile int32_t irScratch[2];  // 9A reg 38 read back after writing 5A, A5 (-1 none)
    volatile int32_t gyroSingle[3]; // D0 regs 00, 16, 3E by one-byte reads
    volatile int32_t busScanned;    // the bus last scanned, -1 none
    volatile int32_t busAnswered;   // addresses that answered
    volatile uint8_t busAddr[32];   // ... the first 32 (8-bit)
    volatile uint8_t busCnt[128];   // control byte after each address

    // 5: page FB reg 01, 0x34 bytes: touch (2 x 5 words) then pad (2 x 8),
    // big-endian 12-bit; pad Y at 0x14, X at 0x24 (Linux for the 3DS).
    volatile uint8_t padRaw[0x34];
    volatile int32_t padX, padY, padXmin, padXmax, padYmin, padYmax;
    volatile uint32_t padReads;
    volatile uint8_t cdc67[3]; // page 67 regs 24, 25, 26 as found
    volatile int32_t padOn;    // the sampling bits were set
} ProbeShared;


// The ARM9 sends the block's address through this FIFO channel at start.
#define PROBE_FIFO FIFO_USER_01
