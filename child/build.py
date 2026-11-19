#!/usr/bin/env python3

# The DS Download Play child: the game, for a console that has no card.
#
# An ordinary architectds ROM, built from this folder so that its build tree
# (child/build/...) is apart from the parent's. It is the parent's own code --
# the engine (engine -> ../source) and the game (game -> ../dll/hl) -- built
# once more with HLDS_CHILD:
#
#  - the game's core and the match's rules are linked into the program:
#    Download Play sends the header and the two processors' programs, never
#    a file system. The bosses and the soldiers are libraries apart (family ->
#    ../dll/family, built against this program's ELF, as the parent's are),
#    which the parent carries (dlplay/) and sends a demo's console when one
#    of its maps needs them (source/game/Families.cpp);
#  - no card is looked for; the game's data is what it is sent, served from
#    memory by the "air:" drive (source/net/AirDevice.hpp);
#  - a Nintendo DS program only (unit code 0): sent over the air it runs in
#    DS mode, even on a DSi.
#
# No game content is in it. The host sends that after it has started, out of
# its own card (tools/hlds-convert's air/ archives). For a test with no host,
# HLDS_CHILD_TESTPACK=<map> builds one map's in (tools/child_testpack.py):
# that build is for an emulator on this PC, never to be given to anyone.
#
# Usage: ./make.sh builds it after the parent; or, from this folder with the
# project's venv active, python build.py.

import os
import subprocess
import sys

from architectds import *

ROOT = os.path.abspath('..')
TESTPACK = 'generated/airpack/airpack.bin'

CLEANING = '-c' in sys.argv or '--clean' in sys.argv
if not CLEANING:
    os.makedirs(os.path.dirname(TESTPACK), exist_ok=True)
    if subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'child_testpack.py'), TESTPACK]).returncode != 0:
        raise SystemExit('the child test pack was not made')

# HLDS_CHILD_FROMCARD=<map>: a test that reads the streams off the emulator's
# card (air/), a packet's worth at a time, rather than linking them in: what
# is left of the memory is then what a guest sent them over the air has.
fromcard = os.environ.get('HLDS_CHILD_FROMCARD', '').strip()
extra = ['HLDS_CHILD_FROMCARD=\\"%s\\"' % fromcard] if fromcard else []
# HLDS_CHILD_VRAMSOUND=1: the test of a sound played out of bank D given to
# the ARM7 (Sound.hpp, vramTest), at the start.
if os.environ.get('HLDS_CHILD_VRAMSOUND'):
    extra.append('HLDS_CHILD_VRAMSOUND')
# HLDS_DEV=1: a development build, as the parent's (../build.py): the test
# starts (debug_start.txt, debug_net.txt) and their walks.
if os.environ.get('HLDS_DEV'):
    extra.append('HLDS_DEV=1')
# HLDS_DEBUG=1: the full heap guard, as the parent's (../build.py).
if os.environ.get('HLDS_DEBUG'):
    extra.append('HLDS_GUARD_FULL')

arm9 = Arm9Binary(
    sourcedirs=['engine', 'game'],
    includedirs=['engine', 'shared', 'thirdparty'],
    # HL_FAMILIES_APART: the bosses and the soldiers are not linked in.
    defines=['HL_HW_MATH', 'HLDS_CHILD', 'HL_STATIC_GAME', 'HL_FAMILIES_APART'] + extra,
    # (The same libraries as the parent's: see ../build.py.)
    libs=['NEA', 'nds9', 'dswifi_dl9_noip'],
    libdirs=[
        '${BLOCKSDS}/libs/libnds',
        '${BLOCKSDSEXT}/dswifi_dl',
        '${BLOCKSDSEXT}/nitro-engine-advanced',
    ],
    cxxflags='-Wall -Werror=stack-usage=4096 -O2 -std=gnu++26 -fno-threadsafe-statics -D_LITTLE_ENDIAN -include yas/detail/config/endian.hpp',
    ldflags=' '.join(['-Wl,--wrap=' + f for f in (
        'malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'posix_memalign')] +
        # (The divide: source/HwMath.s, and ../build.py.)
        ['-Wl,--wrap=__aeabi_ldivmod', '-Wl,--wrap=__aeabi_uldivmod'] +
        ['-Wl,--undefined=calloc'] +
        # (printf and scanf at their integer level: as the parent's.)
        ['-Wl,--undefined=__i_vfprintf', '-Wl,--undefined=__i_vfscanf',
         '-Wl,--defsym=vfprintf=__i_vfprintf', '-Wl,--defsym=vfscanf=__i_vfscanf'] +
        # (DTCM's own data and heap: as the parent's, ../build.py.)
        ['-Wl,--defsym=__dtcm_data_size=1024']),
)
# airpack_bin[] / airpack_bin_size: empty unless a test asked for a map.
arm9.add_data([os.path.dirname(TESTPACK)])
# (Its ID, as the parent's: ../tools/program_id.py.)
arm9.post_link = ('python3 ../tools/program_id.py stamp', ['../tools/program_id.py'])
arm9.generate_elf()

# The bosses and the soldiers, built against this program as the parent's are
# against the parent (../build.py: THE FAMILIES), with this program's
# defines: the parent puts them in its NitroFS as dlplay/<family>.dsl.
FAMILY_WRAPS = ['malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'posix_memalign',
                '__aeabi_ldivmod', '__aeabi_uldivmod']
families = []
for family in ('boss', 'military'):
    lib = Arm9DynamicLibrary(
        name=family,
        main_binary=arm9,
        sourcedirs=['family'],
        includedirs=['engine', 'shared', 'thirdparty'],
        defines=['__NDS__', '__BLOCKSDS__', 'ARM9', 'HL_HW_MATH', 'HLDS_CHILD', 'HL_STATIC_GAME',
                 'HL_FAMILIES_APART', 'HL_FAMILY_' + family.upper()] + extra,
        libdirs=[
            '${BLOCKSDS}/libs/libnds',
            '${BLOCKSDSEXT}/dswifi_dl',
            '${BLOCKSDSEXT}/nitro-engine-advanced',
        ],
        cxxflags='-Wall -Werror=stack-usage=4096 -O2 -std=gnu++26 -fno-threadsafe-statics -mlong-calls -D_LITTLE_ENDIAN '
                 '-include yas/detail/config/endian.hpp',
        ldflags_first='-Wl,--just-symbols=' + arm9.elf_path,
        link_deps=[arm9.elf_path],
        ldflags=' '.join(['-Wl,--wrap=' + f for f in FAMILY_WRAPS] +
                         ['-Wl,--gc-sections', '-Wl,-u,GetFamilyAPI', '-Wl,-u,hlFamilyProgramId']),
        post_tool=('python3 ../tools/dsl_family.py ' + arm9.elf_path,
                   ['../tools/dsl_family.py', '../tools/dsl_strip.py', '../tools/program_id.py', arm9.elf_path]),
    )
    lib.generate_dsl()
    families.append(lib)

# The parent's ARM7 program, as it is (built first, by ../make.sh): the
# sound core and the wireless are the same.
arm7 = Arm7BinaryDefault(elf_path=os.path.join(ROOT, 'build', 'arm7', 'arm7.elf'))

nds = NdsRom(
    binaries=[arm9, arm7] + families,
    nds_path='out/hl-child.nds',
    game_icon='../Logo.bmp',
    game_title='HALF-LIFE DS',
    game_subtitle='Sent by Download Play',
    game_author='github.com/CrowbarDS',
    # (Hybrid: tools/dlp_twl_child.py folds its DSi parts in, for a console
    # that starts it in DSi mode.)
    unit_code=2,
)
nds.generate_nds()

nds.run_command_line_arguments()
