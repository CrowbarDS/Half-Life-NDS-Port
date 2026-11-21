#!/usr/bin/env bash
# The I-cache profiler: a melonDS of its own, with the ARM9's instruction cache
# modelled (melonDS has the model but leaves it off: CP15.cpp's CodeRead32) and
# each 32-byte line of main RAM's lookups and misses counted -- by the part of
# the frame the game says it is in (a phase-counting build, HLDS_ICACHE_PHASES:
# source/DebugLog.hpp writes it to a register only this melonDS has), and
# what runs in an interrupt apart. For choosing
# what goes in ITCM (tools/icache_rank.py), never for timing: its timings are
# not the console's.
#
#   tools/icache/setup.sh <melonDS source folder>
#
# copies the source to build/tools/melonds-icache, applies
# tools/icache/melonds-icache.patch and builds build/tools/melonds-icache/build/melonDS.
# Run with HLDS_ICACHE_OUT=<prefix> (tools/icache/scenes.sh does): every 2^26
# lookups it writes the counts so far to <prefix>.<n>. The melonDS the
# emulator scripts use otherwise is left as it is.
set -eu
SRC=${1:?usage: tools/icache/setup.sh <melonDS source folder>}
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/tools/melonds-icache"
mkdir -p "$OUT"
rsync -a --delete --exclude build "$SRC/" "$OUT/"
patch -d "$OUT" -p1 < "$ROOT/tools/icache/melonds-icache.patch"
cmake -S "$OUT" -B "$OUT/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_SDL=ON -DENABLE_JIT=ON
cmake --build "$OUT/build" -j"${HLDS_JOBS:-2}"
echo "built $OUT/build/melonDS"
