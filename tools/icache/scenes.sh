#!/usr/bin/env bash
# The I-cache profiler's scenes (tools/icache/setup.sh, tools/icache_rank.py):
# a ROM -- an HLDS_ITCM_OFF=1 build, to see every function from main memory --
# run in the profiling melonDS on four scenes, each one's snapshots in
# <out>/<scene>/s.<n>.
#
#   tools/icache/scenes.sh <rom> <out>
#
# The card is the repository's sdroot/, which the owner plays from: run it on
# a card of its own (a copy, swapped in), as the other emulator scripts are;
# or ICACHE_CARD=sc: the scratch card's configurations (build/emu/cfg_scds,
# cfg_scdsi), on whichever card they name.
# A phase-counting build (HLDS_ICACHE_PHASES, with HLDS_PROFILE, HLDS_DEV and
# HLDS_ITCM_OFF) writes its misses by part of the frame: rank them with
# tools/icache_rank.py --by-phase.
# Then, for a scene (its first snapshot after the map was up, s.0, to its
# last):
#
#   tools/icache_rank.py <its elf> <the current elf> <out>/<scene>/s.0 <out>/<scene>/s.<last> \
#       --families=<out>/<scene>/families.txt
#
# The emulator's JIT must be off (build/emu's configurations have it off), or
# the code it runs never passes the counters.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
ROM=${1:?usage: tools/icache/scenes.sh <rom> <out>}
OUT=${2:?usage: tools/icache/scenes.sh <rom> <out>}
export MELONDS="$PWD/build/tools/melonds-icache/build/melonDS"
[ -x "$MELONDS" ] || { echo "no profiling melonDS: tools/icache/setup.sh first" >&2; exit 1; }
CARD_PREFIX=${ICACHE_CARD:-}
FIGHT="key Up 3; hold z; sleep 1.5; release z; key Left 1; key Up 3; sleep 12; hold z; sleep 2; release z; key Right 2; key Up 3; sleep 12; hold z; sleep 2; release z; sleep 15"
while IFS='|' read -r tag mode map spot extra; do
    mkdir -p "$OUT/$tag"
    mode="$CARD_PREFIX$mode"
    CARD="$(sed -n 's/^FolderPath = "\(.*\)"$/\1/p' "build/emu/cfg_$mode/melonDS/melonDS.toml" 2>/dev/null | head -1)"
    D="${CARD:-$PWD/sdroot}/_nds/hl_nds/data"
    python3 -I tools/emu/setstart.py "$D/manifest.bin" "$map" >/dev/null || exit 1
    rm -f "$D/perf.txt"
    HLDS_ICACHE_OUT="$OUT/$tag/s" EMU_ROM="$ROM" EMU_START="$spot" timeout 500 tools/emu/play.sh "$mode" \
        "ready 150; sleep 1; $extra" > "$OUT/$tag/play.log" 2>&1
    # Where the game's families were loaded (the measuring build says so
    # in perf.txt), for the ranking to name their code: the emulator writes
    # the card back a while after it has quit.
    for _ in $(seq 60); do
        [ -s "$D/perf.txt" ] && break
        sleep 1
    done
    grep "^family: " "$D/perf.txt" > "$OUT/$tag/families.txt" 2>/dev/null
    cp "$D/perf.txt" "$OUT/$tag/perf.txt" 2>/dev/null
    echo "$tag: $(ls "$OUT/$tag" | grep -c '^s\.') snapshots, $(wc -l < "$OUT/$tag/families.txt") families"
done <<LIST
c1a3a-ds|ds|c1a3a|632 -2528 -60 31 -1 0 0 0 1 2|key Up 1.5; sleep 45
c3a1-dsi|dsi|c3a1|832 880 692 277 -1 0 0 0 1 2|$FIGHT
c1a0-ds|ds|c1a0|484 318 -168 180 -1 0 0 0 1 2|key Up 3; key Left 2; key Up 3; sleep 30
c2a5f-ds|ds|c2a5f|528 2672 -1562 166 -1 0 0 2 1 2|$FIGHT
LIST
