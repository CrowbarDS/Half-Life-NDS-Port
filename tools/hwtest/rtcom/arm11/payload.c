// The rtcom probe's ARM11 program. TWPatch's TwlBg loads it at 0x201000 and
// calls it, from its frame hook, with (byte, iterator); it returns the answer
// byte. It runs in TwlBg's own time, so every wait here has an end: a stuck
// bus must not stop the 3DS's picture.
//
// Addresses are TwlBg's: the I/O at 0x10100000 is seen at 0x1EC00000.

#include <stdint.h>

#include "payload_ops.h"

typedef volatile uint8_t vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;

// The clock as the DS sees it: its control byte, and the second alarm's
// three bytes (the DS's first byte at the top).
#define RTC_CNT (*(vu8 *)0x1EC47100)
#define RTC_ALARM1 ((vu8 *)0x1EC47130)
#define RTC_ALARM2 ((vu8 *)0x1EC47134)
// Set after writing the alarm, so that the DS reads the new bytes.
#define RTC_ALARM2_NEW 0x40
// The first alarm's, a guess from the order of the others (date 0x10).
#define RTC_ALARM1_NEW 0x20

// The I2C buses (OP_I2C_BUS picks one; the third by default: the
// gyroscope, the infrared, the New 3DS's extra controls). Data, control,
// extended control, clock.
static const uint32_t kBuses[3] = {0x1EC61000, 0x1EC44000, 0x1EC48000};
static uint32_t bus = 0x1EC48000;
#define I2C_DATA (*(vu8 *)bus)
#define I2C_CNT (*(vu8 *)(bus + 1))
#define I2C_CNTEX (*(vu16 *)(bus + 2))
#define I2C_SCL (*(vu16 *)(bus + 4))

#define I2C_STOP 0x01
#define I2C_START 0x02
#define I2C_ERROR 0x04
#define I2C_ACK 0x10
#define I2C_READ 0x20
#define I2C_IRQ 0x40
#define I2C_BUSY 0x80

#define WAIT_LIMIT 200000

static uint8_t command = 0;
static uint32_t address = 0;
static uint32_t word = 0;
static uint32_t padAt = 0; // the circle pad in TwlBg's memory, once found
static uint8_t device = 0, reg = 0;
static uint8_t buffer[16] = {0};
static uint8_t i2cReady = 0;
static uint8_t lastCnt = 0; // the control byte after the last byte sent

// TwlBg's run-time code: where to look for its input routine.
#define TWLBG_LOW 0x100000
#define TWLBG_LOW_END 0x127B00

static uint32_t padFind(void)
{
    static const uint8_t split[10] = {0x61, 0x68, 0x08, 0x05, 0x00, 0x0D, 0xA8, 0x81, 0x08, 0x02};
    for (uint32_t a = TWLBG_LOW; a < TWLBG_LOW_END - sizeof split; a += 2)
    {
        unsigned k = 0;
        while (k < sizeof split && ((const vu8 *)a)[k] == split[k])
            ++k;
        if (k < sizeof split)
            continue;
        // The nearest "ldr r5, [pc, #imm]" before it.
        for (uint32_t back = 2; back < 0x80; back += 2)
        {
            const uint16_t insn = *(const vu16 *)(a - back);
            if (insn >> 8 == 0x4D)
            {
                const uint32_t lit = ((a - back + 4) & ~3u) + (insn & 0xFFu) * 4;
                return *(const vu32 *)lit + 12;
            }
        }
        return 0;
    }
    return 0;
}

static void alarmSet(uint8_t a, uint8_t b, uint8_t c)
{
    RTC_ALARM2[2] = a;
    RTC_ALARM2[1] = b;
    RTC_ALARM2[0] = c;
    RTC_CNT |= RTC_ALARM2_NEW;
}

static int i2cIdle(void)
{
    for (int n = 0; n < WAIT_LIMIT; ++n)
        if (!(I2C_CNT & I2C_BUSY))
            return 1;
    return 0;
}

static void i2cInit(void)
{
    if (i2cReady)
        return;
    i2cIdle();
    I2C_CNTEX = 2; // the device may hold the clock
    I2C_SCL = 5 << 8; // (the clock Linux for the 3DS uses on these buses)
    i2cReady = 1;
}

// One byte out; 1 if the device acknowledged.
static int i2cSend(uint8_t value, uint8_t flags)
{
    I2C_DATA = value;
    I2C_CNT = I2C_BUSY | I2C_IRQ | flags;
    if (!i2cIdle())
    {
        lastCnt = 0xFF;
        return 0;
    }
    lastCnt = I2C_CNT;
    if (!(lastCnt & I2C_ACK))
    {
        I2C_CNT = I2C_BUSY | I2C_IRQ | I2C_ERROR | I2C_STOP;
        i2cIdle();
        return 0;
    }
    return 1;
}

static uint8_t i2cReceive(uint8_t flags)
{
    I2C_CNT = I2C_BUSY | I2C_IRQ | I2C_READ | flags;
    i2cIdle();
    return I2C_DATA;
}

// The device and register chosen, and turned round to read if asked.
static int i2cBegin(uint8_t dev, uint8_t r, int read)
{
    for (int tries = 0; tries < 4; ++tries)
    {
        if (!i2cIdle())
            return 0;
        if (!i2cSend(dev, I2C_START))
            continue;
        if (!i2cSend(r, 0))
            continue;
        if (read && !i2cSend(dev | 1, I2C_START))
            continue;
        return 1;
    }
    return 0;
}

static int i2cRead(uint8_t dev, uint8_t r, uint8_t *out, int count)
{
    i2cInit();
    if (!i2cBegin(dev, r, 1))
        return 0;
    for (int n = 0; n < count - 1; ++n)
        out[n] = i2cReceive(I2C_ACK);
    out[count - 1] = i2cReceive(I2C_STOP);
    return 1;
}

static int i2cWrite(uint8_t dev, uint8_t r, uint8_t value)
{
    i2cInit();
    if (!i2cBegin(dev, r, 0))
        return 0;
    I2C_DATA = value;
    I2C_CNT = I2C_BUSY | I2C_IRQ | I2C_STOP;
    i2cIdle();
    return (I2C_CNT & I2C_ACK) != 0;
}

__attribute__((section(".text.entry"), target("arm"))) uint32_t payloadEntry(uint32_t byte, uint32_t iter)
{
    byte &= 0xFF;
    if (iter == 0)
    {
        command = (uint8_t)byte;
        address = 0;
        return command == OP_ECHO ? PAYLOAD_HELLO : 0;
    }
    switch (command)
    {
    case OP_ECHO:
        alarmSet(RTC_ALARM2[2] + 1, RTC_ALARM2[1] + 1, RTC_ALARM2[0] + 1);
        return byte ^ 0xFF;
    case OP_PEEK:
        if (iter <= 4)
        {
            address |= byte << (8 * (iter - 1));
            if (iter < 4)
                return 0;
            word = *(vu32 *)address;
            alarmSet((uint8_t)(word >> 8), (uint8_t)(word >> 16), (uint8_t)(word >> 24));
            return word & 0xFF;
        }
        return iter <= 7 ? (word >> (8 * (iter - 4))) & 0xFF : 0;
    case OP_I2C_READ:
        if (iter == 1)
            device = (uint8_t)byte;
        else if (iter == 2)
            reg = (uint8_t)byte;
        else if (iter == 3)
        {
            const int count = byte < 1 ? 1 : (byte > 16 ? 16 : (int)byte);
            for (int n = 0; n < 16; ++n)
                buffer[n] = 0;
            return (uint32_t)i2cRead(device, reg, buffer, count);
        }
        else
            return iter - 4 < 16 ? buffer[iter - 4] : 0;
        return 0;
    case OP_I2C_WRITE:
        if (iter == 1)
            device = (uint8_t)byte;
        else if (iter == 2)
            reg = (uint8_t)byte;
        else if (iter == 3)
            return (uint32_t)i2cWrite(device, reg, (uint8_t)byte);
        return 0;
    case OP_I2C_PROBE:
        if (iter != 1)
            return 0;
        i2cInit();
        if (!i2cIdle())
            return 0xFF;
        if (i2cSend((uint8_t)byte, I2C_START))
        {
            // (A register number with the stop, so the device is let go.)
            I2C_DATA = 0;
            I2C_CNT = I2C_BUSY | I2C_IRQ | I2C_STOP;
            i2cIdle();
        }
        return lastCnt;
    case OP_RTC_FILL:
        if (iter != 1)
            return 0;
        RTC_ALARM1[2] = (uint8_t)byte;
        RTC_ALARM1[1] = (uint8_t)(byte + 1);
        RTC_ALARM1[0] = (uint8_t)(byte + 2);
        RTC_CNT |= RTC_ALARM1_NEW;
        alarmSet((uint8_t)(byte + 3), (uint8_t)(byte + 4), (uint8_t)(byte + 5));
        return RTC_CNT;
    case OP_PAD_FIND:
        if (iter == 1)
        {
            padAt = padFind();
            return padAt != 0;
        }
        return iter <= 5 ? (padAt >> (8 * (iter - 2))) & 0xFF : 0;
    case OP_PAD_READ:
        if (iter != 1)
            return 0;
        if (!padAt)
            padAt = padFind();
        if (!padAt)
            return 0;
        word = *(vu32 *)padAt;
        alarmSet((uint8_t)(word >> 8), (uint8_t)(word >> 16), (uint8_t)(word >> 24));
        return word & 0xFF;
    case OP_I2C_BUS:
        if (iter == 1 && byte < 3)
        {
            bus = kBuses[byte];
            i2cReady = 0;
            return 1;
        }
        return 0;
    default:
        return PAYLOAD_UNKNOWN;
    }
}
