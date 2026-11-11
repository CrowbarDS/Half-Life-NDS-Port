#include "Rtcom.hpp"

#include <cstdint>

#include <nds.h>

#include "Hl3dsLink.hpp"
#include "HlRtcom.hpp"

// The program uploaded (Peek11.s).
extern "C" const std::uint8_t hlPeek11Start[], hlPeek11End[];

namespace hlrtcom
{
    namespace
    {
        // REG_RTCCNT's lines: data, clock, select, and the bits that make
        // each an output.
        constexpr std::uint8_t kData = 0x01, kClock = 0x02, kSelect = 0x04;
        constexpr std::uint8_t kDataOut = 0x10, kClockOut = 0x20, kSelectOut = 0x40;

        // The clock's registers TwlBg reads (0x112: a byte, 0x113: the
        // status), and the commands that reach them, sent high bit first.
        constexpr std::uint8_t kWriteData = 0x6C, kReadData = 0x6D, kWriteStatus = 0x6E, kReadStatus = 0x6F;
        // Statuses. To the 3DS: a command below 0x80, the next byte, "say
        // no". From it: yes, no (or done).
        constexpr std::uint8_t kNext = 0x81, kSayNo = 0x83;
        constexpr std::uint8_t kYes = 0x80, kNo = 0x82;
        constexpr std::uint8_t kUpload = 0x41, kMakeRunnable = 0x42, kRun = 0x44;
        // Peek11.s's commands (call 0's byte).
        constexpr std::uint8_t kPeek = 0x00, kPad = 0x50;
        // The clock's second alarm: three bytes, read whole (Peek11.s's
        // second channel back).
        constexpr std::uint8_t kReadAlarm2 = 0x6B;

        // The top screen's window, left and right edge, on the ARM11's side.
        constexpr std::uint32_t kWindowLeft = 0x1ED02218, kWindowRight = 0x1ED0221C;

        // The bit delay. Uploading is only right with a long one (measured
        // on an Old 3DS: 0.85 ms an exchange; 0.15 with the short).
        int bitDelay = 14;

        void pause(int n)
        {
            for (volatile int i = n; i > 0; --i)
            {
            }
        }

        void clockOut(std::uint8_t value)
        {
            for (int bit = 7; bit >= 0; --bit)
            {
                const std::uint8_t d = (value >> bit) & 1;
                REG_RTCCNT8 = kSelectOut | kSelect | kClockOut | kDataOut | d;
                pause(bitDelay);
                REG_RTCCNT8 = kSelectOut | kSelect | kClockOut | kClock | kDataOut | d;
                pause(bitDelay);
            }
        }

        std::uint8_t clockIn()
        {
            std::uint8_t value = 0;
            for (int bit = 0; bit < 8; ++bit)
            {
                REG_RTCCNT8 = kSelectOut | kSelect | kClockOut;
                pause(bitDelay);
                REG_RTCCNT8 = kSelectOut | kSelect | kClockOut | kClock;
                pause(bitDelay);
                value = static_cast<std::uint8_t>(value << 1 | (REG_RTCCNT8 & kData));
            }
            return value;
        }

        void transfer(std::uint8_t command, int value)
        {
            REG_RTCCNT8 = kSelectOut | kClockOut | kClock | kDataOut | kData;
            pause(2);
            REG_RTCCNT8 = kSelectOut | kSelect | kClockOut | kClock | kDataOut | kData;
            pause(2);
            clockOut(command);
            if (value >= 0)
                clockOut(static_cast<std::uint8_t>(value));
        }

        void end()
        {
            pause(2);
            REG_RTCCNT8 = kSelectOut | kClockOut | kClock;
            pause(2);
        }

        void write(std::uint8_t command, std::uint8_t value)
        {
            transfer(command, value);
            end();
        }

        std::uint8_t read(std::uint8_t command)
        {
            transfer(command, -1);
            const std::uint8_t value = clockIn();
            end();
            return value;
        }

        /// The 3DS's answer: its status, or -1 after about a tenth of a
        /// second (a TwlBg without rtcom never answers).
        int wait()
        {
            for (int n = 0; n < 1000000; ++n)
            {
                if (REG_IF & IRQ_RTC)
                {
                    REG_IF = IRQ_RTC;
                    return read(kReadStatus);
                }
            }
            REG_IF = IRQ_RTC;
            return -1;
        }

        bool ask(std::uint8_t status, std::uint8_t data)
        {
            write(kWriteData, data);
            write(kWriteStatus, status);
            return wait() == kYes;
        }

        bool upload()
        {
            const std::uint32_t size = static_cast<std::uint32_t>(hlPeek11End - hlPeek11Start);
            if (!ask(kUpload, size & 0xFF) || !ask(kNext, (size >> 8) & 0xFF) || !ask(kNext, (size >> 16) & 0xFF) ||
                !ask(kNext, size >> 24))
                return false;
            for (std::uint32_t n = 0; n < size; ++n)
            {
                if (!ask(kNext, hlPeek11Start[n]))
                    return false;
            }
            // The upload ends on a "no"; then the program is made runnable.
            write(kWriteStatus, kSayNo);
            if (wait() != kNo)
                return false;
            write(kWriteStatus, kMakeRunnable);
            return wait() == kYes;
        }

        /// The word at `address` on the ARM11's side.
        bool peek(std::uint32_t address, std::uint32_t &value)
        {
            if (!ask(kRun, kPeek))
                return false;
            value = 0;
            for (int k = 0; k < 4; ++k)
            {
                if (!ask(kNext, (address >> (8 * k)) & 0xFF))
                    return false;
            }
            value = read(kReadData);
            for (int k = 1; k < 4; ++k)
            {
                if (!ask(kNext, 0))
                    return false;
                value |= static_cast<std::uint32_t>(read(kReadData)) << (8 * k);
            }
            return true;
        }

        /// The second alarm's three bytes.
        void readAlarm2(std::uint8_t bytes[3])
        {
            transfer(kReadAlarm2, -1);
            for (int k = 0; k < 3; ++k)
                bytes[k] = clockIn();
            end();
        }

        // --- The circle pad, each frame ------------------------------------
        //
        // Never a wait: each call of step() looks whether the 3DS has
        // answered (the clock's interrupt flag; the interrupt itself stays
        // off) and goes on from there. A request is sent once a frame.

        enum class Pad : std::uint8_t
        {
            Off,      // no session, or given up for good
            Idle,     // waiting for the next frame
            WaitRun,  // asked to run the pad command
            WaitNext, // asked for its answer
        };

        Pad pad = Pad::Off;
        hldata::Link3ds *link = nullptr;
        std::uint16_t lastLine = 0; // REG_VCOUNT at the last step
        bool frameDue = false;
        int waited = 0;   // steps since the request was sent
        int failures = 0; // in a row
        int restAt = 0;   // steps left before trying again after a run of failures

        // A step comes every sound block (2 ms): about 50 ms.
        constexpr int kWaitSteps = 25;
        constexpr int kFailuresToRest = 8;
        constexpr int kRestSteps = 2000; // about 4 s

        /// One request: the data byte, then the status, interrupts held off
        /// so that nothing else uses the clock's lines meanwhile (libnds
        /// reads the time after a sleep).
        void request(std::uint8_t status, std::uint8_t data)
        {
            const int ime = enterCriticalSection();
            REG_IF = IRQ_RTC;
            write(kWriteData, data);
            write(kWriteStatus, status);
            leaveCriticalSection(ime);
            waited = 0;
        }

        /// The 3DS's status if it has answered, else -1.
        int answer()
        {
            if (!(REG_IF & IRQ_RTC))
                return -1;
            const int ime = enterCriticalSection();
            REG_IF = IRQ_RTC;
            const int status = read(kReadStatus);
            leaveCriticalSection(ime);
            return status;
        }

        void failed()
        {
            if (++failures >= kFailuresToRest)
            {
                link->present = 0;
                link->padX = link->padY = 0;
                failures = 0;
                restAt = kRestSteps;
            }
            pad = Pad::Idle;
        }

        /// A 3DS: its MCU does not keep what is written to 0x71 (a DSi's
        /// does). What was there is put back.
        bool is3ds()
        {
            const std::uint8_t before = i2cReadRegister(I2C_PM, 0x71);
            i2cWriteRegister(I2C_PM, 0x71, 0xD2);
            const std::uint8_t after = i2cReadRegister(I2C_PM, 0x71);
            i2cWriteRegister(I2C_PM, 0x71, before);
            return after != 0xD2;
        }
    } // namespace

    void detect()
    {
        std::uint32_t told = 0;
        if (isDSiMode() && is3ds())
        {
            const std::uint16_t savedRcnt = REG_RCNT;
            REG_IF = IRQ_RTC;
            REG_RCNT = 0x8100; // the 3DS's answers raise the clock's interrupt
            REG_IF = IRQ_RTC;
            // A command TwlBg does not know: rtcom says no.
            write(kWriteStatus, 0x01);
            if (wait() == kNo)
            {
                told = hldata::kRtcomAnswered;
                bitDelay = 14;
                std::uint32_t left = 0, right = 0;
                const bool loaded = upload();
                if (loaded && peek(kWindowLeft, left) && peek(kWindowRight, right) && right > left &&
                    right - left + 1 > 320)
                    told |= hldata::kRtcomWide;
                // Left open, the short bit delay from now on: the circle
                // pad is read each frame (step()).
                if (loaded)
                {
                    bitDelay = 1;
                    pad = Pad::Idle;
                }
            }
            REG_IF = IRQ_RTC;
            if (pad == Pad::Off)
                REG_RCNT = savedRcnt;
        }
        fifoSendValue32(FIFO_USER_02, told);
    }

    void step()
    {
        if (pad == Pad::Off)
            return;
        if (link == nullptr)
        {
            if (!fifoCheckAddress(FIFO_USER_02))
                return;
            link = static_cast<hldata::Link3ds *>(fifoGetAddress(FIFO_USER_02));
        }
        // A new frame: the line count has come round.
        const std::uint16_t line = REG_VCOUNT;
        if (line < lastLine)
            frameDue = true;
        lastLine = line;

        switch (pad)
        {
        case Pad::Idle:
            if (restAt > 0)
            {
                --restAt;
                return;
            }
            if (!frameDue)
                return;
            frameDue = false;
            request(kRun, kPad);
            pad = Pad::WaitRun;
            return;
        case Pad::WaitRun:
        {
            const int status = answer();
            if (status < 0)
            {
                if (++waited > kWaitSteps)
                    failed();
                return;
            }
            if (status != kYes)
            {
                failed();
                return;
            }
            request(kNext, 0);
            pad = Pad::WaitNext;
            return;
        }
        case Pad::WaitNext:
        {
            const int status = answer();
            if (status < 0)
            {
                if (++waited > kWaitSteps)
                    failed();
                return;
            }
            if (status != kYes)
            {
                failed();
                return;
            }
            std::uint8_t low = 0, rest[3] = {0, 0, 0};
            {
                const int ime = enterCriticalSection();
                low = read(kReadData);
                readAlarm2(rest);
                leaveCriticalSection(ime);
            }
            failures = 0;
            pad = Pad::Idle;
            if (low == 0 && rest[0] == 0xFF && rest[1] == 0xFF && rest[2] == 0xFF)
            {
                // TwlBg's pad was not found: nothing to read, ever.
                link->present = 0;
                pad = Pad::Off;
                return;
            }
            link->padX = static_cast<std::int16_t>(low | rest[0] << 8);
            link->padY = static_cast<std::int16_t>(rest[1] | rest[2] << 8);
            link->count = link->count + 1;
            link->present = 1;
            return;
        }
        case Pad::Off:
            return;
        }
    }
} // namespace hlrtcom
