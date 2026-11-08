#include "rtcom.h"

#include <nds.h>

// REG_RTCCNT's bits: data, clock, select, and the three that make each an
// output.
#define RTC_DATA 0x01
#define RTC_CLOCK 0x02
#define RTC_SELECT 0x04
#define RTC_DATA_OUT 0x10
#define RTC_CLOCK_OUT 0x20
#define RTC_SELECT_OUT 0x40

static int bitDelay = 14;
static uint16_t savedRcnt;

void rtcomSetFast(bool fast)
{
    bitDelay = fast ? 1 : 14;
}

static inline void pause(int n)
{
    for (volatile int i = n; i > 0; --i)
    {
    }
}

static void clockOut(uint8_t value)
{
    for (int bit = 7; bit >= 0; --bit)
    {
        const uint8_t d = (value >> bit) & 1;
        REG_RTCCNT8 = RTC_SELECT_OUT | RTC_SELECT | RTC_CLOCK_OUT | RTC_DATA_OUT | d;
        pause(bitDelay);
        REG_RTCCNT8 = RTC_SELECT_OUT | RTC_SELECT | RTC_CLOCK_OUT | RTC_CLOCK | RTC_DATA_OUT | d;
        pause(bitDelay);
    }
}

static uint8_t clockIn(void)
{
    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit)
    {
        REG_RTCCNT8 = RTC_SELECT_OUT | RTC_SELECT | RTC_CLOCK_OUT;
        pause(bitDelay);
        REG_RTCCNT8 = RTC_SELECT_OUT | RTC_SELECT | RTC_CLOCK_OUT | RTC_CLOCK;
        pause(bitDelay);
        value = (uint8_t)(value << 1 | (REG_RTCCNT8 & RTC_DATA));
    }
    return value;
}

static void transfer(const uint8_t *out, int outCount, uint8_t *in, int inCount)
{
    REG_RTCCNT8 = RTC_SELECT_OUT | RTC_CLOCK_OUT | RTC_CLOCK | RTC_DATA_OUT | RTC_DATA;
    pause(2);
    REG_RTCCNT8 = RTC_SELECT_OUT | RTC_SELECT | RTC_CLOCK_OUT | RTC_CLOCK | RTC_DATA_OUT | RTC_DATA;
    pause(2);
    for (int n = 0; n < outCount; ++n)
        clockOut(out[n]);
    for (int n = 0; n < inCount; ++n)
        in[n] = clockIn();
    pause(2);
    REG_RTCCNT8 = RTC_SELECT_OUT | RTC_CLOCK_OUT | RTC_CLOCK;
    pause(2);
}

void rtcomBegin(void)
{
    savedRcnt = REG_RCNT;
    REG_IF = IRQ_RTC;
    REG_RCNT = 0x8100;
    REG_IF = IRQ_RTC;
}

void rtcomEnd(void)
{
    REG_IF = IRQ_RTC;
    REG_RCNT = savedRcnt;
}

void rtcomWriteData(uint8_t value)
{
    const uint8_t out[2] = {0x6C, value};
    transfer(out, 2, 0, 0);
}

uint8_t rtcomReadData(void)
{
    const uint8_t out = 0x6D;
    uint8_t in = 0;
    transfer(&out, 1, &in, 1);
    return in;
}

void rtcomWriteStatus(uint8_t value)
{
    const uint8_t out[2] = {0x6E, value};
    transfer(out, 2, 0, 0);
}

static uint8_t readStatus(void)
{
    const uint8_t out = 0x6F;
    uint8_t in = 0;
    transfer(&out, 1, &in, 1);
    return in;
}

void rtcomWriteExtra(const uint8_t bytes[3])
{
    const uint8_t out[4] = {0x6A, bytes[0], bytes[1], bytes[2]};
    transfer(out, 4, 0, 0);
}

void rtcomReadExtra(uint8_t bytes[3])
{
    const uint8_t out = 0x6B;
    transfer(&out, 1, bytes, 3);
}

int rtcomWait(void)
{
    // About 100 ms: a 3DS frame is 16.7, the hook may be busy for one.
    for (int n = 0; n < 1000000; ++n)
    {
        if (REG_IF & IRQ_RTC)
        {
            REG_IF = IRQ_RTC;
            return readStatus();
        }
    }
    REG_IF = IRQ_RTC;
    return -1;
}

bool rtcomAsk(uint8_t status, uint8_t data)
{
    rtcomWriteData(data);
    rtcomWriteStatus(status);
    return rtcomWait() == RTCOM_ACK;
}

bool rtcomUpload(const uint8_t *code, uint32_t size, int32_t *failAt)
{
    *failAt = -1;
    if (!rtcomAsk(RTCOM_UPLOAD, size & 0xFF) || !rtcomAsk(RTCOM_NEXT, (size >> 8) & 0xFF) ||
        !rtcomAsk(RTCOM_NEXT, (size >> 16) & 0xFF) || !rtcomAsk(RTCOM_NEXT, size >> 24))
    {
        *failAt = -2;
        return false;
    }
    for (uint32_t n = 0; n < size; ++n)
    {
        if (!rtcomAsk(RTCOM_NEXT, code[n]))
        {
            *failAt = (int32_t)n;
            return false;
        }
    }
    // The upload command ends on a "no"; then the code is made runnable.
    rtcomWriteStatus(RTCOM_SAY_NO);
    if (rtcomWait() != RTCOM_NAK)
    {
        *failAt = -3;
        return false;
    }
    rtcomWriteStatus(RTCOM_MAKE_RUNNABLE);
    if (rtcomWait() != RTCOM_ACK)
    {
        *failAt = -4;
        return false;
    }
    return true;
}

bool rtcomRun(uint8_t first, uint8_t *reply)
{
    if (!rtcomAsk(RTCOM_RUN, first))
        return false;
    *reply = rtcomReadData();
    return true;
}

bool rtcomNext(uint8_t value, uint8_t *reply)
{
    if (!rtcomAsk(RTCOM_NEXT, value))
        return false;
    *reply = rtcomReadData();
    return true;
}

void rtcomClose(void)
{
    rtcomWriteStatus(RTCOM_CLOSE);
    rtcomWait();
    rtcomWriteStatus(RTCOM_NAK);
}
