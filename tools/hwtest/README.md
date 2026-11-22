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

## itcmswap/ — what bringing code into ITCM would cost

The two numbers `tools/icache_rank.py --by-phase` models an ITCM region
swapped by phase with: what copying code into ITCM costs, and what one miss
of the instruction cache costs. On the console, not the emulator, whose
timings are not the console's (melonDS does not even model the instruction
cache: its "one miss" is 0). In CPU cycles: a byte copied, or a line of the
cache.

- **memcpy warm**, **ldm/stm warm / cold / uncached** (4, 8, 16 KB): main
  RAM into ITCM, with libc's copy and with an LDM/STM of eight registers;
  the source in the data cache, out of it (cleaned and emptied first), and
  read through the uncached mirror.
- **chain cold / warm, one miss**: a chain of branches, one to each 32-byte
  line of main RAM, run with the instruction cache emptied and again warm;
  the difference per line is what a miss costs, to pass as `--miss-cycles`.

| Where | copy, cycles a byte | chain cold / warm, a line | one miss |
|---|---|---|---|
| DSi, 134 MHz (2026-11-21, first probe) | 3.18-3.30 (2.85 once), warm, cold or uncached alike | 93.1 / 54.6 | 38.5: too low, see below |
| DSi, 134 MHz (2026-11-21, a chain of 64) | 3.1-3.3 alike | 95.4 / 6.5 | **88.8** (0.66 us) |

A copy into ITCM runs at the speed of main RAM, cached or not: 8 KB is
about 0.2 ms on a DSi. The first probe's chain ran 192 lines through a cache
of 64 sets of 4: lines 64 apart fell in the same way (the replacement counter
is the whole cache's, it seems) and pushed each other out, so its warm run
still missed -- 54.6 cycles a line, where a cached branch takes a few. The
chain is now 64 lines, one to each set: warm, 6.5 cycles a line, and a miss
88.8 cycles, 0.66 us. A DS is not measured yet: its main RAM is the same, so
the same time is likely, half as many of its cycles.

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

## fxprobe/ — the tricks the effects plan leans on

Each test captures the picture into bank D and reads it back, so the answers
are numbers on the bottom screen. After them, A shows each scene live and B
runs the tests again.

1. **Lines**: a triangle with two equal vertices: its width, alpha 15, fog
   (with a fogged quad beside it as a control), cut at the right edge of the
   screen, and one running through the near plane.
2. **A blended sprite over the 3D layer** with `BLDALPHA` 16/16 (the head-up
   display's setting): is it added, clamped at 31; does the back one of two
   sprites over each other show; is the backdrop added to as well?
3. **A DMA started by each horizontal blank writing `BG0HOFS`** from a table
   (`y % 16` left, then right): lines moved by their own amount, a line late,
   or other; and, moved right, whether the edge wraps or shows the backdrop.
   Then on DMA 1 (`dma1`), and on DMA 1 while the geometry FIFO is kept fed
   (`fed`) as the game feeds it: DMA 0 in its FIFO mode on a DS (NEA's
   display lists), NDMA 0 on a DSi (the world's). The underwater wobble
   (`source/render/WaterWobble`) is on DMA 1, and on a DSi only until the
   `fed` line on a DS says it can be on one. Two ways round it after: the
   scroll on DMA 0 (`fd0`; a DS fed by DMA 1, as NEA would have to be), and
   on a DSi the feed in blocks of 16 words, not 112 (`f16`). The number at
   the end of each fed line is how many times 2048 words were fed while the
   picture was caught (what short blocks cost). The title scrolls off the
   top of the screen to make room.

In melonDS (DS and DSi modes alike; the emulator is not the reference):

| Test | melonDS |
|---|---|
| Line width, slanted run | 1 pixel; 80 pixels for an 81-long run |
| Line at alpha 15 | red 15: blended |
| Fogged line, fogged quad | both fogged (an earlier run without the quad read the line unfogged) |
| Line over the edge, through the near plane | 56 of 56 pixels; drawn to the bottom row |
| Sprite 12 over 3D 10 / 25 | 22 / 31: added, clamped |
| Two sprites over each other | 22: the front one only |
| Sprite over the backdrop (6) | 18 |
| Per-line scroll | 190 lines right, 0 late (the 2 others: the bar's last rows) |
| ... on DMA 1 | the same |
| ... on DMA 1, the FIFO fed | DS: 19 right, 6 late, 167 other (DMA 0 holds it back); DSi: 190 right |
| ... on DMA 0, fed by DMA 1 (DS) or NDMA 0 (DSi) | 190 right on both; 8 to 10 times 2048 words fed |
| ... on DMA 1, a DSi fed 16 words at a time | 190 right; 8 times |
| Scrolled right | no wrap: the backdrop shows |

On the owner's DSi (2026-11-19): every line and sprite figure as in melonDS
(the lines' width, alpha, fog, edge and near plane; the sprite added and
clamped, the front one winning, the backdrop added to). The per-line scroll:
190 right on DMA 0 and on DMA 1 alone; **fed: 11 right, 13 late, 168 other**
-- unlike melonDS, the DSi's NDMA 0 feeding the FIFO holds DMA 1 back too.
Two more runs with the variants: fed 16 and 19 right; `fd0` (the scroll on
DMA 0) 13 and 14 -- no better; **`f16` (NDMA 0 in blocks of 16) 190 right**,
8 times 2048 words fed as in every run. The game and NEA feed the DSi's
FIFO 16 words at a time since. On a DS: not yet run.

## markpull/ — wall marks pulled toward the eye

A wall mark is drawn today with the "same depth" test (polygon bit 14), which
eats marks on a wall seen at a low angle (the mark probe's findings, measured
on a DSi: one pixel spans tens of units of depth there). This probe draws the
same marks pulled toward the eye along each corner's line of sight -- the
same pixels on the screen, only nearer -- with the ordinary depth test, and
measures what it costs: a mark drawn over something standing in front of its
wall by less than the pull.

- **P1 coverage**: five marks along a wall 1024 long, the eye 256 from its
  middle, at 90, 45, 20, 10 and 5 degrees: by the "same depth" test (`eq`),
  pulled 1/256, 1/128 or 1/64 of their distance, or by as much as the depth
  changes across two pixels there and 1/128 more (`2px`). The worst mark,
  the average, then each mark at 20 and 10 degrees with its size in pixels.
- **P2 bleed**: a surface 1 to 16 units in front of the wall over half a
  mark, at 45 and 20 degrees: how much of it the mark is drawn on (0 is
  right).

Results also go to `markpull.txt` at the root of the card. In melonDS (DSi
mode; it matched the DSi's W-buffer depth almost exactly in the mark probe):

| Coverage (worst / average) | eq | 1/256 | 1/128 | 1/64 | 2px |
|---|---|---|---|---|---|
| 90 degrees | 100 / 100 | 93 / 99 | 100 | 100 | 100 |
| 45 | 100 | 100 | 100 | 100 | 100 |
| 20 | 0 / 96 | 0 / 92 | 0 / 87 | 0 / 94 | 95 / 95 |
| 10 | 33 / 93 | 50 / 92 | 50 / 93 | 83 / 98 | 50 / 99 |
| 5 | 0 / 90 | 86 / 89 | 89 / 90 | 94 / 95 | 97 / 98 |

At 20 degrees the far marks (110, 48 and 26 pixels) are 60, 0, 0 by `eq`
and 0 pulled by a fixed part: one pixel there spans about 13 units of depth.
`2px`, whose pull grows with that, keeps them whole.

| Bleed onto a surface d in front | eq | 1/256 | 1/128 | 1/64 | 2px |
|---|---|---|---|---|---|
| 45 degrees, d 1 / 2 / 4 / 8 | 100 / 100 / 100 / 0 | 91 / 0 / 0 / 0 | 100 / 0 / 0 / 0 | 100 / 100 / 0 / 0 | 100 / 100 / 100 / 0 |
| 20 degrees, d 1 / 2 / 4 / 8 | 100 / 100 / 0 / 0 | 32 / 0 / 0 / 0 | 65 / 0 / 0 / 0 | 100 / 2 / 0 / 0 | 100 / 100 / 99 / 0 |

None bleed at 16. On the owner's DSi (DSi mode, 2026-11-19): every figure as
in melonDS within a point (worst at 5 degrees, 1/256: 87; 1/128: 90; 1/64:
95). On a DS: not yet run.

## ovlprobe/ — overlays: code that shares a region, read when it is wanted

The overlays of `tools/ovl.py` and `source/Overlay.*`, on their own: two
overlays in one region (`overlays.json`), `one` in Thumb and `two` in ARM,
each with code, constants, data and zeroed data. The linker script is the
installed BlocksDS one with the region put in (`ovl.py ld`), the link is
checked (`ovl.py check`), the overlays packed into NitroFS (`ovl.py pack`),
and the ROM checked to hold none of them in its ARM9 image (`ovl.py
romcheck`). `make`, then run `hl_ovlprobe.nds`; `./negative.sh` builds what
must be refused. `make OVL_MODE=rom` builds `hl_ovlprobe_rom.nds` instead,
with the overlays in the ROM's own ARM9 overlay table and its files, as
retail games have them (`ovl.py rom`, ndstool `-y9` and `-y`; the sums
stamped into the program by `ovl.py stamp`), and a file of NitroFS's own.

On the top screen, each a PASS or a FAIL:

- **pack**: the pack is found (`nitro:/ovl/arm9.ovl`) and is the program's.
- **table** (`OVL_MODE=rom`, in place of pack): each overlay's file is found
  by its id in the ROM's FAT and is of its size; how many overlays the
  header's table holds.
- **path** (`OVL_MODE=rom`): a file of NitroFS's own, read by its name: its
  id, after the overlays', still found.
- **one, two**: each answers as itself once loaded.
- **swaps**: 1000 loads, one then the other, every answer right. A stale
  instruction cache answers as the overlay before.
- **zeroed**: each load's zeroed data starts at 0, and its data as built.
- **heap**: no heap taken by a load.
- **irq**: a timer's interrupt went on through the loads.
- **twl** (DSi): DSi-only code calls into an overlay through its gate.
- **loads**: how fast a load reads (1000 of the larger overlay's size).

Then A calls `two` through its gate with `one` loaded: it must stop on
`HALT: A gate found its overlay not loaded: two`.

| Where | pack | one, two | swaps | zeroed | heap | irq | twl | loads | A |
|---|---|---|---|---|---|---|---|---|---|
| melonDS, DS mode (2026-11-19) | PASS | PASS | 0 wrong | PASS | 0 bytes | 352 ticks | — | 6.0 MB/s | halts, names `two` |
| melonDS, DSi mode | PASS | PASS | 0 wrong | PASS | 0 bytes | 321 ticks | PASS | 6.6 MB/s | halts, names `two` |
| melonDS, DS mode, BlocksDS v1.25.0's script | PASS | PASS | 0 wrong | PASS | 0 bytes | 351 ticks | — | 6.1 MB/s | |
| DSi, TWiLight Menu++, from SD (2026-11-19) | PASS | PASS | 0 wrong | PASS | 0 bytes | 921 ticks | PASS | 2.3 MB/s | halts, names `two` |

`OVL_MODE=rom` (the ROM's own overlay table), where **table** and **path**
stand in for pack:

| Where | table | path | one, two | swaps | zeroed | heap | irq | twl | loads |
|---|---|---|---|---|---|---|---|---|---|
| melonDS, DS mode (2026-11-20) | PASS, 2 in the header | PASS | PASS | 0 wrong | PASS | 0 bytes | 353 ticks | — | 6.0 MB/s |
| melonDS, DSi mode | PASS, 2 in the header | PASS | PASS | 0 wrong | PASS | 0 bytes | 322 ticks | PASS | 6.6 MB/s |
| DSi, TWiLight Menu++, from SD (2026-11-21) | PASS, 2 in the header | PASS | PASS | 0 wrong | PASS | 0 bytes | 888 ticks | PASS | 2.4 MB/s |

On the DSi, A then halted naming `two`, by `twoCompute` (`one` there), as it
must.

`negative.sh` (2026-11-19): a call into an overlay outside a gate (R1), a
static constructor in an overlay (R0), and an overlay larger than its
region's sizer (the link's assertion) are each refused; and (2026-11-20) a
program that reads the ROM's overlay table, in a ROM made without one
(`romcheck`). No DS with a
flashcart was at hand: the DS's card read is checked in melonDS only.

On the console the loads read at 2.3 MB/s (928 ms for 1000 loads of 2.1 KB):
a 32 KB overlay is about 14 ms, a phase change's work, not a frame's. The 3DS in TWL mode: not yet run.

## lamppatch/ — the lamp's light as a patch of the wall's own texture

The game ADDS its flashlight's light to a wall's light (R_AddDynamicLights);
the DS cannot add one picture to another. The plan: draw the lit part of the
wall again in its own texture, its corners as bright as the game makes them
(shared/HlLampPatch.hpp, the game's own code, which this probe runs). The
wall is two triangles, 240 square (the largest face the map compiler leaves),
its corners four colours apart; the light 8 units off its middle. The patch
is drawn three ways: solid by the "same depth" test (eq), the same at alpha
30 (eq30), and solid, pulled two pixels' depth toward the eye (2px, as the
marks are).

- P1 coverage: how much of the patch shows over its wall.
- P2 unseen: a patch that adds no light: how many of its pixels differ from
  the wall alone, and by most how much (its edge would show as a seam).
- P3 by eye: a brick wall with the lamp on it at 90, 45, 20 and 10 degrees.

On the owner's console (2026-11-22; melonDS gives the same coverage):

| g | eq | eq30 | 2px |
|---|---|---|---|
| 90 | 0 | 100 | 100 |
| 60 | 0 | 100 | 99 |
| 45 | 0 | 100 | 100 |
| 30 | 0 | 99 | 99 |
| 20 | 0 | 96 | 99 |
| 10 | 0 | 69 | 99 |
| 5 | 0 | 62 | 100 |

Unseen, on the console: eq30 7, 8, 3 and 4% of the patch's pixels one level
off the wall at 90, 45, 20 and 10 degrees; 2px 16, 18, 33 and 32%; none by
more than one. (melonDS: 6..31% for both.) By eye: the bricks stay as they
are, brighter in the pool, by both ways.

A solid patch by the same-depth test draws nothing: the hardware draws solid
polygons in its own order, by where they are on the screen, so the patch can
come before its wall. At alpha 30 it is see-through, drawn after every solid
polygon, and the test holds as the marks' does: whole down to 20 degrees.
So the game draws a patch at alpha 30 by the same-depth test where the wall
is met at 30 degrees or more, and solid and pulled below that.
Results go to lamppatch.txt at the root of the card.

## slot3/ — a texture in Bank D while Bank D is lent out

The plan is to keep every texture that is not 4x4 compressed in Bank D
(texture slot 3) during play. A map change captures the last picture into
Bank D, and display capture can write nowhere but Banks A to D. So the frame
being captured is drawn while D is the capture's and not a texture slot.
What do its polygons textured from D show then?

Four quads: A3I5 (colour 2, red, alpha 7), PAL4 (colour 1, green), PAL256
(colour 5, yellow), all 8x8 in Bank D, and an untextured white one. Colour 0
of the palette is blue; the clear colour magenta. Each case is drawn 30
frames running and each quad's inner 2880 pixels counted: its own colour,
clear, colour 0, other, and in how many frames the count changed.

1. D slot 3: what is right.
2. D in LCD mode while the frame is drawn, captured into Bank B.
3. D in LCD mode, captured into D itself: the game's freeze.
4. D mapped as a main-screen background.
5. D switched off.

In melonDS (DS mode), case 1 is right. In cases 2 to 5 every texel reads as
0: the A3I5 quad is not drawn (alpha 0), the PAL4 and PAL256 quads are
palette colour 0, and nothing changes from frame to frame. On the owner's
console (2026-11-22): the same. So the frame a freeze captures needs nothing
done to it: what is textured from D is simply missing from it (or drawn in
its palette's colour 0). Results go to slot3.txt at the root of the card.
