// The game's own ARM7 program: what BlocksDS's default (with the wireless
// library the Download Play build needs) does -- the touch screen and the
// buttons, the wifi, the card on a DSi, the clock -- and, instead of maxmod,
// the game's own sound (Audio.cpp), which plays what the ARM9 asks through
// one block of shared memory (shared/HlAudioLink.hpp); and, on a 3DS, the
// shape of its top screen (Rtcom.cpp).

#include <cstdint>

#include <dswifi7.h>
#include <nds.h>

#include "Audio.hpp"
#include "Rtcom.hpp"

// libnds's tables of what each interrupt calls, by its bit (the
// dispatcher's: interruptDispatcher.s); not in its headers.
extern "C" VoidFn irqTable[];
extern "C" VoidFn irqTableAUX[];

namespace
{
    volatile bool exitLoop = false;

    // (Diagnostic) what the ARM7 is in, for the ARM9 to say if it stops
    // (HlAudioLink.hpp's where): each interrupt marked as it runs, and what
    // it broke into marked again as it ends. The DSi's wireless card's
    // interrupt, and the FIFO's, are the libraries': called through ours.
    constexpr int kAuxWifiCard = 11; ///< (dswifi's card.twl.c: IRQ_WIFI_SDIO_CARDIRQ)
    constexpr int kFifoNotEmpty = 18;
    volatile VoidFn wifiCardIrq = nullptr, fifoIrq = nullptr;

    void wifiCardMarked()
    {
        const unsigned was = hlaudio::marked();
        hlaudio::mark(4);
        if (VoidFn call = wifiCardIrq)
            call();
        hlaudio::mark(was);
    }

    void fifoMarked()
    {
        const unsigned was = hlaudio::marked();
        hlaudio::mark(5);
        if (VoidFn call = fifoIrq)
            call();
        hlaudio::mark(was);
    }

    /// Whatever the libraries have put in those places since, called
    /// through the marking ones.
    void markHandlers()
    {
        const VoidFn card = irqTableAUX[kAuxWifiCard];
        if (card != nullptr && card != wifiCardMarked)
        {
            wifiCardIrq = card;
            irqTableAUX[kAuxWifiCard] = wifiCardMarked;
        }
        const VoidFn fifo = irqTable[kFifoNotEmpty];
        if (fifo != nullptr && fifo != fifoMarked)
        {
            fifoIrq = fifo;
            irqTable[kFifoNotEmpty] = fifoMarked;
        }
    }

    void powerButton()
    {
        exitLoop = true;
    }

    void vblankHandler()
    {
        const unsigned was = hlaudio::marked();
        hlaudio::mark(2);
        inputGetAndSend();
        hlaudio::mark(1);
        Wifi_Update();
        hlaudio::mark(was);
    }
} // namespace

int main()
{
    enableSound();
    // The speakers' resting level, brought up to the middle if it is not
    // there (a little at a time: at once, it pops). Started from a card, the
    // firmware has left it there; a program started by Download Play is
    // left it at nothing, and every sound is then lost.
    for (std::uint32_t bias = REG_SOUNDBIAS & 0x3FF; bias < 0x200; ++bias)
    {
        REG_SOUNDBIAS = bias + 1;
        swiDelay(0x200);
    }
    readUserSettings();
    ledBlink(LED_ALWAYS_ON);
    touchInit();
    irqInit();
    fifoInit();

    installWifiFIFO();
    // (libnds's own sound messages stay: the DSi's output rate and mix
    // ratio, and the sound capture the DSi's DSP takes its echo from, are
    // asked for that way.)
    installSoundFIFO();
    installSystemFIFO(); // sleep, the card, the firmware...
    if (isDSiMode())
        installCameraFIFO();

    // A 3DS's TwlBg asked whether it shows the top screen wide: before the
    // sound, which it would hold up.
    hlrtcom::detect();
    hlaudio::install();

    setPowerButtonCB(powerButton);
    // (Timer 3: the clock. The sound keeps time by timers 0, 1 and 2.)
    initClockIRQTimer(3);
    irqSet(IRQ_VBLANK, vblankHandler);
    irqEnable(IRQ_VBLANK);

    // The sound's blocks, between interrupts; and SELECT+START+L+R, or the
    // DSi's power button, to leave.
    unsigned blocks = 0;
    while (!exitLoop)
    {
        hlaudio::waitAndRun();
        if (isDSiMode())
            markHandlers();
        if (++blocks % 8 == 0)
        {
            const std::uint16_t mask = KEY_SELECT | KEY_START | KEY_L | KEY_R;
            if ((~REG_KEYINPUT & mask) == mask)
                exitLoop = true;
        }
    }
    return 0;
}
