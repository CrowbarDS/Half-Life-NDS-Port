#!/usr/bin/env python3

# The project build, expressed with architectds_nea_mod.
#
# Usage (after running ./setup-env.sh):
#   ./make.sh            # build
#   ./make.sh --help     # see all options
#
# No game content is built into the ROM. Every map, texture and sound is made
# by tools/hlds-convert from the player's own copy of Half-Life and read from
# the SD card under _nds/hl_nds/data/ at runtime.
#
# THE GAME is a dynamic library apart, as hl.dll is on the PC: the monsters,
# the weapons, the logic, the saves' map state (dll/hl/, and the shared
# headers it instantiates). It is built against the ELF below and carried in
# the ROM's NitroFS as dsl/hl.dsl; the engine loads it at startup and the two
# speak through the tables in source/game/GameApi.hpp. HL_DSL is defined in
# its build: there the engine's objects and hot code are reached through the
# engine's table.
#
# HOT CODE: a source named Foo.arm.cpp is built as ARM instead of Thumb
# (architectds), and one named Foo.itcm.arm.cpp is ALSO placed in the 32 KB
# ITCM by the BlocksDS linker script, which matches objects on `*.itcm*`. The
# order of the two suffixes matters: architectds tests for the `.arm.cpp`
# ending, so Foo.arm.itcm.cpp would be ITCM but Thumb. A single function is
# marked in the source instead: HL_ARM, HL_ITCM (source/Hot.hpp) and, for the
# code the host shares, HL_HOT (shared/HlFixed.hpp).
#
# HLDS_JOBS=4 in the environment keeps ninja to four compilers at once.

import os

from architectds import *

# HLDS_PROFILE=1 ./make.sh builds the frame-time readout in (see below).
profile = ['HLDS_PROFILE=1'] if os.environ.get('HLDS_PROFILE') else []
# HLDS_BOX_TEST=1 ./make.sh builds the world's box test in (an experiment:
# WorldDraw.hpp, THE BOX TEST).
profile += ['HLDS_BOX_TEST=1'] if os.environ.get('HLDS_BOX_TEST') else []
# HLDS_DEBUG=1 ./make.sh builds the full heap guard in (source/Guard.hpp): a
# header and four bytes after every block, and small blocks held a while once
# given back. Without it (the release) the blocks are the allocator's own,
# with nothing added, and its chain is what is checked.
guard = ['HLDS_GUARD_FULL'] if os.environ.get('HLDS_DEBUG') else []

# The 64-bit divide goes to the console's divider (source/HwMath.s), and to
# the library's for what the divider would answer otherwise.
HW_MATH_WRAPS = ['__aeabi_ldivmod', '__aeabi_uldivmod']

# The wireless library with its IP stack, for online play (a DSi's only):
# its Internet half moved to the DSi-only part of the program, which a DS
# never loads (tools/wifi_twl.py). Made here, before the build is written.
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tools'))
import wifi_twl
WIFI_TWL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build', 'wifi_twl')
wifi_twl.make(os.path.join(os.path.expandvars('${BLOCKSDSEXT}'), 'dswifi_dl'), WIFI_TWL_DIR)

arm9 = Arm9Binary(
    sourcedirs=['source'],
    # 'thirdparty' puts yas on the include path as <yas/...>, the same spelling
    # the host converter uses -- both sides must agree on the serializer.
    includedirs=['source', 'shared', 'thirdparty'],
    # HLDS_PROFILE=1 in the environment builds the frame-time readout in:
    # where the frame went, split between input, the BSP walk and the send to
    # the geometry engine, on the overlay and in perf.txt. Off, every one of
    # its entry points is an empty inline (DebugLog.hpp), so the shipped
    # build carries none of it.
    defines=['HL_HW_MATH'] + profile + guard,
    # The wireless is the dswifi fork that also does DS Download Play
    # (github.com/Warioware64/dswifi, installed as "dswifi_dl" beside the
    # stock one). Its headers have the stock names (dswifi9.h), so
    # ${BLOCKSDS}/libs/dswifi must NOT be among the libdirs as well: which
    # set of headers came first would decide the build. It is the build
    # with the IP stack (online play), as wifi_twl made it.
    libs=['NEA', 'nds9', 'dswifi_dl9_twl'],
    libdirs=[
        '${BLOCKSDS}/libs/libnds',
        '${BLOCKSDSEXT}/dswifi_dl',
        '${BLOCKSDSEXT}/nitro-engine-advanced',
        WIFI_TWL_DIR,
    ],
    # -D_LITTLE_ENDIAN + force-including yas's endian config first locks yas to
    # little-endian in every TU *before* any <nds.h> can define _BIG_ENDIAN
    # (which yas would otherwise misread as "big-endian"). The converter writes
    # the yas archives on the host and the DS reads them back, so a byte-order
    # disagreement between the two would corrupt them silently.
    # stack-usage: no function may take more than 4 KB of the stack, which
    # is 15 KB of DTCM in all (Game::destroy's 9 KB overflowed it when a map
    # was left, 2026-10-30). The game library and the child are held to it too.
    cxxflags='-Wall -Werror=stack-usage=4096 -O2 -std=gnu++26 -D_LITTLE_ENDIAN -include yas/detail/config/endian.hpp',
    # Every malloc, free and the rest goes through the guarded heap
    # (source/Guard.hpp), which finds a write past a block or into one given
    # back, and says whose block it was.
    # (calloc kept, though nothing calls the allocator's own: the game
    # library is linked against the program's names, and asks for it by name
    # -- GameDll's resolver gives it the guarded one.)
    ldflags=' '.join(['-Wl,--wrap=' + f for f in (
        'malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'posix_memalign')] +
        ['-Wl,--wrap=' + f for f in HW_MATH_WRAPS] +
        ['-Wl,--undefined=calloc']),
)

arm9.generate_elf()

# The other processor's program: the game's own (arm7/source) -- what
# BlocksDS's default does, with the game's own sound core. It links
# the wireless library of the same build as the ARM9's: the two share the
# structures they talk through, and the library refuses a pair that do not
# match.
arm7 = Arm7Binary(
    sourcedirs=['arm7/source'],
    includedirs=['shared'],
    libs=['nds7', 'dswifi_dl7'],
    libdirs=['${BLOCKSDS}/libs/libnds', '${BLOCKSDSEXT}/dswifi_dl'],
    cxxflags='-Wall -O2 -std=gnu++20 -fno-exceptions -fno-rtti',
)
arm7.generate_elf()

hl = Arm9DynamicLibrary(
    name='hl',
    main_binary=arm9,
    sourcedirs=['dll/hl'],
    includedirs=['source', 'shared', 'thirdparty'],
    # (What Arm9Binary defines by itself, as well: libnds's and the engine's
    # headers lay their structures out by them, and the library reads the
    # engine's.)
    defines=['__NDS__', '__BLOCKSDS__', 'ARM9', 'HL_HW_MATH', 'HL_DSL'] + profile,
    # (Only their headers: the library links nothing of its own. What it
    # calls of libc and the rest is the ELF's, by address.)
    libdirs=[
        '${BLOCKSDS}/libs/libnds',
        '${BLOCKSDSEXT}/dswifi_dl',
        '${BLOCKSDSEXT}/nitro-engine-advanced',
    ],
    cxxflags='-Wall -Werror=stack-usage=4096 -O2 -std=gnu++26 -D_LITTLE_ENDIAN -include yas/detail/config/endian.hpp',
    # (By these names it finds the program's: without them, the library's.)
    ldflags=' '.join(['-Wl,--wrap=' + f for f in HW_MATH_WRAPS]),
    # What dsltool writes (build/hl/hl.full.dsl) has a name for every symbol
    # and every branch within the library among its relocations; the loader
    # keeps the names and 8 bytes per relocation on the heap for good. Only
    # what it acts on is kept, and the build stops unless the library then
    # loads to the same bytes with the same code sum (tools/dsl_strip.py).
    post_tool=('python3 tools/dsl_strip.py', ['tools/dsl_strip.py']),
)
hl.generate_dsl()

nitrofs = NitroFS()
nitrofs.add_arm9_dsl(hl)
# The program the host sends consoles without the game by Download Play
# (child/build.py: built after this one, whose ARM7 program it uses -- so it
# is here from make.sh's second pass of this file on).
CHILD_ROM = os.path.join('child', 'out', 'hl-child-twl.nds')
if os.path.isfile(CHILD_ROM):
    nitrofs.add_file(CHILD_ROM, 'dlplay/child.nds')
nitrofs.generate_image()

nds = NdsRom(
    binaries=[arm9, arm7, hl, nitrofs],
    nds_path='half-life-nds-port.nds',
    game_icon="Logo.bmp",
    game_title='HALF-LIFE DS',
    game_subtitle='Ported by CrowbarDS',
    game_author='github.com/CrowbarDS',
    # 2 = NDS + DSi hybrid: one ROM that runs on a DS and, booted in DSi mode,
    # gets the 16 MB of RAM and the 134 MHz clock (Global::Init).
    unit_code=2,
    # The game code's W and the ROM version 0x57 ('W'): TWiLight Menu++ takes
    # a homebrew so marked as able to draw for a 3DS's wide screen, offers
    # 16:10 for it, and swaps the wide TwlBg in (settings: Screen).
    game_info=('WHLD', '00', 'HOMEBREW', 0x57),
)
nds.generate_nds()

nds.run_command_line_arguments()
