# Rules for coding agents

These apply to any coding agent working in this repository.

## Commits

- Ask for the owner's approval before every commit and every push. Show the full message first. Approval covers only what was shown.
- One commit is one topic. If the description would go over its limit, split the commit.
- Title: 128 characters at most, aim for about 72. Format `<area>: <what>`, imperative mood, no trailing period.
- Description: 256 characters at most in total, 1 to 3 short bullets. Optional if the title is enough.
- Say why, or what changes visibly, not how the code does it.
- No `Co-Authored-By` or other attribution lines. No "Generated with" lines.
- No file lists, memory sizes, format version numbers or step-by-step narration of the code.
- Count the characters before committing.

Example:

    net: add end-of-match intermission screen

    - Camera on info_intermission point, scores and stats shown
    - Duration set by host

## Game data

- Never add game files to the repository: Valve or mod assets, or anything converted from them. `sdroot/`, `sdroot-stage/` and `build/` are ignored for this reason.
- Never publish a build made with `HLDS_CHILD_TESTPACK`: it puts a map's data in the child ROM.
- The one exception is the owner's real captures in `docs/screenshots/`.

## Art and AI

- AI may help with the user interface only: menu layout, frames, buttons, simple icons.
- Every other visual or audio asset must not be AI-generated: textures, models, sprites, illustrations, title or loading pictures, logos, cover or banner art, sounds, music, voices, and any screenshot, preview or video of the game.
- Such assets are made by a person, or come from the player's own game files through the converter.

## Code

- No heap allocation during gameplay. Use static pools.
- Fixed point in hot paths.
- No runtime division in hot paths. `tools/arm9-report.sh` lists what the hot list calls.
- Nothing in a DS guest's match peak that a menu uses.
- No interrupt handler may use the divider or the square-root unit, directly or through any call: that covers our 64-bit divide and root, and libnds's `div32`, `sqrt32`, `normalizef32` and the like. Each unit holds one sum, and libnds's routines don't hold interrupts off. `tools/irq-divider.py` fails the build otherwise; a new handler goes in its list.
- Settings are written when leaving a page, not on every change.
- Check integer maths with integers: a root by `r*r <= v < (r+1)*(r+1)`, a quotient by Python's `//` or `math.isqrt`, never through a double. melonDS is not the reference for the console's divider and root units: `tools/hwtest/` probes are.
- A product of two 16-bit values in ARM code: `shared/HlDsp.hpp` (SMULxy, SMULWy, SMLAxy). A 64-bit divide by a constant whose numerator fits 32 bits: divide in 32 bits, which is a multiply.
- `shared/` is compiled into the ROM and the converter, so it stays free of console-only code unless guarded.
- `helpSrc/` is reference material and is git-ignored. Write new code from the behaviour you need; do not copy or translate code from it.

## Before any commit

Build both ROMs and the converter, then run the host suites:

```sh
HLDS_JOBS=2 ./make.sh
cmake -S tools/hlds-convert -B build/convert
cmake --build build/convert -j2
ctest --test-dir build/convert
```

- Say what was checked on hardware and what was not. An emulator run isn't a hardware check.
- Without `HLDS_VALVE_DIR`, many suites skip their real work and still pass. Set it to a Half-Life `valve` folder to run the checks that play the maps, and say which way you ran them.
- Rebuilds are memory-hungry. Run one build at a time, and use `HLDS_JOBS=1` after changing a widely included `shared/` header.
- Emulator runs (`tools/emu/`) use the repository's `sdroot/`, which the owner also plays from. Back up its saves and settings first, and restore them afterwards.

## Where things live

| Folder | What it is |
|---|---|
| `shared/` | File formats, fixed-point maths, game rules: compiled by the ROM and the converter, so host tests run the console's code |
| `source/` | The ARM9 engine |
| `dll/` | The game library, `hl.dsl`, loaded by the engine at start-up |
| `child/` | The Download Play child: the same engine and game, linked into one program |
| `arm7/` | The ARM7 program |
| `teak/` | The DSi's DSP program |
| `tools/hlds-convert/` | The converter: `core` (no Qt), `cli`, `gui`, `tests` |
| `tools/emu/`, `tools/ref/`, `tools/hwtest/` | Emulator runs, reference runs of the real game, hardware probes |
| `architectds_nea_mod/` | The ROM build system |
| `docs/TECHNICAL.md` | How and why things work |

## Forks the build needs

Installed under `$BLOCKSDSEXT`, on top of BlocksDS and the Wonderful Toolchain:

| Fork | Installed as | Used for |
|---|---|---|
| [nitro-engine](https://github.com/Warioware64/nitro-engine) | `nitro-engine-advanced` | Graphics engine |
| [dswifi](https://github.com/Warioware64/dswifi) | `dswifi_dl` | Wireless and Download Play |
| [teak-llvm](https://github.com/Warioware64/teak-llvm) | `llvm-teak` | DSP compiler (optional) |
| [libteak](https://github.com/Warioware64/libteak) | `libteak` | DSP library (optional) |

Without the DSP toolchain the ROM still builds, with no DSP audio.
