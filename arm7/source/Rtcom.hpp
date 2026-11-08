#pragma once

// A 3DS's own side, asked once at start: is the top screen shown wide?
//
// On a 3DS a DS(i) program's screens are shown by TwlBg, a 3DS program. The
// one TWPatch makes from the console's own firmware keeps "rtcom": a byte
// channel from this processor to the 3DS's ARM11, through the clock's
// serial port, that can upload a small program and call it. Through it a
// few bytes of the top screen's window are read: 320 pixels across on the
// plain TwlBg, 384 on the wide one (tools/hwtest/rtcom, the probe that
// found how).
//
// Only in DSi mode on a 3DS (the MCU test TWiLight Menu++ uses): on a DSi
// or a DS the clock is a real chip, and nothing is sent to it.

namespace hlrtcom
{
    /// Asks, and tells the ARM9 (shared/HlRtcom.hpp). Before the sound starts: it takes up to a
    /// tenth of a second, longer than a sound block may wait.
    void detect();
} // namespace hlrtcom
