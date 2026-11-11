// Our ARM11 code inside TwlBg, the 3DS program that shows DSi mode's screens.
//
// It lives in code TwlBg never runs (feed.ld) and is called once a 3DS frame
// in place of TwlBg's per-frame input routine, which it calls first. Then it
// reads the gyroscope and puts it, with the circle pad that routine read, into
// the DS clock's alarm registers (feed.h). The patcher (tools/3ds/twlbg.py)
// fills in the head for the TwlBg it patches and points the frame's call at
// the head.
//
// It must never stop the picture: every wait has an end, and a gyroscope that
// keeps failing is left alone.

#include <stdint.h>

#include "feed.h"

typedef volatile uint8_t vu8;
typedef volatile uint16_t vu16;

typedef void (*Routine)(void);

static void feedEntry(void);

// What lasts from frame to frame, in RAM nobody else uses.
struct State
{
    int32_t angle[3]; // each axis's rate summed, raw steps
    uint8_t count;
    uint8_t gyroUp;   // woken
    uint8_t failures; // reads in a row that failed
    uint8_t gyroOff;  // given up on
};

// The head: it stands in for TwlBg's table of frame routines, whose second
// word is the one called each frame. The rest the patcher fills in.
struct Head
{
    uint32_t unused;      // the table's first word, never read through us
    Routine frame;        // what the frame calls: us
    const Routine *table; // TwlBg's own table (its +1 word: the input routine)
    const uint16_t *pad;  // TwlBg's circle pad: X at [0], Y at [1], 12 bits
    struct State *state;
    // The gyroscope's axes (0-2) sent as yaw and as pitch; the patcher
    // checks them. (Which way round each turns is the DS's business.)
    uint8_t yawAxis, pitchAxis, unused2[2];
};

__attribute__((section(".head"), used)) struct Head head = {0, feedEntry, 0, 0, 0, 1, 0, {0, 0}};

// The DS clock as TwlBg shows it to the DS: control, and the two alarms, the
// DS's first byte at the highest address. (Reached through one base address:
// a literal each would not fit.)
struct Rtc
{
    uint8_t cnt;
    uint8_t unused[0x2F];
    uint8_t alarm1[4];
    uint8_t alarm2[4];
};
#define RTC ((volatile struct Rtc *)0x1EC47100)
// Set after writing an alarm so the DS reads the new bytes.
#define RTC_ALARM1_NEW 0x20
#define RTC_ALARM2_NEW 0x40

// --- I2C bus 3 (the gyroscope's) ---------------------------------------------

struct I2c
{
    uint8_t data, cnt;
    uint16_t cntex, scl;
};
#define I2C ((volatile struct I2c *)0x1EC48000)
#define I2C_DATA (I2C->data)
#define I2C_CNT (I2C->cnt)
#define I2C_CNTEX (I2C->cntex)
#define I2C_SCL (I2C->scl)

#define I2C_STOP 0x01
#define I2C_START 0x02
#define I2C_ERROR 0x04
#define I2C_ACK 0x10
#define I2C_READ 0x20
#define I2C_IRQ 0x40
#define I2C_BUSY 0x80

// The gyroscope (ITG-3200 family, measured on an Old 3DS): asleep until its
// power register is cleared; rates big-endian X, Y, Z from 0x1D.
#define GYRO 0xD0
#define GYRO_POWER 0x3E
#define GYRO_RANGE 0x16
#define GYRO_RANGE_2000 0x18 // +-2000 degrees/s, 256 Hz filter
#define GYRO_RATES 0x1D

#define WAIT_LIMIT 20000
#define GIVE_UP 8

static int idle(void)
{
    for (int n = 0; n < WAIT_LIMIT; ++n)
        if (!(I2C_CNT & I2C_BUSY))
            return 1;
    return 0;
}

static int send(uint8_t value, uint8_t flags)
{
    I2C_DATA = value;
    I2C_CNT = I2C_BUSY | I2C_IRQ | flags;
    if (!idle())
        return 0;
    if (!(I2C_CNT & I2C_ACK))
    {
        I2C_CNT = I2C_BUSY | I2C_IRQ | I2C_ERROR | I2C_STOP;
        idle();
        return 0;
    }
    return 1;
}

static int begin(uint8_t reg, int read)
{
    if (!idle() || !send(GYRO, I2C_START) || !send(reg, 0))
        return 0;
    return !read || send(GYRO | 1, I2C_START);
}

static int gyroWrite(uint8_t reg, uint8_t value)
{
    if (!begin(reg, 0))
        return 0;
    return send(value, I2C_STOP);
}

static int gyroRates(int16_t rate[3])
{
    uint8_t raw[6];
    if (!begin(GYRO_RATES, 1))
        return 0;
    for (int n = 0; n < 6; ++n)
    {
        I2C_CNT = I2C_BUSY | I2C_IRQ | I2C_READ | (n == 5 ? I2C_STOP : I2C_ACK);
        if (!idle())
            return 0;
        raw[n] = I2C_DATA;
    }
    for (int k = 0; k < 3; ++k)
        rate[k] = (int16_t)(raw[2 * k] << 8 | raw[2 * k + 1]);
    return 1;
}

static void gyroStep(struct State *s)
{
    if (s->gyroOff)
        return;
    int ok;
    if (!s->gyroUp)
    {
        idle();
        I2C_CNTEX = 2; // the device may hold the clock
        I2C_SCL = 5 << 8;
        ok = gyroWrite(GYRO_POWER, 0) && gyroWrite(GYRO_RANGE, GYRO_RANGE_2000);
        s->gyroUp = (uint8_t)ok;
    }
    else
    {
        int16_t rate[3];
        ok = gyroRates(rate);
        if (ok)
            for (int k = 0; k < 3; ++k)
                s->angle[k] += rate[k];
    }
    s->failures = ok ? 0 : (uint8_t)(s->failures + 1);
    if (s->failures >= GIVE_UP)
        s->gyroOff = 1;
}

static void feedEntry(void)
{
    head.table[1]();

    struct State *s = head.state;
    gyroStep(s);

    const uint16_t x = head.pad[0], y = head.pad[1];
    s->count++;
    const int gyro = s->gyroUp && !s->gyroOff;
    const uint32_t yaw = (uint32_t)(s->angle[head.yawAxis] >> FEED_ANGLE_SHIFT) & 0xFFF;
    const uint32_t pitch = (uint32_t)(s->angle[head.pitchAxis] >> FEED_ANGLE_SHIFT) & 0xFFF;

    volatile struct Rtc *rtc = RTC;
    __asm__("" : "+r"(rtc)); // one base and offsets, not a literal per byte
    rtc->alarm1[2] = (uint8_t)(FEED_MARK | (gyro ? FEED_GYRO : 0) | (s->count & 3) << FEED_COUNT_SHIFT);
    rtc->alarm1[1] = (uint8_t)((x & 0xFFF) >> 4);
    rtc->alarm1[0] = (uint8_t)((y & 0xFFF) >> 4);
    rtc->alarm2[2] = (uint8_t)(yaw >> 4);
    rtc->alarm2[1] = (uint8_t)((yaw & 0xF) << 4 | pitch >> 8);
    rtc->alarm2[0] = (uint8_t)pitch;
    rtc->cnt |= RTC_ALARM1_NEW | RTC_ALARM2_NEW;
}
