#!/usr/bin/env bash
# usage: tools/emu/room.sh <ds|dsi> <map>...
#        tools/emu/room.sh <ds|dsi> --walk "<map> <map>..."...
#
# How much of the heap each map leaves, in the emulator: each map started
# from a debug start (EMU_START), played a little, then left by the pause
# menu's QUIT TO MAIN MENU, so that the game writes both its perf.txt lines:
#   room: at the end of the load -- the heap's top and largest hole, the
#         map's sounds loaded of those kept, the release reserve
#   play: as the map is left -- how far play took the heap past what the
#         load left (guard::playRise), what is held, the reserve in play
# Each map's lines go to <out>/<map>.txt, and a row of each to
# <out>/table.txt. The emulator is not the console: these say what a DS's
# heap holds, not how fast its card is.
#
# The card is the repository's sdroot/, which its owner plays from: its
# saves, settings, achievements, perf.txt and manifest are copied to
# build/emu/cardbak/<time>-room/ first and put back at the end (copied
# back, then what the runs left removed: rsync), even if the sweep is stopped.
#
#   ROOM_SPOTS=<file>  lines "map:x y z yaw [...]" (EMU_START's numbers) of
#                      where to start; default build/logs/item4/campaign-specs.txt
#                      if there, else the map's info_player_start in the stage
#   ROOM_PLAY=<s>      seconds of play after the walk (default 40)
#   ROOM_WALK=<xin>    what is done first (default: a short walk about)
#   ROOM_EXIT=<xin>    how the map is left (default: START, a touch of
#                      QUIT TO MAIN MENU, and of YES)
#   ROOM_OUT=<dir>     default build/logs/room/<time>
#   ROOM_WAIT=<s>      the most to wait for melonDS to write the card back (90)
#   ROOM_STAGE=<dir>   the stage's maps folder (default sdroot-stage's): the
#                      maps' .ent for their start spots and .dsneed below
#
# At the end (tools/emu/room_margin.py): the least heap free before a map
# against kDsMapHeap -- what is left of its margin, and a warning under
# 16 KB -- and the single-player maps within 4 KB of the 128 KB floor for
# their sounds with that heap. This is the real measure (the build's check
# sees only the program's size): run it before every release and in every
# full DS sweep.
#
# --walk: each argument a walk of maps by their own ways between them
# (tools/emu/campaign_walk.py), in one run of the emulator: started at the
# first's spot, each map left ROOM_GO seconds after it is up (default 20) by
# its way into the next (debug_start.txt's "go" lines), as if walked into --
# the map kept, the arrival and its saves -- and the last left by the menu.
# (The start and the walk are a development build's: HLDS_DEV=1 ./make.sh.)
# A row for each map gone into, in order; a walk that stopped short (a halt,
# a map with no way to the next) says where. Each run's lines in
# <out>/walk-<n>.txt. ROOM_LOAD=<s>: what a load is given (default 30).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:?ds or dsi}"
shift
[ $# -gt 0 ] || { echo "usage: $0 <ds|dsi> <map>..." >&2; exit 2; }
cd "$ROOT"
D=sdroot/_nds/hl_nds/data
STAMP=$(date +%Y%m%d-%H%M)
OUT="${ROOM_OUT:-build/logs/room/$STAMP}"
BAK="build/emu/cardbak/$STAMP-room"
STAGE="${ROOM_STAGE:-sdroot-stage/_nds/hl_nds/data/maps}"
SPOTS="${ROOM_SPOTS:-build/logs/item4/campaign-specs.txt}"
PLAY="${ROOM_PLAY:-40}"
WALK="${ROOM_WALK:-key Left 4; sleep 3; key Right 2; sleep 3; key Up 3; sleep 3; key Left 2}"
# (START pauses; the menu's rows are touched, not chosen with the pad:
# QUIT TO MAIN MENU is its last row, YES the first of "QUIT TO MENU?".)
EXIT="${ROOM_EXIT:-key r 0.2; sleep 1.5; tap 128 425; sleep 1.5; tap 128 287}"
mkdir -p "$OUT" "$BAK"

# The owner's card, kept aside first.
for f in saves settings.dat achievements.dat perf.txt manifest.bin; do
    [ -e "$D/$f" ] && cp -a "$D/$f" "$BAK/"
done
echo "card backed up in $BAK"
restore()
{
    # (melonDS may still be writing the card back: give it time first.)
    sleep 5
    for f in settings.dat achievements.dat perf.txt manifest.bin; do
        if [ -e "$BAK/$f" ]; then cp -a "$BAK/$f" "$D/$f"; else rm -f "$D/$f"; fi
    done
    # (Copied back over what is there, then what the runs added taken away.)
    if [ -d "$BAK/saves" ]; then
        mkdir -p "$D/saves"
        rsync -a --delete "$BAK/saves/" "$D/saves/"
    fi
    rm -f "$D/debug_start.txt"
    echo "card restored from $BAK"
}
trap restore EXIT

# Where to start a map: the spots file, or its info_player_start.
spotOf()
{
    local map=$1
    if [ -f "$SPOTS" ] && grep -q "^$map:" "$SPOTS"; then
        grep "^$map:" "$SPOTS" | head -1 | cut -d: -f2
        return
    fi
    [ -f "$STAGE/$map.ent" ] || return
    python3 -I - "$STAGE/$map.ent" <<'EOF'
import re, sys
text = open(sys.argv[1], encoding="latin-1").read()
for block in re.findall(r"\{([^}]*)\}", text):
    keys = dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block))
    if keys.get("classname") == "info_player_start" and "origin" in keys:
        x, y, z = (int(round(float(v))) for v in keys["origin"].split())
        yaw = int(round(float(keys.get("angle", "0").split()[0])))
        print(f"{x} {y} {z + 36} {yaw} -1")
        break
EOF
}

printf "%-8s %9s %9s %9s %-22s %9s %9s %-28s %9s %9s\n" map "load top" "hole" "sounds" "reserve (load)" "play rise" "play top" "reserve (play)" "free pre" "map took" > "$OUT/table.txt"

if [ "${1:-}" = "--walk" ]; then
    shift
    n=0
    for walk in "$@"; do
        n=$((n + 1))
        read -r -a maps <<< "$walk"
        first=${maps[0]}
        last=${maps[${#maps[@]} - 1]}
        spot=$(spotOf "$first")
        if [ -z "$spot" ]; then
            echo "walk $n: no start spot for $first"
            continue
        fi
        go=""
        for map in "${maps[@]:1}"; do go="$go $map:${ROOM_GO:-20}"; done
        python3 -I tools/emu/setstart.py "$D/manifest.bin" "$first" >/dev/null || exit 1
        rm -f "$D/perf.txt"
        echo "walk $n: $first .. $last (${#maps[@]} maps), from $spot"
        total=$(( ${#maps[@]} * (${ROOM_GO:-20} + ${ROOM_LOAD:-30}) ))
        EMU_START="$spot" EMU_GO="$go" timeout $(( total + 600 )) tools/emu/play.sh "$MODE" \
            "ready 150; sleep $total; $EXIT; sleep 15" >/dev/null 2>&1
        for _ in $(seq "${ROOM_WAIT:-90}"); do
            grep -q "^play: $last " "$D/perf.txt" 2>/dev/null && break
            sleep 1
        done
        sleep 3
        grep -E "^(room|play|load|reopen|go): |^mount: " "$D/perf.txt" > "$OUT/walk-$n.txt" 2>/dev/null
        python3 -I tools/emu/room_rows.py "$OUT/walk-$n.txt" | tee -a "$OUT/table.txt"
        reached=$(grep -c "^room: " "$OUT/walk-$n.txt")
        if [ "$reached" -lt "${#maps[@]}" ]; then
            echo "  walk $n STOPPED: $reached of ${#maps[@]} maps loaded" | tee -a "$OUT/table.txt"
        fi
    done
    python3 -I tools/emu/room_margin.py "$OUT" "$STAGE" | tee -a "$OUT/table.txt"
    echo "table: $OUT/table.txt"
    exit 0
fi

for map in "$@"; do
    spot=$(spotOf "$map")
    if [ -z "$spot" ]; then
        echo "$map: no start spot (ROOM_SPOTS, or an info_player_start in the stage)"
        continue
    fi
    python3 -I tools/emu/setstart.py "$D/manifest.bin" "$map" >/dev/null || exit 1
    rm -f "$D/perf.txt"
    echo "$map: from $spot"
    EMU_START="$spot" timeout 900 tools/emu/play.sh "$MODE" \
        "ready 150; sleep 5; $WALK; sleep $PLAY; $EXIT; sleep 15" >/dev/null 2>&1
    # (The emulator writes the card back to sdroot/ after it has quit.)
    for _ in $(seq "${ROOM_WAIT:-90}"); do
        grep -q "^play: $map " "$D/perf.txt" 2>/dev/null && break
        sleep 1
    done
    sleep 3
    grep -E "^(room|play|load|reopen): $map |^mount: " "$D/perf.txt" > "$OUT/$map.txt" 2>/dev/null
    python3 -I tools/emu/room_rows.py "$OUT/$map.txt" >> "$OUT/table.txt"
    tail -1 "$OUT/table.txt"
done
python3 -I tools/emu/room_margin.py "$OUT" "$STAGE" | tee -a "$OUT/table.txt"
echo "table: $OUT/table.txt"
