#include "audio/DspAudio.hpp"

// (Sent the game, a console has none of this: DspAudio.hpp.)
#ifndef HLDS_CHILD

#include <cstdlib>
#include <malloc.h>
#include <cstring>
#include <string>

#include <nds.h>


#include "Global.hpp"
#include "HlDspRoom.hpp"
#include "audio/Arm7Audio.hpp"

extern "C"
{
// Only the numbers: the arithmetic in this header is the DSP's, not ours.
#include "dsp/hl_dsp_proto.h"
}

extern const std::uint32_t hlDspProgramBytes;
extern const std::uint8_t hlDspProgram[];

namespace hlrt
{
    DspAudio dspAudio;

    namespace
    {
        using namespace hldata;

        /// What is passed on to the DSP in one go.
        std::uint16_t scratch[512];
    } // namespace

    bool DspAudio::command(std::uint16_t cmd, std::uint16_t a1, std::uint16_t a2, std::uint16_t &answer)
    {
        // Never for ever: a DSP that has stopped answering is done without.
        auto wait = [](bool (*ready)(int), int id)
        {
            for (int spin = 0; spin < 200000; ++spin)
            {
                if (ready(id))
                    return true;
            }
            return false;
        };
        if (!wait(dspSendDataReady, 1))
            return false;
        dspSendData(1, a1);
        if (!wait(dspSendDataReady, 2))
            return false;
        dspSendData(2, a2);
        if (!wait(dspSendDataReady, 0))
            return false;
        dspSendData(0, cmd);
        if (!wait(dspReceiveDataReady, 1))
            return false;
        dspReceiveData(1); // the tick it took effect before: for the tests
        if (!wait(dspReceiveDataReady, 0))
            return false;
        answer = dspReceiveData(0);
        return true;
    }

    bool DspAudio::available()
    {
        return hlDspProgramBytes != 0 && nwramIsAvailable();
    }

    void DspAudio::stop()
    {
        if (up_)
            dspPowerOff();
        up_ = false;
        started_ = false;
        room_ = -1;
        level_ = 0;
        arm7Audio.setDsiRatio(8);
    }

    bool DspAudio::start()
    {
        if (up_)
            return true;
        if (!available())
            return false;
        if (dspExecuteDefaultTLF(hlDspProgram) != DSP_EXEC_OK)
            return false;
        std::uint16_t answer = 0;
        if (!command(HL_DSP_CMD_PING, 0, 0, answer) || answer != HL_DSP_MAGIC ||
            !command(HL_DSP_CMD_INFO, HL_DSP_INFO_SEND, 0, sendAt_) ||
            !command(HL_DSP_CMD_INFO, HL_DSP_INFO_ROOM, 0, roomAt_))
        {
            dspPowerOff();
            return false;
        }
        up_ = true;
        started_ = true;
        failed_ = false;
        // (What the ARM7 sends from here on.)
        sendRead_ = arm7Audio.sendWritten();
        sendWrite_ = 0;
        // Three quarters of the speakers to the ordinary channels -- the
        // ARM7 makes up the rest: Pipeline.arm.cpp -- and a quarter to the
        // DSP's echo, which is all it puts out. (Half and half, the game was
        // quieter by half.)
        arm7Audio.setDsiRatio(6);
        return true;
    }

    void DspAudio::write(const std::uint16_t *words, std::uint16_t ring, std::uint16_t at, int count,
                         int size)
    {
        // Round the end of the ring, in two pieces if it must be.
        const int first = count < size - at ? count : size - at;
        dspFifoWriteData(words, static_cast<std::uint16_t>(ring + at), first);
        if (count > first)
            dspFifoWriteData(words + first, ring, count - first);
    }

    void DspAudio::room(int which)
    {
        if (!up_ || which == room_)
            return;
        room_ = which;
        // Its numbers written where the DSP keeps them, then taken (which
        // empties its lines: a room changed starts quiet).
        std::uint16_t block[HL_DSP_ROOM_WORDS];
        hldata::dspRoomBlock(which, block);
        level_ = block[HL_DSP_ROOM_LEVEL];
        dspFifoWriteData(block, roomAt_, HL_DSP_ROOM_WORDS);
        std::uint16_t answer;
        up_ = command(HL_DSP_CMD_ROOM, 0, 0, answer);
    }

    HL_ARM void DspAudio::feedEcho()
    {
        const volatile std::int16_t *ring = arm7Audio.send();
        if (ring == nullptr)
            return;
        const std::uint32_t written = arm7Audio.sendWritten();
        std::uint32_t count = written - sendRead_;
        // Fallen a long way behind (a map was loading): what is that old is
        // not sent, and the echo starts again from now.
        if (count > 1024)
        {
            sendRead_ = written - 256;
            count = 256;
        }
        // With no echo wanted, nothing is sent: the DSP is left alone.
        if (level_ == 0)
        {
            sendRead_ = written;
            return;
        }
        if (count > sizeof(scratch) / sizeof(scratch[0]))
            count = sizeof(scratch) / sizeof(scratch[0]);
        if (count == 0)
            return;
        for (std::uint32_t i = 0; i < count; ++i)
            scratch[i] = static_cast<std::uint16_t>(ring[(sendRead_ + i) & (hldata::AudioLink::kSendRing - 1)]);
        sendRead_ += count;

        std::uint16_t read = 0;
        if (!command(HL_DSP_CMD_SEND_FED, sendWrite_, 0, read))
        {
            up_ = false;
            return;
        }
        int room = static_cast<int>(read) - static_cast<int>(sendWrite_) - 1;
        if (room < 0)
            room += HL_DSP_SEND_WORDS;
        if (static_cast<int>(count) > room)
            count = static_cast<std::uint32_t>(room); // the DSP is behind: the rest is dropped
        if (count == 0)
            return;
        write(scratch, sendAt_, sendWrite_, static_cast<int>(count), HL_DSP_SEND_WORDS);
        sendWrite_ = static_cast<std::uint16_t>((sendWrite_ + count) % HL_DSP_SEND_WORDS);
        std::uint16_t answer;
        up_ = command(HL_DSP_CMD_SEND_FED, sendWrite_, 0, answer);
    }

    void DspAudio::frame()
    {
        if (!up_)
            return;
        feedEcho();
        // Twice a second: how it is getting on.
        if (up_ && ++statusDue_ >= 30)
        {
            statusDue_ = 0;
            std::uint16_t status = 0;
            if (command(HL_DSP_CMD_STATUS, 0, 0, status))
                dry_ += status & 0x7FFF;
            std::uint16_t heard = 0;
            if (command(HL_DSP_CMD_HEARD, 0, 0, heard))
                heard_ = heard;
        }
    }
} // namespace hlrt

#endif
