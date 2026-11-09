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

namespace
{
    volatile bool exitLoop = false;

    void powerButton()
    {
        exitLoop = true;
    }

    void vblankHandler()
    {
        hlaudio::mark(2);
        inputGetAndSend();
        hlaudio::mark(1);
        Wifi_Update();
        hlaudio::mark(0);
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
        if (++blocks % 8 == 0)
        {
            const std::uint16_t mask = KEY_SELECT | KEY_START | KEY_L | KEY_R;
            if ((~REG_KEYINPUT & mask) == mask)
                exitLoop = true;
        }
    }
    return 0;
}
