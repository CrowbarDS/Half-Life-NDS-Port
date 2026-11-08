// The rtcom probe's ARM7: carries out what the ARM9 asks for and writes what
// it finds into the shared block (common/probe.h says what each test asks).
// The clock interrupt stays off: rtcom's answers come through it, and the
// probe polls them.

#include <nds.h>
#include <string.h>

#include "payload_ops.h"
#include "probe.h"
#include "rtcom.h"
#include "rtcom11_bin.h"

static ProbeShared *shared;

static void where(const char *text)
{
    int n = 0;
    for (; text[n] && n < 31; ++n)
        shared->where[n] = text[n];
    shared->where[n] = 0;
    shared->step++;
}

// Timers 0 and 1 cascaded: a 32-bit count of bus ticks (33.5 MHz).
static void startClock(void)
{
    TIMER_CR(0) = 0;
    TIMER_CR(1) = 0;
    TIMER_DATA(0) = 0;
    TIMER_DATA(1) = 0;
    TIMER_CR(1) = TIMER_ENABLE | TIMER_CASCADE;
    TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1;
}

static uint32_t ticks(void)
{
    uint16_t high = TIMER_DATA(1);
    uint16_t low = TIMER_DATA(0);
    const uint16_t again = TIMER_DATA(1);
    if (again != high)
    {
        high = again;
        low = TIMER_DATA(0);
    }
    return (uint32_t)high << 16 | low;
}

// --- 1: is it a 3DS, and does rtcom answer --------------------------------

static void detect(void)
{
    where("detect");
    shared->dsiMode = isDSiMode();
    shared->is3ds = 0;
    shared->mcuBefore = shared->mcuAfter = -1;
    if (!isDSiMode())
        return;
    // The DSi's MCU keeps what is written to 0x71; the 3DS's does not.
    const uint8_t before = i2cReadRegister(I2C_PM, 0x71);
    i2cWriteRegister(I2C_PM, 0x71, 0xD2);
    const uint8_t after = i2cReadRegister(I2C_PM, 0x71);
    i2cWriteRegister(I2C_PM, 0x71, before);
    shared->mcuBefore = before;
    shared->mcuAfter = after;
    shared->is3ds = after != 0xD2;
}

static bool ping(void)
{
    where("ping");
    // A command TwlBg does not know: rtcom answers "no".
    const uint32_t t0 = ticks();
    rtcomWriteStatus(0x01);
    const int status = rtcomWait();
    shared->pingTicks = ticks() - t0;
    shared->pingStatus = status;
    return status == RTCOM_NAK;
}

// --- 2: upload and echo ---------------------------------------------------

static bool upload(void)
{
    where("upload");
    rtcomSetFast(false);
    int32_t failAt = -1;
    const uint32_t t0 = ticks();
    shared->uploadBytes = rtcom11_bin_size;
    const bool ok = rtcomUpload(rtcom11_bin, rtcom11_bin_size, &failAt);
    shared->uploadTicks = ticks() - t0;
    shared->uploadFailAt = failAt;
    shared->uploadOk = ok;
    return ok;
}

static void echo(void)
{
    where("echo");
    uint8_t reply = 0;
    shared->helloByte = rtcomRun(OP_ECHO, &reply) ? reply : -1;
    int right = 0, extraRight = 0;
    uint32_t low = 0xFFFFFFFF, high = 0;
    for (int n = 0; n < 8; ++n)
    {
        const uint8_t sent[3] = {(uint8_t)(0x10 * n), (uint8_t)(0x10 * n + 5), (uint8_t)(0xF0 - n)};
        rtcomWriteExtra(sent);
        const uint32_t t0 = ticks();
        const bool ok = rtcomNext((uint8_t)(0x31 + n), &reply);
        const uint32_t took = ticks() - t0;
        low = took < low ? took : low;
        high = took > high ? took : high;
        if (ok && reply == (uint8_t)~(0x31 + n))
            ++right;
        uint8_t back[3] = {0};
        rtcomReadExtra(back);
        if (n == 0)
            for (int k = 0; k < 3; ++k)
                shared->echoExtra[k] = back[k];
        if (back[0] == (uint8_t)(sent[0] + 1) && back[1] == (uint8_t)(sent[1] + 1) &&
            back[2] == (uint8_t)(sent[2] + 1))
            ++extraRight;
    }
    shared->echoOk = right == 8;
    shared->echoExtraOk = extraRight == 8;
    shared->exchangeTicksMin = low;
    shared->exchangeTicksMax = high;

    // The same round trip with the short bit delay.
    rtcomSetFast(true);
    rtcomRun(OP_ECHO, &reply);
    const uint32_t t0 = ticks();
    rtcomNext(0x55, &reply);
    shared->fastTicks = reply == 0xAA ? ticks() - t0 : 0xFFFFFFFF;
}

// --- 3: words from the ARM11's side ---------------------------------------

// A word, by the alarm path; *check gets it by the byte path too.
static bool peek(uint32_t address, uint32_t *value, uint32_t *check)
{
    uint8_t b0 = 0, r = 0;
    if (!rtcomRun(OP_PEEK, &r))
        return false;
    for (int k = 0; k < 3; ++k)
        if (!rtcomNext((address >> (8 * k)) & 0xFF, &r))
            return false;
    if (!rtcomNext(address >> 24, &b0))
        return false;
    uint8_t extra[3] = {0};
    rtcomReadExtra(extra);
    *value = b0 | (uint32_t)extra[0] << 8 | (uint32_t)extra[1] << 16 | (uint32_t)extra[2] << 24;
    uint32_t slow = b0;
    for (int k = 1; k < 4; ++k)
    {
        if (!rtcomNext(0, &r))
            return false;
        slow |= (uint32_t)r << (8 * k);
    }
    *check = slow;
    return true;
}

static void displayRegisters(void)
{
    where("display registers");
    rtcomSetFast(false);
    int good = 0, tried = 0;
    uint32_t v = 0, c = 0;
#define PEEK(addr, dst)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        ++tried;                                                                                                       \
        if (peek(addr, &v, &c))                                                                                        \
        {                                                                                                              \
            dst = v;                                                                                                   \
            good += v == c;                                                                                            \
        }                                                                                                              \
        else                                                                                                           \
            dst = 0xDEADDEAD;                                                                                          \
    } while (0)
    for (int k = 0; k < 4; ++k)
        PEEK(0x1EC11000 + 4 * k, shared->scaler[k]);
    PEEK(0x1EC11300, shared->xmatrix[0]);
    PEEK(0x1EC11304, shared->xmatrix[1]);
    PEEK(0x1ED02218, shared->window[0]);
    PEEK(0x1ED0221C, shared->window[1]);
    PEEK(0x1EC46000, shared->hid);
#undef PEEK
    shared->peekOk = good == tried;
}

// --- 4: the gyroscope -----------------------------------------------------

static int i2cRead(uint8_t dev, uint8_t reg, uint8_t *out, int count)
{
    uint8_t r = 0;
    if (!rtcomRun(OP_I2C_READ, &r) || !rtcomNext(dev, &r) || !rtcomNext(reg, &r) || !rtcomNext((uint8_t)count, &r))
        return -1;
    if (r != 1)
        return 0;
    for (int n = 0; n < count; ++n)
        if (!rtcomNext(0, &out[n]))
            return -1;
    return 1;
}

static int i2cWrite(uint8_t dev, uint8_t reg, uint8_t value)
{
    uint8_t r = 0;
    if (!rtcomRun(OP_I2C_WRITE, &r) || !rtcomNext(dev, &r) || !rtcomNext(reg, &r) || !rtcomNext(value, &r))
        return -1;
    return r;
}

static void gyroScan(void)
{
    where("gyro scan");
    static const uint8_t devs[2] = {0xD0, 0xD2};
    static const uint8_t regs[2] = {0x00, 0x75};
    shared->gyroKind = GYRO_NONE;
    shared->gyroAddr = 0;
    for (int d = 0; d < 2; ++d)
    {
        for (int r = 0; r < 2; ++r)
        {
            uint8_t value = 0;
            const int got = i2cRead(devs[d], regs[r], &value, 1);
            shared->gyroAck[d][r] = got == 1 ? value : -1;
        }
        // WHO_AM_I holds the device's own 7-bit address in both families.
        const uint8_t seven = devs[d] >> 1;
        if (shared->gyroKind == GYRO_NONE && (shared->gyroAck[d][0] & 0x7E) == (seven & 0x7E))
        {
            shared->gyroKind = GYRO_ITG;
            shared->gyroAddr = devs[d];
        }
        else if (shared->gyroKind == GYRO_NONE && shared->gyroAck[d][1] >= 0 && shared->gyroAck[d][1] != 0xFF &&
                 shared->gyroAck[d][1] != 0)
        {
            shared->gyroKind = GYRO_MPU;
            shared->gyroAddr = devs[d];
        }
    }
}

static void gyroWake(void)
{
    if (shared->gyroKind == GYRO_ITG)
    {
        i2cWrite(shared->gyroAddr, 0x3E, 0x00); // clock and power: running
        i2cWrite(shared->gyroAddr, 0x16, 0x18); // full scale, 256 Hz filter
    }
    else if (shared->gyroKind == GYRO_MPU)
    {
        i2cWrite(shared->gyroAddr, 0x6B, 0x00); // out of sleep
    }
}

static void gyroRead(void)
{
    uint8_t raw[6] = {0};
    const uint8_t reg = shared->gyroKind == GYRO_ITG ? 0x1D : 0x43;
    if (i2cRead(shared->gyroAddr, reg, raw, 6) == 1)
    {
        for (int k = 0; k < 3; ++k)
            shared->gyro[k] = (int16_t)(raw[2 * k] << 8 | raw[2 * k + 1]);
        shared->gyroReads++;
    }
    else
        shared->gyroFails++;
}

static void gyroDump(void)
{
    where("gyro dump");
    // The read path checked where the answer is known: the infrared
    // bridge's scratch register gives back what was written to it.
    static const uint8_t patterns[2] = {0x5A, 0xA5};
    for (int k = 0; k < 2; ++k)
    {
        uint8_t back = 0;
        shared->irScratch[k] =
            i2cWrite(0x9A, 0x38, patterns[k]) == 1 && i2cRead(0x9A, 0x38, &back, 1) == 1 ? back : -1;
    }
    if (shared->gyroAddr == 0)
        shared->gyroAddr = 0xD0;
    static const uint8_t single[3] = {0x00, 0x16, 0x3E};
    for (int k = 0; k < 3; ++k)
    {
        uint8_t v = 0;
        shared->gyroSingle[k] = i2cRead(shared->gyroAddr, single[k], &v, 1) == 1 ? v : -1;
    }
    shared->gyroDumpRan = 1;
    for (int base = 0; base < 128; base += 16)
    {
        uint8_t chunk[16] = {0};
        if (i2cRead(shared->gyroAddr, (uint8_t)base, chunk, 16) != 1)
            memset(chunk, 0xEE, sizeof chunk);
        else
            shared->gyroDumpRan++;
        for (int n = 0; n < 16; ++n)
            shared->gyroRegs[base + n] = chunk[n];
    }
}

static void busScan(int index)
{
    where("bus scan");
    uint8_t r = 0;
    shared->busScanned = -1;
    shared->busAnswered = 0;
    if (!rtcomRun(OP_I2C_BUS, &r) || !rtcomNext((uint8_t)index, &r))
        return;
    // (Only the ordinary addresses, 0x08-0x77: 0x00 is the call every
    // device on the bus obeys.)
    for (int n = 0; n < 128; ++n)
        shared->busCnt[n] = 0;
    for (int n = 0x08; n <= 0x77; ++n)
    {
        uint8_t cnt = 0xEE;
        if (!rtcomRun(OP_I2C_PROBE, &r) || !rtcomNext((uint8_t)(n << 1), &cnt))
            cnt = 0xEE; // (rtcom itself failed)
        shared->busCnt[n] = cnt;
        if (cnt != 0xFF && cnt != 0xEE && (cnt & 0x10))
        {
            if (shared->busAnswered < 32)
                shared->busAddr[shared->busAnswered] = (uint8_t)(n << 1);
            shared->busAnswered++;
        }
    }
    // Back to the third bus, the one the gyroscope tests use.
    if (rtcomRun(OP_I2C_BUS, &r))
        rtcomNext(2, &r);
    shared->busScanned = index;
}

// --- 5: the circle pad through the codec ----------------------------------

static void padRead(void)
{
    uint8_t raw[0x34];
    cdcReadRegArray(0xFB, 0x01, raw, sizeof raw);
    for (unsigned n = 0; n < sizeof raw; ++n)
        shared->padRaw[n] = raw[n];
    const int x = ((raw[0x24] << 8 | raw[0x25]) & 0xFFF) - 2048;
    const int y = ((raw[0x14] << 8 | raw[0x15]) & 0xFFF) - 2048;
    if (shared->padReads == 0)
    {
        shared->padXmin = shared->padXmax = x;
        shared->padYmin = shared->padYmax = y;
    }
    shared->padX = x;
    shared->padY = y;
    if (x < shared->padXmin)
        shared->padXmin = x;
    if (x > shared->padXmax)
        shared->padXmax = x;
    if (y < shared->padYmin)
        shared->padYmin = y;
    if (y > shared->padYmax)
        shared->padYmax = y;
    shared->padReads++;
}

static void padStart(bool enable)
{
    where(enable ? "pad on" : "pad scan");
    shared->cdc67[0] = cdcReadReg(0x67, 0x24);
    shared->cdc67[1] = cdcReadReg(0x67, 0x25);
    shared->cdc67[2] = cdcReadReg(0x67, 0x26);
    if (enable)
    {
        // Linux's "touch on": 26 bit 7 set, 24 bit 7 clear, 25 bits 2-5 to 4.
        cdcWriteRegMask(0x67, 0x26, 0x80, 0x80);
        cdcWriteRegMask(0x67, 0x24, 0x80, 0x00);
        cdcWriteRegMask(0x67, 0x25, 0x3C, 0x10);
        shared->padOn = 1;
    }
    shared->padReads = 0;
}

// --- the session ----------------------------------------------------------

static bool sessionOpen = false;
static bool payloadIn = false;

static bool openSession(void)
{
    if (!shared->is3ds)
        return false;
    rtcomBegin();
    if (!ping())
    {
        rtcomEnd();
        return false;
    }
    sessionOpen = true;
    return true;
}

static void closeSession(void)
{
    if (!sessionOpen)
        return;
    rtcomClose();
    rtcomEnd();
    sessionOpen = false;
}

static void runAuto(void)
{
    detect();
    if (!openSession())
        return;
    if (!payloadIn)
        payloadIn = upload();
    if (payloadIn)
    {
        echo();
        displayRegisters();
        gyroScan();
    }
    closeSession();
}

int main(void)
{
    irqInit();
    fifoInit();
    installSystemFIFO();
    irqEnable(IRQ_VBLANK); // (for swiWaitForVBlank)
    startClock();

    while (!fifoCheckAddress(PROBE_FIFO))
        swiWaitForVBlank();
    shared = (ProbeShared *)fifoGetAddress(PROBE_FIFO);
    shared->busScanned = -1;
    detect();

    bool gyroAwake = false;
    uint32_t padMode = 0;
    while (1)
    {
        swiWaitForVBlank();
        const uint32_t command = shared->command;
        shared->running = command;
        switch (command)
        {
        case PROBE_AUTO:
            runAuto();
            shared->command = PROBE_IDLE;
            break;
        case PROBE_GYRO_LIVE:
            if (payloadIn && shared->gyroKind != GYRO_NONE && openSession())
            {
                if (!gyroAwake)
                {
                    gyroWake();
                    gyroAwake = true;
                }
                rtcomSetFast(true);
                gyroRead();
                closeSession();
            }
            break;
        case PROBE_GYRO_DUMP:
            if (payloadIn && openSession())
            {
                rtcomSetFast(false);
                gyroDump();
                closeSession();
            }
            shared->command = PROBE_IDLE;
            break;
        case PROBE_PAD_SCAN:
        case PROBE_PAD_ON:
            if (padMode != command)
            {
                padMode = command;
                padStart(command == PROBE_PAD_ON);
            }
            padRead();
            break;
        case PROBE_BUS_SCAN:
            if (payloadIn && openSession())
            {
                rtcomSetFast(false);
                busScan(2);
                closeSession();
            }
            shared->command = PROBE_IDLE;
            break;
        case PROBE_HID_LIVE:
            if (payloadIn && openSession())
            {
                uint32_t v = 0, c = 0;
                rtcomSetFast(true);
                if (peek(0x1EC46000, &v, &c))
                    shared->hid = v;
                closeSession();
            }
            break;
        default:
            break;
        }
        if (command != PROBE_PAD_SCAN && command != PROBE_PAD_ON)
            padMode = 0;
    }
    return 0;
}
