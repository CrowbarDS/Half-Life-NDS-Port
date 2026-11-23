// The game's own ARM7 program: what BlocksDS's default (with the wireless
// library the Download Play build needs) does -- the touch screen and the
// buttons, the wifi, the card on a DSi, the clock -- and, instead of maxmod,
// the game's own sound (Audio.cpp), which plays what the ARM9 asks through
// one block of shared memory (shared/HlAudioLink.hpp); and, on a 3DS, the
// shape of its top screen (Rtcom.cpp).

#include <cstddef>
#include <cstdint>

#include <dswifi7.h>
#include <nds.h>

#include "Audio.hpp"
#include "Rtcom.hpp"

// libnds's tables of what each interrupt calls, by its bit (the
// dispatcher's: interruptDispatcher.s); not in its headers.
extern "C" VoidFn irqTable[];
// The stacks' ends (ds_arm7.ld): the interrupts' from __sp_svc up to
// __sp_irq_user, the program's from the end of the program's data (__end__),
// or the heap's above it, up to __sp_usr.
extern "C" char __sp_svc[], __sp_irq_user[], __sp_usr[], __end__[];
extern "C" void *sbrk(int);

namespace
{
    volatile bool exitLoop = false;

    // How deep the stacks go: each painted with a pattern at the start, and
    // the lowest word written over found now and then (cheap: from the
    // bottom, it stops at the first). The program's from where the heap ends
    // then, which only grows -- in DS mode: in DSi mode the heap is in the
    // DSi's own memory (ds_arm7_crt0.s), far below, and the program's stack
    // has the room above its data to itself. (Painted from there, it painted
    // over the ARM7's whole program: a blank screen.)
    constexpr std::uint32_t kStackPaint = 0x37A1C0DEu;

    const std::uint32_t *stackFloor()
    {
        const auto *data = reinterpret_cast<const std::uint32_t *>(__end__);
        const auto *heap = static_cast<const std::uint32_t *>(sbrk(0));
        const auto *top = reinterpret_cast<const std::uint32_t *>(__sp_usr);
        return heap > data && heap < top ? heap : data;
    }

    void paintStacks()
    {
        const int saved = enterCriticalSection();
        for (auto *w = reinterpret_cast<std::uint32_t *>(__sp_svc); w < reinterpret_cast<std::uint32_t *>(__sp_irq_user);
             ++w)
            *w = kStackPaint;
        volatile std::uint32_t here = 0;
        for (auto *w = const_cast<std::uint32_t *>(stackFloor()); w < const_cast<std::uint32_t *>(&here) - 64; ++w)
            *w = kStackPaint;
        leaveCriticalSection(saved);
    }

    std::uint32_t depth(const std::uint32_t *low, const std::uint32_t *top)
    {
        while (low < top && *low == kStackPaint)
            ++low;
        return static_cast<std::uint32_t>((top - low) * 4);
    }

    // The wireless library's thread (dswifi's Wifi_SetThreadConfig): above
    // this program's loop, which never yields to it (hlaudio::waitAndRun),
    // and below libnds's FIFO thread (64). Each of its updates is short (a
    // few packets each way), the work this loop did itself before. Its
    // stack, in DS mode, among this
    // program's data: the static frames of its deepest path sum to ~920
    // bytes (a shared-key authentication's reply). In DSi mode it is on the
    // heap, which is in the DSi's own memory there, with room for the WPA
    // handshake's (~1,040 bytes, and the key's derivation besides).
    constexpr int kWifiPriority = 10;
    constexpr std::size_t kWifiStackDs = 1536;
    constexpr std::size_t kWifiStackDsi = 3072;
    alignas(8) std::uint32_t wifiStack[kWifiStackDs / 4];

    void measureStacks()
    {
        hlaudio::stacks(depth(stackFloor(), reinterpret_cast<const std::uint32_t *>(__sp_usr)),
                        depth(reinterpret_cast<const std::uint32_t *>(__sp_svc),
                              reinterpret_cast<const std::uint32_t *>(__sp_irq_user)),
                        isDSiMode() ? 0 : depth(wifiStack, wifiStack + kWifiStackDs / 4));
    }

    // (Diagnostic) what the ARM7 is in, for the ARM9 to say if it stops
    // (HlAudioLink.hpp's where): each interrupt marked as it runs, and what
    // it broke into marked again as it ends. The FIFO's is libnds's: called
    // through ours.
    constexpr int kFifoNotEmpty = 18;
    volatile VoidFn fifoIrq = nullptr;

    void fifoMarked()
    {
        const unsigned was = hlaudio::marked();
        hlaudio::mark(5);
        if (VoidFn call = fifoIrq)
            call();
        hlaudio::mark(was);
    }

    /// Whatever libnds has put in that place since, called through the
    /// marking one.
    void markHandlers()
    {
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

    // The buttons and the touch screen: due at each VBlank, done by the loop
    // below -- not in the interrupt. libnds's
    // FIFO messages are answered in a thread of their own, with interrupts
    // on: the codec, the power chip and the firmware written or read there,
    // over the SPI bus the touch screen is read on, and the thread's stack
    // (1 KB) is where an interrupt that cuts it off runs.
    volatile bool vblankDue = false;

    void vblankHandler()
    {
        vblankDue = true;
    }

    void vblankWork()
    {
        if (!vblankDue)
            return;
        vblankDue = false;
        const unsigned was = hlaudio::marked();
        hlaudio::mark(2);
        // (Interrupts held off: nothing breaks in -- the FIFO's thread, which
        // could, only runs when an interrupt hands it the processor.)
        const int ime = enterCriticalSection();
        inputGetAndSend();
        leaveCriticalSection(ime);
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
    paintStacks();
    readUserSettings();
    ledBlink(LED_ALWAYS_ON);
    touchInit();
    irqInit();
    fifoInit();

    // The wireless in a thread of its own, never from an interrupt: on a
    // DSi (online play) from a timer every millisecond, its card's
    // interrupt and the FIFO's, a game sending many packets kept one
    // of them always due again, and the sound, the buttons and the touch
    // screen stopped for good. Its thread polls the card instead, woken
    // after each sound block (Wifi_Poll, below).
    if (isDSiMode())
        Wifi_SetThreadConfig(kWifiPriority, nullptr, kWifiStackDsi);
    else
    {
        for (std::uint32_t &w : wifiStack)
            w = kStackPaint;
        Wifi_SetThreadConfig(kWifiPriority, wifiStack, sizeof(wifiStack));
    }
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
    // (Timer 3: the clock. The sound keeps time by timers 0 and 1; timer 2
    // is free -- the DSi wireless's timer is no more.)
    initClockIRQTimer(3);
    irqSet(IRQ_VBLANK, vblankHandler);
    irqEnable(IRQ_VBLANK);

    // The sound's blocks, between interrupts; and SELECT+START+L+R, or the
    // DSi's power button, to leave.
    unsigned blocks = 0;
    while (!exitLoop)
    {
        hlaudio::waitAndRun();
        vblankWork();
        if (isDSiMode())
            markHandlers();
        // (On a DSi, a few packets each way, every block: the wireless's
        // thread, woken, cuts in for them.)
        Wifi_Poll();
        if (++blocks % 8 == 0)
        {
            const std::uint16_t mask = KEY_SELECT | KEY_START | KEY_L | KEY_R;
            if ((~REG_KEYINPUT & mask) == mask)
                exitLoop = true;
        }
        if (blocks % 256 == 0)
            measureStacks();
    }
    return 0;
}
