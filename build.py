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
# THE GAME's core -- the monsters, the weapons, the logic, the saves' map
# state (dll/hl/, and the shared headers it instantiates) -- is linked into
# the program (HL_STATIC_GAME), and still speaks to the engine through the
# tables in source/game/GameApi.hpp, as hl.dll does on the PC. What a map
# can do without -- the bosses, the soldiers, the match's rules -- is a
# library of its own, loaded with the map that needs it.
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
# HLDS_VOICE_DUMP=1 ./make.sh: each talk (TALK held, online) written to the
# card's data/voice.wav as it is let go -- heard (left) and as sent (right);
# and the first 8 s of what others said, as it came, to data/heard.wav (with
# heard.txt: how many packets were brought down or dropped).
profile += ['HLDS_VOICE_DUMP=1'] if os.environ.get('HLDS_VOICE_DUMP') else []
# HLDS_DEBUG=1 ./make.sh builds the full heap guard in (source/Guard.hpp): a
# header and four bytes after every block, and small blocks held a while once
# given back. Without it (the release) the blocks are the allocator's own,
# with nothing added, and its chain is what is checked.
# (HLDS_DEBUG_BUILD: what else a debug build says, on the screen and in
# perf.txt -- an archive that seeks the slow way.)
guard = ['HLDS_GUARD_FULL', 'HLDS_DEBUG_BUILD'] if os.environ.get('HLDS_DEBUG') else []
# HLDS_HEAP_TRACE=1 ./make.sh: a debug build that says who holds the heap
# (Guard.hpp: the heap trace) -- each block's caller and generation, the
# blocks still held from before each map's load, and its peak, in
# data/heaptrace.txt (tools/heaptrace.py names the callers). On the
# release allocator, 8 bytes a block: not with HLDS_DEBUG's full guard.
trace = os.environ.get('HLDS_HEAP_TRACE')
if trace and guard:
    raise SystemExit('HLDS_HEAP_TRACE and HLDS_DEBUG are two ways of watching the heap: one at a time')
if trace:
    guard = ['HLDS_HEAP_TRACE', 'HLDS_DEBUG_BUILD']

# The 64-bit divide goes to the console's divider (source/HwMath.s), and to
# the library's for what the divider would answer otherwise.
HW_MATH_WRAPS = ['__aeabi_ldivmod', '__aeabi_uldivmod']
# picolibc's printf and scanf at their integer level (the program's and the
# child's): see the program's link below.
INTEGER_STDIO = ['-Wl,--undefined=__i_vfprintf', '-Wl,--undefined=__i_vfscanf',
                 '-Wl,--defsym=vfprintf=__i_vfprintf', '-Wl,--defsym=vfscanf=__i_vfscanf']
# Bytes at the top of DTCM for its own data and for dtcm_malloc; the stack
# has the rest of DTCM under them. (The child sets the same: child/build.py.)
DTCM_DATA_SIZE = 1024

# The wireless library with its IP stack, for online play (a DSi's only):
# its Internet half moved to the DSi-only part of the program, which a DS
# never loads (tools/wifi_twl.py). Made here, before the build is written.
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tools'))
import wifi_twl
WIFI_TWL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build', 'wifi_twl')
wifi_twl.make(os.path.join(os.path.expandvars('${BLOCKSDSEXT}'), 'dswifi_dl'), WIFI_TWL_DIR)

arm9 = Arm9Binary(
    # The engine (source/) and the game's core (dll/hl/), linked together
    # (HL_STATIC_GAME): a library of the core cost the heap ~53 KB more than
    # the same code linked in -- its loader's veneer pool, code both sides
    # carried, the forwarders between them. Families the core can do without
    # are libraries of their own, loaded with the map that needs them.
    sourcedirs=['source', 'dll/hl'],
    # 'thirdparty' puts yas on the include path as <yas/...>, the same spelling
    # the host converter uses -- both sides must agree on the serializer.
    includedirs=['source', 'shared', 'thirdparty'],
    # HLDS_PROFILE=1 in the environment builds the frame-time readout in:
    # where the frame went, split between input, the BSP walk and the send to
    # the geometry engine, on the overlay and in perf.txt. Off, every one of
    # its entry points is an empty inline (DebugLog.hpp), so the shipped
    # build carries none of it.
    # HL_FAMILIES_APART, HL_NET_APART: the families (below) are not linked in.
    defines=['HL_HW_MATH', 'HL_STATIC_GAME', 'HL_FAMILIES_APART', 'HL_NET_APART'] + profile + guard,
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
    # is 14.4 KB of DTCM in all (Game::destroy's 9 KB overflowed it when a map
    # was left, 2026-10-30). The game library and the child are held to it too.
    # -fno-threadsafe-statics: a function's static is made with no lock
    # (source/StdHalts.cpp says why).
    cxxflags='-Wall -Werror=stack-usage=4096 -O2 -std=gnu++26 -fno-threadsafe-statics -D_LITTLE_ENDIAN '
             '-include yas/detail/config/endian.hpp',
    # Every malloc, free and the rest goes through the guarded heap
    # (source/Guard.hpp), which finds a write past a block or into one given
    # back, and says whose block it was.
    # (calloc kept, though nothing calls the allocator's own: the game
    # library is linked against the program's names, and asks for it by name
    # -- GameDll's resolver gives it the guarded one.)
    ldflags=' '.join(['-Wl,--wrap=' + f for f in (
        'malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'posix_memalign')] +
        ['-Wl,--wrap=' + f for f in HW_MATH_WRAPS] +
        # (A heap trace's: operator new says whose its block is.)
        (['-Wl,--wrap=_Znwj', '-Wl,--wrap=_Znaj'] if trace else []) +
        ['-Wl,--undefined=calloc'] +
        # printf and scanf without floats or 64-bit numbers (picolibc's
        # integer level): nothing in the program formats or reads either,
        # and the full ones cost every console about 6 KB of heap.
        INTEGER_STDIO +
        # DTCM's own data and its heap (dtcm_malloc), at the top of DTCM;
        # the stack has the rest under it.
        ['-Wl,--defsym=__dtcm_data_size=' + str(DTCM_DATA_SIZE)]),
    # The program's ID (a hash of this ELF), stamped as it is linked: the
    # families are built for this program alone, and say so
    # (tools/program_id.py; source/game/FamilyApi.hpp).
    post_link=('python3 tools/program_id.py stamp', ['tools/program_id.py']),
)

arm9.generate_elf()

# THE FAMILIES: the bosses, the soldiers, the match's rules, each a library
# of its own (dll/family/, once for each), loaded with the map that needs it
# (source/game/Families.cpp). Each is linked against the program's ELF,
# taken first: what it calls of the core, of libc and the rest is the
# program's, at the program's addresses, and none of it is copied in.
# -mlong-calls: a call into the program is to an address in a word, which
# reaches anywhere (a branch from the heap of a 16 MB DSi might not).
# Then tools/dsl_family.py: the program's symbols marked as such, what the
# loader would keep for nothing dropped, the program's ID stamped in.
FAMILY_WRAPS = ['malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'posix_memalign'] + HW_MATH_WRAPS
families = []
for family in ('boss', 'military', 'net'):
    lib = Arm9DynamicLibrary(
        name=family,
        main_binary=arm9,
        sourcedirs=['dll/family'],
        includedirs=['source', 'shared', 'thirdparty'],
        # (What Arm9Binary defines by itself, as well: libnds's and the
        # engine's headers lay their structures out by them. And the
        # program's own, all of them: the core's code it shares is the same
        # only if it is compiled the same.)
        defines=['__NDS__', '__BLOCKSDS__', 'ARM9', 'HL_HW_MATH', 'HL_STATIC_GAME', 'HL_FAMILIES_APART',
                 'HL_NET_APART', 'HL_FAMILY_' + family.upper()] + profile + guard,
        # (Only their headers: a family links nothing of its own.)
        libdirs=[
            '${BLOCKSDS}/libs/libnds',
            '${BLOCKSDSEXT}/dswifi_dl',
            '${BLOCKSDSEXT}/nitro-engine-advanced',
        ],
        cxxflags='-Wall -Werror=stack-usage=4096 -O2 -std=gnu++26 -fno-threadsafe-statics -mlong-calls -D_LITTLE_ENDIAN '
                 '-include yas/detail/config/endian.hpp',
        ldflags_first='-Wl,--just-symbols=' + arm9.elf_path,
        link_deps=[arm9.elf_path],
        # (By the wrapped names it finds the program's guarded heap and
        # divide. Only what its two exports reach is kept.)
        ldflags=' '.join(['-Wl,--wrap=' + f for f in FAMILY_WRAPS] +
                         ['-Wl,--gc-sections', '-Wl,-u,GetFamilyAPI', '-Wl,-u,hlFamilyProgramId']),
        post_tool=('python3 tools/dsl_family.py ' + arm9.elf_path,
                   ['tools/dsl_family.py', 'tools/dsl_strip.py', 'tools/program_id.py', arm9.elf_path]),
    )
    lib.generate_dsl()
    families.append(lib)

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


nitrofs = NitroFS()
for lib in families:
    nitrofs.add_arm9_dsl(lib)
# The program the host sends consoles without the game by Download Play
# (child/build.py: built after this one, whose ARM7 program it uses -- so it
# is here from make.sh's second pass of this file on).
CHILD_ROM = os.path.join('child', 'out', 'hl-child-twl.nds')
if os.path.isfile(CHILD_ROM):
    nitrofs.add_file(CHILD_ROM, 'dlplay/child.nds')
    # ... and the families built for it, sent to a demo's console that needs
    # one (source/net/NetGame.cpp: a family's stream).
    for family in ('boss', 'military'):
        sent = os.path.join('child', 'build', family, family + '.dsl')
        if os.path.isfile(sent):
            nitrofs.add_file(sent, 'dlplay/' + family + '.dsl')
nitrofs.generate_image()

nds = NdsRom(
    binaries=[arm9, arm7] + families + [nitrofs],
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
