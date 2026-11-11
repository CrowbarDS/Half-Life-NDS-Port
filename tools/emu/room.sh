#!/usr/bin/env bash
# usage: tools/emu/room.sh <ds|dsi> <map>...
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
STAGE=sdroot-stage/_nds/hl_nds/data/maps
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

printf "%-8s %9s %9s %9s %-22s %9s %9s %s\n" map "load top" "hole" "sounds" "reserve (load)" "play rise" "play top" "reserve (play)" > "$OUT/table.txt"
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
    python3 -I - "$OUT/$map.txt" "$map" >> "$OUT/table.txt" <<'EOF'
import re, sys
lines = open(sys.argv[1]).read().splitlines()
room = next((l for l in lines if l.startswith("room:")), "")
play = next((l for l in lines if l.startswith("play:")), "")
def num(pattern, text):
    m = re.search(pattern, text)
    return m.group(1) if m else "-"
kb = lambda v: v if v == "-" else f"{int(v) / 1024:.1f}K"
sounds = re.search(r"sounds (?:NONE \(no room for their list\), )?(\d+) of (\d+)", room)
print(f"{sys.argv[2]:<8} {kb(num(r'top (\d+)', room)):>9} {kb(num(r'largest hole (\d+)', room)):>9} "
      f"{(sounds.group(1) + '/' + sounds.group(2)) if sounds else '-':>9} {num(r'reserve (.*)$', room):<22} "
      f"{kb(num(r'rise (\d+)', play)):>9} {kb(num(r'top (\d+)', play)):>9} {num(r'reserve (.*)$', play)}")
EOF
    tail -1 "$OUT/table.txt"
done
echo "table: $OUT/table.txt"
