#!/usr/bin/env bash
# usage: diag.sh <scenario>         (tools/ref/diag/<name>.txt, or a path)
#
# Play a scenario in the REAL game with the diagnostic build of the SDK
# (github.com/CrowbarDS/hlsdk-portable-diag, see setup.sh's REF_DIAG) and keep
# what its server logged, frame by frame:
#   build/ref/diag/<name>_pc.csv    the game's log
#   build/ref/diag/<name>.bsp       the map exactly as the game had it (start
#                                   moved, entities stripped), for the port's
#                                   replay (diag_replay) to cook the same
#   build/ref/diag/<name>.nod       the node graph the game made of it
#
# The scenario's head says where:
#   # map c1a2
#   # start <x y z yaw>
#   # keep <pattern>       classes kept besides the world and the start
#                          (default func_.*: the brushes, not the monsters)
# and its lines are the inputs, one span of frames each (cl_dll/diag_input.cpp
# in the SDK fork says how they read).
#
# Then the port plays it (build/convert/diag_replay, the converter's build) into
# build/ref/diag/<name>_ds.csv, and tools/ref/diagcmp.py holds the two together.
# DIAG_PORT_ONLY=1: only that, against the game's log of the last run (for
# trying a change to the port).
# DIAG_VBLANKS=2: the port at the console's pace, the game's frames taken two
# at a time (diag_replay), compared on every second frame.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HL="${HL_DIR:-$HOME/.steam/debian-installation/steamapps/common/Half-Life}"
SCEN="${1:?scenario}"
[ -f "$SCEN" ] || SCEN="$ROOT/tools/ref/diag/$SCEN.txt"
[ -f "$SCEN" ] || { echo "no scenario $1" >&2; exit 1; }
SCEN="$(cd "$(dirname "$SCEN")" && pwd)/$(basename "$SCEN")"
NAME="$(basename "$SCEN" .txt)"
head_of() { sed -n "s/^# *$1 \+//p" "$SCEN" | head -1; }
MAP="$(head_of map)"
AT="$(head_of start)"
KEEP="$(head_of keep)"
[ -n "$MAP" ] && [ -n "$AT" ] || { echo "$SCEN: needs '# map' and '# start' lines" >&2; exit 1; }
# How long the script plays, in frames (60 a second at fps_max 60).
FRAMES=$(awk '!/^ *#/ && !/^cmd / && NF >= 5 { n += $1 } END { print n + 0 }' "$SCEN")

OUT="$ROOT/build/ref/diag"
mkdir -p "$OUT"
port() {
    local replay="$ROOT/build/convert/diag_replay"
    # (The monsters' models are cooked from the game's own folder.)
    export HLDS_VALVE_DIR="${HLDS_VALVE_DIR:-$HL/valve}"
    [ -x "$replay" ] || { echo "no $replay: build the converter (cmake --build build/convert --target diag_replay)" >&2; exit 1; }
    # (Each frame as long as the game's command was: its log.)
    "$replay" "$OUT/$NAME.bsp" "$SCEN" "$OUT/${NAME}_ds.csv" 120 "$OUT/${NAME}_pc.csv" || exit 1
    python3 -I "$ROOT/tools/ref/diagcmp.py" "$NAME"
}
if [ -n "${DIAG_PORT_ONLY:-}" ]; then
    [ -f "$OUT/${NAME}_pc.csv" ] && [ -f "$OUT/$NAME.bsp" ] || { echo "no game's log of $NAME yet: run without DIAG_PORT_ONLY" >&2; exit 1; }
    port
    exit 0
fi
LOG="$HL/hlds_ref/diag/$NAME.csv"
rm -f "$LOG" "$OUT/${NAME}_pc.csv"

export REF_DIAG=1 REF_BARE=1 REF_KEEP="${KEEP:-func_.*}"
# The same map copy for the port.
# shellcheck disable=SC2086
python3 "$ROOT/tools/ref/place.py" "$HL/valve/maps/$MAP.bsp" "$OUT/$NAME.bsp" $AT || exit 1
# One command sent for each frame (the game's default sends 30 a second,
# two frames' commands at a time); every frame the same step of time,
# DIAG_STEP seconds (1/60 by default), however slowly the game really runs
# without a graphics card; and F11 starts the script.
# (The log's name is given with the key: the server's cvar is not there yet
# when the config is read.)
export REF_CFG="diag_script $SCEN
cl_cmdrate 100
sv_aim 0
host_framerate ${DIAG_STEP:-0.0166667}
bind F11 \"diag_log $NAME; diag_go\""
export REF_UNTIL="DIAG: log closed"
"$ROOT/tools/ref/play.sh" "$MAP" "$AT" "key F11 0.1; sleep $(( FRAMES / 60 + 1 ))"
if ! grep -q '^END,' "$LOG" 2>/dev/null; then
    echo "the game's log is missing or unfinished: $LOG (see build/ref/diag_console.log)" >&2
    exit 1
fi
cp "$LOG" "$OUT/${NAME}_pc.csv"
# The node graph the game made of the copy (it makes one whenever the map is
# newer than its graph), for the port's monsters to find their ways by.
rm -f "$OUT/$NAME.nod"
[ -f "$HL/hlds_ref/maps/graphs/$MAP.nod" ] && cp "$HL/hlds_ref/maps/graphs/$MAP.nod" "$OUT/$NAME.nod"
echo "$OUT/${NAME}_pc.csv: $(grep -c '^pl,' "$OUT/${NAME}_pc.csv") commands"
port
