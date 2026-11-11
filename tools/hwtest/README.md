# Hardware probes

Small ROMs that answer questions only real hardware can answer (melonDS
emulates neither sound capture faithfully, nor the DSP's DMA, nor the bus
jams). Each is its own BlocksDS project:

    cd tools/hwtest/<probe>
    BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core \
    BLOCKSDSEXT=/opt/wonderful/thirdparty/blocksds/external \
    WONDERFUL_TOOLCHAIN=/opt/wonderful make -j4

They were made for the custom ARM7 audio core (phase P0 of the audio plan).

## routing/ — the DS sound routing the room effect depends on

The ARM7 drives the channels, capture units and SOUNDCNT itself; the
automatic tests run at start, the listening ones on buttons (the screen says
which). Results on a DSi and a DS, 2026-10-28 (the same on both):

| Test | Result |
|---|---|
| 1. Capture of the left mixer with SOUNDCNT bit 12 set (Ch1 out of the mixer) | Ch1 in: mean 4096; Ch1 out: mean 0 -- **the capture is dry only** |
| 2. Capture into ARM7 WRAM | 512/512 words written, mean 4096 -- **works** |
| 3. Capture against Ch1's timer | always **8 samples ahead** (100/96 … 800/808): a fixed offset |
| 4. Pan with the speakers taking Ch1/Ch3 directly | by ear: fine (the core pans Ch1 hard left and Ch3 hard right either way) |
| 5. A tone through capture and Ch1/Ch3 against the plain mixer | by ear: the same |
| 6. Block timer (64 samples) and capture lock over minutes | DSi 232 s, DS 201 s: **0 late blocks**, the click never moved; gaps 57959..73113 ticks on a DSi, 62858..68214 on a DS (65536 ideal): at most ~0.23 ms (7 samples) of wobble |

A ring two or three 64-sample blocks deep has room to spare for that wobble.
So on a DS the room effect plays its wet signal on Ch1/Ch3, kept out of the
mixer, and takes the dry mix from the capture units: it never hears itself.

## dsp_bus/ — does feeding the DSP jam the bus? (DSi only)

The game feeds the DSP by writing words into the PDATA FIFO (the DSP's own
DMA moves them into its memory); NEA measured a jam with transfers the DSP
pulls itself through AHBM while a DMA feeds the geometry FIFO
(nitro-engine NEADisplayList.c). This probe runs the game's DSP program and
does 4 x 512 words written and read back per frame, alone, after, and during
a geometry-FIFO DMA (legacy and NDMA) of ~4000 4x4 matrix multiplies (a list
that makes no polygons: the first version drew more triangles than polygon
memory holds, and that choked the geometry engine on its own).

Results on a DSi, 2026-10-28, about 500 frames per condition:

| Condition | Fails |
|---|---|
| PDATA alone | 0 |
| DMA display list alone (DSP running) | 0 |
| NDMA display list alone | 0 |
| DMA list, then PDATA | 0 |
| NDMA list, then PDATA | 0 |
| PDATA **during** an NDMA list | 0 |
| PDATA **during** a legacy DMA list | 0 |

No stalls, no wrong data, no restarts. **The PDATA path does not jam**, even
fully overlapping. The rule that follows: the DSP is only ever given data by
the ARM9 through PDATA, never through AHBM -- and then the world's NDMA
display list can stay on while the DSP runs.

Note for writing such code: on hardware the PDATA registers must be driven
exactly as libnds does (PCFG before PADR for writes, PADR before PCFG for
reads, a short wait before each access, no waiting on the write-busy bit);
the probe's first version did not, and every transfer stalled on a DSi while
melonDS accepted it.

## divmath/ — what the divider and the square-root unit cost

Times each way of driving the units, in bus-clock ticks per call, and counts
wrong answers (the count must be 0). A goes to the next page:

1. The 64-bit divide: the old code, then STM/LDM with the IME guard, with the
   CPSR guard, and with no guard (about what an inline divide would run).
2. The root, and both units left in the wrong mode, which must restart cleanly.
3. 32-bit divides: the library's routine against the unit's 32-by-32 mode, by
   size of number.
4. A fixed wait instead of polling: the fewest idle loops that still give right
   answers, what that costs, and what one loop fewer does.

The "cold" column empties the instruction cache before each call. melonDS does
not charge for that, so only a console's cold column means anything, and it is
what decides whether 32-bit divides should move to the hardware.

Results on a DSi, 2026-11-02, in bus ticks per call, every answer right. Not
yet run on a DS.

| 64-bit divide | Ticks |
|---|---|
| Old code | 78 |
| STM/LDM, IME guard | 61 |
| STM/LDM, CPSR guard | 54 |
| ... address by MOV and ADD, not a literal | 54 |
| ... and returning by `pop {r4, pc}` (2026-11-03, warm and cold) | 54 |
| STM/LDM, no guard | 52 |
| Left in the wrong mode (CPSR guard) | 61 |
| Fixed wait of 31 loops, no polling, no guard | 45 |

| 64-bit root | Ticks |
|---|---|
| Old code | 41 |
| STM, CPSR guard | 28 |
| ... address by MOV and ADD, not a literal | 28 |
| Left in the wrong mode | 36 |

| 32-bit divide, numerator/denominator bits | Library warm | Library cold | Hardware |
|---|---|---|---|
| 12/6 | 10.1 | 170.2 | 38 |
| 20/10 | 13.6 | 208.7 | 38 |
| 30/16 | 16.6 | 244.0 | 38 |
| 30/4 | 22.5 | 313.7 | 38 |
| 20/10 with the remainder | 17.6 | 232.9 | 38 |

What follows:

- The game uses STM/LDM with the CPSR guard: about 30% off both units.
- Polling stays. A fixed wait saves 7 ticks of 52, needs a count for each kind
  of console and has no margin: 30 loops gave wrong answers on the DSi, and
  melonDS needs 35, so a count right for the console is wrong in the emulator.
- Inlining would save little: without the guard the routine, called, is only 2
  ticks quicker, and that figure already leaves out the cost of a bare call.
- 32-bit divides stay in the library. It is 2 to 4 times quicker when in the
  cache and 4 to 8 times slower when not, so the unit only wins where the
  library's code is out of the cache often (about 1 call in 8 at 20/10).

## divcontract/ — the game's 64-bit divide against the library's

The routines of `source/HwMath.s`, reached through `--wrap` as in the game,
beside the library's: quotient and remainder, word for word, on the edge cases
(by zero, by -1, the largest and smallest numbers, top bits set) and on random
numbers of every width. It also checks that r4-r11 are kept, that the CPU's
interrupt bit comes back as it went in (on or off), and the root against one
worked out bit by bit. Every root is checked by integers alone, `r*r <= v <
(r+1)*(r+1)`, never through a double (53 bits cannot hold every 64-bit value:
a double's root of 9007199326062755 is one too high). The screen ends in PASS
or FAIL, with the first differences above it.

Roots of 2^62 and above are also counted apart. 2026-11-03: on a console,
PASS, 0 of 20439 wrong from 2^62 up, so `isqrt64` gives the unit the whole
range. melonDS gets every root below 2^62 right and 6997 of those 20439 one
too high: there the probe says FAIL, and the emulator is wrong, not the game.

## dspmath/ — the ARM9's 16-bit multiplies

`shared/HlDsp.hpp`'s SMULxy, SMULWy, SMLAxy and SMLAWy, as ARM code, against
the plain arithmetic the host's tests check (`test_dsp`): over a million
answers on edge values and random numbers, "wrong" must be 0. Then, in CPU
cycles an item, warm: a Q12 value times a Q16 step >> 16 by SMULL or SMULWB,
and three Q14 products summed through 64 bits, in 32 bits, or by SMLABB.

On a console, 2026-11-03: PASS, 0 wrong, and in cycles an item:

| Loop | Cycles |
|---|---|
| scale, SMULL | 20.34 |
| scale, SMULWB | 18.03 |
| dot3, through 64 bits | 17.68 |
| dot3, in 32 bits | 16.73 |
| dot3, SMULBB and SMLABB | 16.53 |

So the game uses them: about a tenth off a time step's product and a sixteenth
off a bone's rotation.

## rtcom/ — can the game reach the 3DS's side? (3DS only)

On a 3DS, DS(i) mode is shown by TwlBg, the 3DS's own program. TWPatch makes
one from the console's own firmware; its builds keep **rtcom** on by default:
a byte channel from the DS's ARM7 to the 3DS's ARM11, through the clock's
serial port, that can upload a small ARM11 program and call it. This probe
asks what the 3DS features need (circle pad, gyroscope, widescreen):

1. Is it a 3DS (TWiLight's MCU test, DSi mode only), and does TwlBg answer
   rtcom? (`ping` must read `82`. Nothing is tried on a DSi or a DS: there the
   clock is a real chip.)
2. Does our ARM11 program (`arm11/payload.c`) upload and run, and do the
   three extra bytes in the clock's second alarm go both ways? How long does
   one exchange take, with the long and the short bit delay?
3. The top screen's scaler and window registers, read on the ARM11's side:
   once with the plain TwlBg, once with `Widescreen.cxi` (the probe's header
   is marked wide-capable, so TWiLight offers it 16:10 like the game). What
   differs tells the game which one is loaded.
4. The gyroscope on the ARM11's third I2C bus: which address (`D0`, `D2`)
   answers, its kind (ITG: WHO_AM_I at 0x00, MPU: at 0x75), its registers
   (R), its rates while the console turns (L).
5. The circle pad from the ARM7 itself, through the codec: the 3DS's stream
   (page 0xFB, register 0x01, 0x34 bytes, as Linux for the 3DS reads it; pad
   Y at 0x14, X at 0x24, 12 bits, 2048 the middle), as found (Up) and after
   setting page 0x67's sampling bits (Right; touch may stop until restart).
   If X and Y follow the pad, the game reads it with no rtcom at all.

Build it like the others (the ARM11 program is built and embedded first).
Buttons: A all of 1-4, L gyroscope live, R the bus check and the gyroscope's
registers (the infrared bridge's scratch register written and read back,
then 0xD0's registers), Up and Right the circle pad, Left every address of
I2C bus 3, Down the 3DS's own buttons live, B stop, SELECT next page.
Photograph each page.

First run on an Old 3DS, 2026-11-08 (TWPatch TwlBg, plain, not wide):

| Question | Answer |
|---|---|
| 3DS, rtcom | yes; ping `82` in 6 ms (one TwlBg frame at most) |
| Upload, echo | 1120 B in 714 ms; echo and the alarm's three bytes right both ways |
| One exchange | 0.85 ms with the long bit delay, 0.15 ms with the short |
| ARM11 reads | right (both paths agree); `0x1EC46000` gives the 3DS's buttons |
| Display, plain | window `0x28`..`0x167` (320 wide, centred in 400); scaler +4 `0x00C700FF` |
| Bus 3 | `0x9A` (infrared) and `0xD0` answer; `0xD2` does not |
| Gyroscope at `0xD0` | answers, but reads `00` at 0x00 and 0x75 (with SCL `1<<8`) |
| Codec, read register by register | nothing moved (that read the stream one byte, not 0x34) |

Still to run: the wide TwlBg, the bus check and dump with SCL `5<<8`, the
codec stream. melonDS (no 3DS) only checks the DSi path: `3DS: no`, the
MCU's 0x71 keeps `D2`, nothing sent to the clock.

## cardread/ — what a read of a map's archive costs during play

For the DS-mode map archives (docs/ds-memory-phase0.md): whether the world's
geometry can be read from the card a 4 KB page at a time while a map is
played. A DS reads its card on the ARM9 (DLDI), so the game waits for every
read; what matters is the worst read, not the mean.

The probe takes the card's biggest map archives (and c1a3, c2a5, c1a3a,
c2a2b1 when there), says how many fragments each file is in, and times
4 KB reads with FatFs's fast seek off and on: at random sector-aligned
places near the start (`near`) and in the last quarter (`far`), the same 4
bytes past a sector (`far+4`, a member on a word as archives are now),
one after the other (`seq`), 16 KB at once (`16K`), and the first read
after opening (`cold`). Least, mean, 95th percentile and most, in µs.
Everything is appended to `_nds/hl_nds/cardread.txt`; nothing else on the
card is written.

Fast seek is compiled into BlocksDS's FatFs (`FF_USE_FASTSEEK 1`) and is on
for a file only once it has a lookup table (`fatInitLookupCache`). NEA gives
each mounted archive a 2 KB one (NEANPAC.c), which holds 255 fragments.
For a file in more fragments than its table holds, `fatInitLookupCache`
returns the size it would need and leaves the half-made table on the file
(libnds 1.25.0, checked in its object code): seeks past the 255th fragment
would then follow it astray. The probe counts each archive's fragments on
a descriptor it closes at once, for that reason.

It also times the archives NEA's fragmented-archive test left in
`_nds/hl_nds/fragtest/` (nitro-engine `tests/npac_frag`, run with
`FRAGTEST_KEEP` beside `FRAGTEST_OK`): archives in 1, 200, 400 and 600
pieces, which a card written to over time may hold. Their far reads with
fast seek off are what a map archive costs when NEA falls back to no table.

NEA now looks at what `fatInitLookupCache` answers (NEANPAC.c): an archive in
more pieces than its table may hold -- 2 KB, 255 pieces, in DS mode; 4 KB,
511, in DSi mode -- is opened again with no table and seeks the slow way.
`tools/emu/fragcard.sh` runs that test in melonDS on a card image of its own.

Results: to be filled in (docs/ds-memory-phase0.md).
