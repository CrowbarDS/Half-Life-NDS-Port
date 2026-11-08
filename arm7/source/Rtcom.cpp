#include "Rtcom.hpp"

#include <cstdint>

#include <nds.h>

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
        // no", close. From it: yes, no (or done).
        constexpr std::uint8_t kNext = 0x81, kSayNo = 0x83, kClose = 0xFE;
        constexpr std::uint8_t kYes = 0x80, kNo = 0x82;
        constexpr std::uint8_t kUpload = 0x41, kMakeRunnable = 0x42, kRun = 0x44;

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
            if (!ask(kRun, 0))
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
                if (upload() && peek(kWindowLeft, left) && peek(kWindowRight, right) && right > left &&
                    right - left + 1 > 320)
                    told |= hldata::kRtcomWide;
                // Closed as the known clients close it.
                write(kWriteStatus, kClose);
                wait();
                write(kWriteStatus, kNo);
            }
            REG_IF = IRQ_RTC;
            REG_RCNT = savedRcnt;
        }
        fifoSendValue32(FIFO_USER_02, told);
    }
} // namespace hlrtcom
