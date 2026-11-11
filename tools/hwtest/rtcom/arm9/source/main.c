// The rtcom probe's ARM9: the buttons pick a test, the screen shows what the
// ARM7 found (common/probe.h). SELECT turns the page.

#include <nds.h>
#include <stdio.h>

#include "probe.h"

static ProbeShared block __attribute__((aligned(32)));

static const char *commandName(uint32_t c)
{
    switch (c)
    {
    case PROBE_IDLE: return "idle";
    case PROBE_AUTO: return "auto";
    case PROBE_GYRO_LIVE: return "gyro live";
    case PROBE_GYRO_DUMP: return "gyro dump";
    case PROBE_PAD_SCAN: return "pad scan";
    case PROBE_PAD_ON: return "pad on";
    case PROBE_HID_LIVE: return "3DS buttons";
    case PROBE_BUS_SCAN: return "bus scan";
    case PROBE_CHANNEL: return "alarms";
    case PROBE_FEED: return "feed";
    case PROBE_PAD_TWL: return "3DS pad";
    default: return "?";
    }
}

// "--": no answer; else the byte. (Four slots, one per value on a line.)
static const char *hex(int32_t v, int slot)
{
    static char text[4][4];
    if (v < 0)
        return "--";
    snprintf(text[slot], sizeof text[slot], "%02lX", (unsigned long)(v & 0xFF));
    return text[slot];
}

static void pageSummary(ProbeShared *s)
{
    printf("DSi mode %d  MCU 71: %02lX->%02lX\n", (int)s->dsiMode, (unsigned long)(s->mcuBefore & 0xFF),
           (unsigned long)(s->mcuAfter & 0xFF));
    printf("3DS: %s\n", s->is3ds ? "yes" : "no");
    printf("ping: %02lX in %lu ticks\n", (unsigned long)(s->pingStatus & 0xFF), (unsigned long)s->pingTicks);
    if (s->pingStatus != 0x82)
        printf(" (82 expected: TWPatch rtcom)\n");
    printf("upload %s %lu B, %lu ms\n", s->uploadOk ? "ok" : "FAIL", (unsigned long)s->uploadBytes,
           (unsigned long)(s->uploadTicks / 33514));
    if (!s->uploadOk)
        printf(" failed at %ld\n", (long)s->uploadFailAt);
    printf("hello %02lX  echo %s  extra %s\n", (unsigned long)(s->helloByte & 0xFF), s->echoOk ? "ok" : "BAD",
           s->echoExtraOk ? "ok" : "BAD");
    printf(" extra back %02lX %02lX %02lX (01 06 F1)\n", (unsigned long)s->echoExtra[0],
           (unsigned long)s->echoExtra[1], (unsigned long)s->echoExtra[2]);
    printf("exchange %lu..%lu fast %lu tk\n", (unsigned long)s->exchangeTicksMin,
           (unsigned long)s->exchangeTicksMax, (unsigned long)s->fastTicks);
    printf("peek paths agree: %s\n", s->peekOk ? "yes" : "NO");
    printf("scaler %08lX %08lX\n", (unsigned long)s->scaler[0], (unsigned long)s->scaler[1]);
    printf("       %08lX %08lX\n", (unsigned long)s->scaler[2], (unsigned long)s->scaler[3]);
    printf("xmatrix %08lX %08lX\n", (unsigned long)s->xmatrix[0], (unsigned long)s->xmatrix[1]);
    printf("window  %08lX %08lX\n", (unsigned long)s->window[0], (unsigned long)s->window[1]);
    printf("3DS keys %08lX\n", (unsigned long)s->hid);
    printf("gyro D0: %s %s  D2: %s %s\n", hex(s->gyroAck[0][0], 0), hex(s->gyroAck[0][1], 1),
           hex(s->gyroAck[1][0], 2), hex(s->gyroAck[1][1], 3));
    printf("gyro %s at %02lX\n", s->gyroKind == GYRO_ITG ? "ITG" : (s->gyroKind == GYRO_MPU ? "MPU" : "none"),
           (unsigned long)s->gyroAddr);
    printf("rates %6d %6d %6d\n", s->gyro[0], s->gyro[1], s->gyro[2]);
    printf(" reads %ld fails %ld\n", (long)s->gyroReads, (long)s->gyroFails);
}

static void pageGyroRegs(ProbeShared *s)
{
    if (!s->gyroDumpRan)
    {
        printf("no dump yet (A first, then R)\n");
        return;
    }
    printf("IR scratch 5A->%s A5->%s\n", hex(s->irScratch[0], 0), hex(s->irScratch[1], 1));
    printf("one-byte 00=%s 16=%s 3E=%s\n", hex(s->gyroSingle[0], 0), hex(s->gyroSingle[1], 1),
           hex(s->gyroSingle[2], 2));
    printf("gyro %02lX regs, %ld/8 rows read\n", (unsigned long)s->gyroAddr, (long)(s->gyroDumpRan - 1));
    for (int row = 0; row < 16; ++row)
    {
        printf("%02X:", row * 8);
        for (int n = 0; n < 8; ++n)
            printf(" %02X", s->gyroRegs[row * 8 + n]);
        printf("\n");
    }
}

static void pagePad(ProbeShared *s)
{
    printf("codec FB:01 stream, %lu reads\n", (unsigned long)s->padReads);
    printf("page 67: 24=%02X 25=%02X 26=%02X%s\n", s->cdc67[0], s->cdc67[1], s->cdc67[2],
           s->padOn ? " (set)" : "");
    printf("pad X %5ld [%5ld..%5ld]\n", (long)s->padX, (long)s->padXmin, (long)s->padXmax);
    printf("pad Y %5ld [%5ld..%5ld]\n", (long)s->padY, (long)s->padYmin, (long)s->padYmax);
    for (int row = 0; row < 7; ++row)
    {
        printf("%02X:", row * 8);
        for (int n = 0; n < 8 && row * 8 + n < 0x34; ++n)
            printf(" %02X", s->padRaw[row * 8 + n]);
        printf("\n");
    }
}

static void pageBus(ProbeShared *s)
{
    if (s->busScanned < 0)
    {
        printf("no scan yet (Left)\n");
        return;
    }
    printf("bus %ld: %ld answered\n", (long)s->busScanned + 1, (long)s->busAnswered);
    for (int n = 0; n < s->busAnswered && n < 32; ++n)
        printf("%02X%s", s->busAddr[n], n % 8 == 7 ? "\n" : " ");
    printf("\ncontrol byte after D0 %02X D2 %02X\n", s->busCnt[0xD0 >> 1], s->busCnt[0xD2 >> 1]);
    printf(" 9A %02X A0 %02X 4A %02X EE %02X\n", s->busCnt[0x9A >> 1], s->busCnt[0xA0 >> 1],
           s->busCnt[0x4A >> 1], s->busCnt[0xEE >> 1]);
}

static void pageChannel(ProbeShared *s)
{
    if (!s->chanRan && !s->chanFailed)
    {
        printf("not run yet (A first, then X)\n");
        return;
    }
    printf("alarm feed: %ld/8 patterns, %ld failed\n", (long)s->chanRan, (long)s->chanFailed);
    for (int a = 0; a < 2; ++a)
        printf("alarm %d lost bits %02X %02X %02X\n", a + 1, s->chanLost[a][0], s->chanLost[a][1],
               s->chanLost[a][2]);
    printf("(00 00 00: every bit carried)\n");
    for (int a = 0; a < 2; ++a)
        printf("0x55 -> alarm %d %02X %02X %02X\n", a + 1, s->chanGot55[a][0], s->chanGot55[a][1],
               s->chanGot55[a][2]);
    printf(" (want 55 56 57 / 58 59 5A)\n");
    printf("control after fill:\n");
    for (int n = 0; n < 8; ++n)
        printf(" %02X", s->chanCnt[n]);
    printf("\nplain 3-byte read: %lu ticks\n", (unsigned long)s->chanReadTicks);
}

static void pageTwlPad(ProbeShared *s)
{
    printf("circle pad from TwlBg (rtcom)\n");
    if (s->twlPadAt < 0)
    {
        printf("not looked for yet (A first)\n");
        return;
    }
    if (s->twlPadAt == 0)
    {
        printf("NOT FOUND in TwlBg's code\n");
        return;
    }
    printf("found at %08lX\n", (unsigned long)s->twlPadAt);
    printf("word %08lX\n", (unsigned long)s->twlPadWord);
    printf("X %5ld [%5ld..%5ld]\n", (long)s->twlX, (long)s->twlXmin, (long)s->twlXmax);
    printf("Y %5ld [%5ld..%5ld]\n", (long)s->twlY, (long)s->twlYmin, (long)s->twlYmax);
    printf("(signed, 0 = middle)\n");
    printf("reads %lu fails %lu\n", (unsigned long)s->twlReads, (unsigned long)s->twlFails);
    printf("one read %lu ticks (%lu us)\n", (unsigned long)s->twlTicks, (unsigned long)(s->twlTicks / 34));
    printf("(move the stick round its edge)\n");
}

static void pageFeed(ProbeShared *s)
{
    printf("our TwlBg's feed (no rtcom)\n");
    printf("reads %lu  marked %lu  moved %lu\n", (unsigned long)s->feedReads, (unsigned long)s->feedMarked,
           (unsigned long)s->feedMoved);
    printf("(moved ~ reads: alive each frame)\n");
    printf("raw %02X %02X %02X | %02X %02X %02X\n", s->feedRaw[0], s->feedRaw[1], s->feedRaw[2], s->feedRaw[3],
           s->feedRaw[4], s->feedRaw[5]);
    printf("(want Bx: B, gyro bit 0)\n");
    printf("pad X %3ld  Y %3ld (of 255)\n", (long)s->feedX, (long)s->feedY);
    printf("gyro %s  sums %03lX %03lX\n", s->feedGyro ? "on" : "OFF", (unsigned long)s->feedYaw,
           (unsigned long)s->feedPitch);
    printf("turned since Y: yaw %ld pitch %ld\n", (long)s->feedTurnYaw, (long)s->feedTurnPitch);
    printf(" (27 units ~ 1 degree)\n");
    printf("two reads: %lu ticks\n", (unsigned long)s->feedTicks);
}

int main(void)
{
    consoleDemoInit();
    // (Its lines out of the cache first: a dirty one written back later
    // would cover what the ARM7 wrote.)
    DC_FlushRange(&block, sizeof block);
    ProbeShared *shared = (ProbeShared *)memUncached(&block);
    fifoSendAddress(PROBE_FIFO, &block);

    int page = 0;
    while (1)
    {
        swiWaitForVBlank();
        scanKeys();
        const uint32_t down = keysDown();
        if (down & KEY_A)
            shared->command = PROBE_AUTO;
        if (down & KEY_L)
            shared->command = PROBE_GYRO_LIVE;
        if (down & KEY_R)
        {
            shared->command = PROBE_GYRO_DUMP;
            page = 1;
        }
        if (down & KEY_UP)
        {
            shared->command = PROBE_PAD_TWL;
            page = 6;
        }
        if (down & KEY_DOWN)
            shared->command = PROBE_HID_LIVE;
        if (down & KEY_LEFT)
        {
            shared->command = PROBE_BUS_SCAN;
            page = 3;
        }
        if (down & KEY_RIGHT)
        {
            shared->command = PROBE_PAD_ON;
            page = 2;
        }
        if (down & KEY_X)
        {
            shared->command = PROBE_CHANNEL;
            page = 4;
        }
        if (down & KEY_Y)
        {
            shared->command = PROBE_FEED;
            page = 5;
        }
        if (down & KEY_B)
            shared->command = PROBE_IDLE;
        if (down & KEY_SELECT)
            page = (page + 1) % 7;
        if (down & KEY_START)
            break;

        consoleClear();
        printf("rtcom probe  [%s] %s\n", commandName(shared->running), (const char *)shared->where);
        if (page == 0)
            pageSummary(shared);
        else if (page == 1)
            pageGyroRegs(shared);
        else if (page == 2)
            pagePad(shared);
        else if (page == 3)
            pageBus(shared);
        else if (page == 4)
            pageChannel(shared);
        else if (page == 5)
            pageFeed(shared);
        else
            pageTwlPad(shared);
        printf("A auto L gyro R regs Up 3DSpad\nX alarms Y feed Rt codec Lt bus Dn keys B stop");
    }
    return 0;
}
