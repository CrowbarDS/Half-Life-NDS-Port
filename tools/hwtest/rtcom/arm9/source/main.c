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
            shared->command = PROBE_PAD_SCAN;
            page = 2;
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
        if (down & KEY_B)
            shared->command = PROBE_IDLE;
        if (down & KEY_SELECT)
            page = (page + 1) % 4;
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
        else
            pageBus(shared);
        printf("A auto L gyro R regs Up pad\nRt pad+on Lt bus Dn keys B stop");
    }
    return 0;
}
