#!/usr/bin/env python3

# The DS Download Play child: the game, for a console that has no card.
#
# An ordinary architectds ROM, built from this folder so that its build tree
# (child/build/...) is apart from the parent's. It is the parent's own code --
# the engine (engine -> ../source) and the game (game -> ../dll/hl) -- built
# once more with HLDS_CHILD:
#
#  - the game is linked into the program rather than loaded from NitroFS as
#    dsl/hl.dsl: Download Play sends the header and the two processors'
#    programs, never a file system;
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

arm9 = Arm9Binary(
    sourcedirs=['engine', 'game'],
    includedirs=['engine', 'shared', 'thirdparty'],
    defines=['HL_HW_MATH', 'HLDS_CHILD'] + extra,
    # (The same libraries as the parent's: see ../build.py.)
    libs=['NEA', 'nds9', 'dswifi_dl9_noip'],
    libdirs=[
        '${BLOCKSDS}/libs/libnds',
        '${BLOCKSDSEXT}/dswifi_dl',
        '${BLOCKSDSEXT}/nitro-engine-advanced',
    ],
    cxxflags='-Wall -Werror=stack-usage=4096 -O2 -std=gnu++26 -D_LITTLE_ENDIAN -include yas/detail/config/endian.hpp',
    ldflags=' '.join(['-Wl,--wrap=' + f for f in (
        'malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'posix_memalign')] +
        # (The divide: source/HwMath.s, and ../build.py.)
        ['-Wl,--wrap=__aeabi_ldivmod', '-Wl,--wrap=__aeabi_uldivmod'] +
        ['-Wl,--undefined=calloc']),
)
# airpack_bin[] / airpack_bin_size: empty unless a test asked for a map.
arm9.add_data([os.path.dirname(TESTPACK)])
arm9.generate_elf()

# The parent's ARM7 program, as it is (built first, by ../make.sh): the
# sound core and the wireless are the same.
arm7 = Arm7BinaryDefault(elf_path=os.path.join(ROOT, 'build', 'arm7', 'arm7.elf'))

nds = NdsRom(
    binaries=[arm9, arm7],
    nds_path='out/hl-child.nds',
    game_icon='../Logo.bmp',
    game_title='HALF-LIFE DS',
    game_subtitle='Sent by Download Play',
    game_author='github.com/Warioware64',
    # (Hybrid: tools/dlp_twl_child.py folds its DSi parts in, for a console
    # that starts it in DSi mode.)
    unit_code=2,
)
nds.generate_nds()

nds.run_command_line_arguments()
