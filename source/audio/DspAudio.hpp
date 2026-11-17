#pragma once

// The DSi's DSP, as the game uses it: the echo of the room, the room
// effects' Enhanced (the program is teak/, assembly; its model
// shared/dsp/hl_dsp.h).
//
// Nothing here exists on a DS, or on a DSi running the game without DSi
// rights: start() says no, and everything else does nothing. Sound then plays
// as it does on a DS.
//
// The DSP cannot hear the sixteen ordinary channels. What echoes is a copy
// of their mix the ARM7's sound program sends (its pipeline captures it:
// Arm7Audio::send); each frame the new part of it is passed on to the DSP --
// through the port, never its own bus (tools/hwtest/dsp_bus) -- which puts
// out the echo alone. (The music is the ARM7's, not the DSP's.)

#include <cstdint>
#include <cstdio>

#include "HlSound.hpp"

namespace hlrt
{
#ifdef HLDS_CHILD
    // Sent the game, a console stays in DS mode, which has no DSP (and has not
    // the speakers' mix it would need: dsiSpeakers): every call does nothing,
    // and none of the DSP's code is in its program.
    class DspAudio
    {
    public:
        static bool available() { return false; }
        bool start() { return false; }
        void stop() {}
        bool up() const { return false; }
        bool failed() const { return false; }
        void room(int) {}
        void frame() {}
        std::uint32_t dry() const { return 0; }
        int heard() const { return 0; }
    };
    inline DspAudio dspAudio;
#else
    class DspAudio
    {
    public:
        /// Whether there is a DSP to be had at all: a DSi, run with DSi
        /// rights, and a ROM built with the DSP program in it.
        static bool available();
        /// Load the program and see that it answers. After the sound hardware
        /// is on.
        bool start();
        /// Silence it and switch it off: the speakers go back to the
        /// ordinary channels alone.
        void stop();
        bool up() const { return up_; }
        /// It has gone wrong too often (or stopped answering) to be used
        /// again this session: the caller goes back to the DS's way.
        bool failed() const { return failed_ || (started_ && !up_); }

        /// The room from now on (the game's 0..28: HlDspRoom.hpp says how
        /// the DSP has it).
        void room(int room);

        /// Every pass of the main loop: feeds the echo.
        void frame();

        /// For the overlay and the log: times a ring ran dry, in all.
        std::uint32_t dry() const { return dry_; }
        /// ... and the loudest sample the DSP was lately sent to echo (0 to
        /// 32767): that the sound is getting to it at all.
        int heard() const { return heard_; }

    private:
        bool command(std::uint16_t cmd, std::uint16_t a1, std::uint16_t a2, std::uint16_t &answer);
        void write(const std::uint16_t *words, std::uint16_t ring, std::uint16_t at, int count, int size);
        void feedEcho();

        bool up_ = false;
        bool failed_ = false;
        bool started_ = false;
        std::uint16_t sendAt_ = 0, roomAt_ = 0;
        std::uint32_t dry_ = 0;
        int heard_ = 0;
        int statusDue_ = 0;

        // The echo's send.
        std::uint32_t sendRead_ = 0;  ///< samples of the ARM7's send passed on, all told
        std::uint16_t sendWrite_ = 0; ///< where in the DSP's ring the next goes
        int room_ = -1;   ///< the room it was last given (-1: none yet)
        int level_ = 0;   ///< ... and how loud (0: nothing to echo)
    };

    extern DspAudio dspAudio;
#endif
} // namespace hlrt
