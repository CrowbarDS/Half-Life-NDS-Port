# DS memory, phase 0: results to fill in

What a DS / DS Lite has left on the heaviest maps, and what its card costs to
read, measured on hardware before the map archives are changed for it. An
emulator run is not a hardware check: say which each figure is.

## How to measure

**Card reads.** Build `tools/hwtest/cardread/` (see `tools/hwtest/README.md`),
put `hl_cardread_probe.nds` on the card that holds the converted game, and
start it. It appends its figures to `_nds/hl_nds/cardread.txt`. Run it once
per flashcard, and once more with the card freshly copied if the archives
were copied in pieces.

**Heap.** A release build with the debug readout on (Options) writes two
lines per map to `_nds/hl_nds/data/perf.txt`:

- `room:` at the end of the load: `top`, `largest hole`, `sounds L of K`, `reserve`;
- `play:` when the map is left (a level change, or the pause menu's QUIT TO
  MAIN MENU): `rise` (what play took past the load), `top`, `reserve`.

Arrive in each map by its level change from the map before, not by a debug
start (that is lighter), walk and fight for about three minutes, then leave
it. The converter's own line for the map (`hlds-convert verify
maps/<map>.npac`, or the cook log) gives `sounds loaded with the map N KB`.

The emulator's sweep gives the same lines for many maps at once:
`MELONDS=... tools/emu/room.sh ds c1a3 c2a5 ...` (it backs up and restores the
card's saves and settings).

## Card reads (from cardread.txt)

Fill in the worst (`most`) figure, in µs; the mean in brackets.

| card | console, mode | archive, MB, fragments | far 4K, fast seek on | far 4K, off | near 4K, on | far+4 4K, on | 16K far, on | cold |
|---|---|---|---|---|---|---|---|---|
| R4-type: | DS Lite, DS | | | | | | | |
| DSpico | DS Lite, DS | | | | | | | |
| DSi SD | DSi, DSi mode | | | | | | | |

Fragments: NEA gives an archive a fast-seek table only if it fits 2 KB in
DS mode (255 pieces) or 4 KB in DSi mode (511 pieces); one in more pieces
seeks the slow way, its chain followed through the FAT, which costs more the
further into the file and the more pieces it has. (Before, a 2 KB table was
always given and its answer not looked at: past 255 pieces, far reads were
wrong.) The game's `perf.txt` says it per map: `mount:` lines, and `seek`
in `room:`. Note which archives, if any, are over.

### A card written to over time: archives in many pieces

NEA's `tests/npac_frag` (with `FRAGTEST_OK` and `FRAGTEST_KEEP` on a card
you can spare) leaves archives in 1, 200, 400 and 600 pieces in
`_nds/hl_nds/fragtest/`; the probe then times them. Worst far 4 KB read, µs
(mean in brackets):

| card, console, mode | pieces | fast seek on | fast seek off (what NEA falls back to) |
|---|---|---|---|
| melonDS, DS mode, 4 KB clusters (emulator: not a card's times) | 1 | 1543 (1479) | 1557 (1492) |
| | 200 | 1599 (1550) | 1925 (1695) |
| | 400 | 1696 (1627) | 2311 (1911) |
| | 600 | 1793 (1698) | 2735 (2124) |
| R4-type: | 1 / 200 / 400 / 600 | | |
| DSpico | 1 / 200 / 400 / 600 | | |
| DSi SD, DSi mode | 1 / 200 / 400 / 600 | | |

For paging (phase 3), the worst read on a card that falls back is the one to
judge the 3 ms / 6 ms limits by: the `seek SLOW` maps of that card are held to
the "fast seek off" column, at their piece count.

## Heap (from perf.txt)

| map | how entered | load top KB | largest hole KB | sounds L/K | reserve (load) | play rise KB | reserve (play) | kept sounds KB (converter) | verdict |
|---|---|---|---|---|---|---|---|---|---|
| c1a3 | | | | | | | | | |
| c1a3a | | | | | | | | | |
| c2a2b1 | | | | | | | | | |
| c2a5 | | | | | | | | | |
| t0a0b | | | | | | | | | |
| c4a1 | | | | | | | | | |
| (the overnight report's 38 tight maps, as time allows) | | | | | | | | | |

## Go / no-go

A map needs **no paging** only if, on DS Lite hardware, entered by its level change:

1. the release reserve is never let go: `reserve held` in both lines;
2. at the end of the load, `top + largest hole >= 160 KB + kept sounds + 64 KB`
   (160 KB is `kPlayReserve`, what play was measured to take at most);
3. `play rise <= 160 KB` over the three minutes;
4. every sound kept is loaded: `sounds L of K` with L = K.

**Phase 3 (paging the world's geometry) goes ahead** if maps fail 1 to 4 and
the worst far 4 KB read, with fast seek as the game has it, is at most
**3 ms** on every flashcard tried. The world draw then gives a frame at most
one page read made at once; what does not fit waits for the next frame.
Between 3 and 6 ms no read is made at once: pages come only from reading
ahead. Above 6 ms paging is not planned on that card; option B′ (pages
packed on the card) or the Expansion Pak placement come first.

| decision | |
|---|---|
| maps that need nothing | |
| maps paging alone fixes (converter's "less on a DS" against the shortfall) | |
| maps that need the Pak placement or option C as well (c2a5, c4a1?) | |
| worst far read (ms), fast seek on / off | |
| go / no-go for phase 3 | |
