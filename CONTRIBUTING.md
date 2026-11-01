# Contributing

Thanks for helping. Please read the two forbidden items below before anything else.

## Forbidden

**No game data.** Never commit a game asset or anything converted from one: Valve or mod maps, models, textures, sounds, or converter output. `sdroot/` and `build/` are ignored for this reason. The only pictures of the game in the repository are the maintainer's real captures, in `docs/screenshots/`.

**No AI-generated art or audio.** AI may help with the user interface only: menu layout, frames, buttons, simple icons. Everything else must not be AI-generated: textures, models, sprites, illustrations, title or loading pictures, logos, cover or banner art, sounds, music, voices, and any screenshot, preview or video of the game. These must be made by a person, or come from the player's own game files through the converter. A fake preview could mislead people and harm the project.

## AI-assisted code

AI-assisted code is welcome if you review it, test it, and say so in the pull request.

Reading the Half-Life SDK or Xash3D to understand how the game behaves is fine. Don't copy or translate their code, or any other project's, into new work: write it from the behaviour it needs. Some existing code is adapted from the SDK; when you change it, follow the code that's there.

## Building

### With Docker

```sh
./docker.sh ./make.sh                    # the ROMs
./docker.sh                              # a shell in the container
```

On Windows, or anywhere, use `docker compose run --rm dev ./make.sh`.

- `WITH_TEAK=1` adds the DSP toolchain, which compiles clang: allow about an hour. Without it, the ROM builds with no DSP audio.
- `WITH_QT=1` adds Qt, to check that the converter's window builds in the container.
- If you build the converter both inside and outside the container, use different build folders. CMake remembers its paths.
- If the image build stops with `Could not resolve host`, it's Docker's name server: try again, or set `"dns": ["1.1.1.1", "8.8.8.8"]` in Docker's engine settings.

### Without Docker (Linux)

Install these under `$BLOCKSDSEXT` (`/opt/wonderful/thirdparty/blocksds/external`), at the commits in [docker/versions.env](docker/versions.env):

| | |
|---|---|
| [BlocksDS](https://blocksds.skylyrac.net/docs/setup/) | The SDK and its ARM compiler; the code is C++26, so it needs a current one |
| [ptexconv](https://github.com/Garhoogin/ptexconv) | Used by the converter for textures |
| [Nitro Engine Advanced](https://github.com/Warioware64/nitro-engine) | This fork: `make -f Makefile.blocksds install NEA_MAXMOD=1` |
| [dswifi](https://github.com/Warioware64/dswifi) | This fork: `make && make install` puts it beside the stock one as `dswifi_dl` |
| [teak-llvm](https://github.com/Warioware64/teak-llvm), [libteak](https://github.com/Warioware64/libteak) | Optional, for DSP audio: `./build-teak.sh all` in the first, `make install` in the second |

You also need Python 3, CMake 3.20 or newer, a C++20 compiler, and Qt 6.8 or newer for the converter's window.

```sh
./setup-env.sh          # once: the Python environment for the ROM build
HLDS_JOBS=4 ./make.sh   # the ROMs, with at most four compilers at once
```

### The converter

```sh
cmake -S tools/hlds-convert -B build/convert
cmake --build build/convert -j4
```

On Windows, [MSYS2](https://www.msys2.org/) has what it needs: `pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base}`.

## Testing

```sh
ctest --test-dir build/convert
HLDS_VALVE_DIR=/path/to/Half-Life/valve ctest --test-dir build/convert
```

- `shared/` is compiled into both the ROM and the converter, so these tests run the console's own code.
- Without `HLDS_VALVE_DIR`, suites that need the game's files are reported as skipped, and others run only their data-free checks. CI runs this way, so run with your own game files too when you change game behaviour.
- `build/convert/hlds-testmap <folder>` writes a small map containing no game data, for trying a change on the console in seconds.
- `tools/emu/` runs the ROM in melonDS without a display and takes pictures. `tools/ref/` does the same with the real game, to compare.
- `HLDS_PROFILE=1 ./make.sh` builds a ROM that times itself, and `tools/arm9-report.sh` says what the program is made of.

Say in your pull request what you checked on real hardware and what you didn't. An emulator run isn't a hardware check.

## Code rules

- No heap allocation during gameplay: use static pools.
- Fixed point in hot paths, and no runtime division there.
- Nothing in a DS guest's match peak that a menu uses.
- Settings are written when leaving a page.

How and why things work is in [docs/TECHNICAL.md](docs/TECHNICAL.md), and the layout of the repository is in [AGENTS.md](AGENTS.md).

## Commits

- One commit is one topic.
- Title: at most 128 characters, aiming for about 72. Format `<area>: <what>`, imperative mood, no trailing period.
- Description: at most 256 characters in total, 1 to 3 short bullets.
- Say why, or what changes visibly, not how the code does it.
- No `Co-Authored-By` or other attribution lines, file lists, memory sizes or version numbers of formats.
