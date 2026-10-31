#!/usr/bin/env bash
# usage: play.sh <ds|dsi> "<script>"     (see README.md)
# EMU_START="x y z yaw" starts the player there (whole units, degrees), armed;
# a fifth number starts that track of the music (-1: none); a sixth is the
# seconds until the player dies, once; a seventh how many
# models may be drawn at once (up to 12); an eighth a weapon, a ninth god
# mode; a tenth how hard the game is (1 easy, 2 medium, 3 difficult).
# EMU_FADE="r g b amount duration-ms hold-ms from after-ms" (with EMU_START)
# puts a fade on the screen as an env_fade would, after-ms into the game
# (from: 1 a fade from the colour, 0 into it).
# EMU_AUDIO=<file> captures the sound (see below).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:?ds or dsi}"
CFG="$ROOT/build/emu/cfg_$MODE"
[ -d "$CFG" ] || "$ROOT/tools/emu/setup.sh" >/dev/null
cd "$ROOT"
# Saves left by an earlier run are not this run's: EMU_KEEP_SAVE=1 keeps
# them (to test exactly that -- the title's CONTINUE and LOAD GAME).
if [ -z "${EMU_KEEP_SAVE:-}" ]; then
    for save in auto quick save1 save2 save3 save4 save5 save6 arrival; do
        rm -f "${ROOT:?}/sdroot/_nds/hl_nds/data/saves/$save.sav" "${ROOT:?}/sdroot/_nds/hl_nds/data/saves/$save.sav.new"
    done
    rm -f "${ROOT:?}/sdroot/_nds/hl_nds/data/saves/index.dat" "${ROOT:?}/sdroot/_nds/hl_nds/data/saves/slot0.sav" \
        "${ROOT:?}/sdroot/_nds/hl_nds/data/saves/slot1.sav"
fi
START_FILE="$ROOT/sdroot/_nds/hl_nds/data/debug_start.txt"
[ -n "${EMU_START:-}" ] && echo "$EMU_START" > "$START_FILE"
[ -n "${EMU_START:-}" ] && [ -n "${EMU_FADE:-}" ] && echo "fade $EMU_FADE" >> "$START_FILE"
trap 'rm -f "$START_FILE"' EXIT
# EMU_AUDIO=<file> writes what the emulator would have played to a file
# instead of to the sound card (raw samples: SDL's "disk" driver), so that
# sound can be looked at without being heard.
if [ -n "${EMU_AUDIO:-}" ]; then
    export SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$EMU_AUDIO"
    rm -f "$EMU_AUDIO"
fi
SCRIPT="${2:-sleep 5}"
# EMU_INSTANCES=2: two consoles linked by local wireless, side by side --
# the emulator's second instance (System > Multiplayer > Launch new
# instance), started on the same ROM from its recent list. The first is at
# the left (its touch screen from 0,222), the second at x 320 (from 320,222);
# "focus 0" or "focus 1" says which one the keys go to.
# (One console: its window is put where the coordinates in README.md are
# measured from -- Qt does not always open it there.)
if [ "${EMU_INSTANCES:-1}" = 1 ]; then
    SCRIPT="sleep 1.5; place 0 0 25; $SCRIPT"
fi
# EMU_PICK2: what the second instance is started on, as a touch of the
# recent list's menu (default its first entry, the ROM the first was started
# on); EMU_ROM2=child picks the Download Play child (the list's second entry
# in build/emu's configs) -- a console sent the game, as it would be if it
# had been, but started without Download Play (which needs a bootable DS
# firmware the emulator here has not got).
# EMU_INSTANCES=3: a third, at x 640 (its touch screen from 640,222): the same
# ROM, or EMU_ROM3=child, the Download Play child.
if [ "${EMU_INSTANCES:-1}" = 3 ]; then
    EMU_SCREEN="${EMU_SCREEN:-960x480}"
    PICK2="${EMU_PICK2:-tap 600 47}"
    [ "${EMU_ROM2:-}" = child ] && PICK2="${EMU_PICK2:-tap 600 75}"
    PICK3="${EMU_PICK3:-tap 520 47}"
    [ "${EMU_ROM3:-}" = child ] && PICK3="${EMU_PICK3:-tap 520 75}"
    SCRIPT="sleep 6; place 0 0 0; sleep 0.5; tap 75 15; sleep 0.8; tap 120 387; sleep 0.8; tap 300 387; sleep 3; place 1 320 0; sleep 1; tap 340 15; sleep 1; tap 420 47; sleep 1; $PICK2; sleep 8; tap 75 15; sleep 0.8; tap 120 387; sleep 0.8; tap 300 387; sleep 3; place 2 640 0; sleep 1; tap 660 15; sleep 1; tap 740 47; sleep 1; ${EMU_SHOT3:-} $PICK3; sleep 12; $SCRIPT"
fi
if [ "${EMU_INSTANCES:-1}" = 2 ]; then
    EMU_SCREEN="${EMU_SCREEN:-640x480}"
    PICK2="${EMU_PICK2:-tap 200 47}"
    [ "${EMU_ROM2:-}" = child ] && PICK2="${EMU_PICK2:-tap 200 75}"
    SCRIPT="sleep 6; place 0 0 0; sleep 0.5; tap 75 15; sleep 0.8; tap 120 387; sleep 0.8; tap 300 387; sleep 3; place 1 320 0; sleep 1; tap 340 15; sleep 1; tap 420 47; sleep 1; $PICK2; sleep 12; $SCRIPT"
fi
# (The wait: the emulator writes the card back to sdroot/ as it quits --
# the files the ROM wrote, perf.txt -- and X must not go before it has.
# Thirty seconds at most: a DSi's card takes longer than ten.)
# (EMU_ROM: another ROM than the game's -- child/out/hl-child-twl.nds, the
# Download Play child.)
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb XDG_CONFIG_HOME="$CFG" EMU_SCRIPT="$SCRIPT" \
    EMU_ROM="${EMU_ROM:-half-life-nds-port.nds}" \
    xvfb-run -a -s "-screen 0 ${EMU_SCREEN:-300x450}x24" bash -c \
    '"${MELONDS:-melonDS}" "$EMU_ROM" >"'"$CFG"'/melonDS.log" 2>&1 &
     python3 tools/emu/xin.py "$EMU_SCRIPT"; kill %1; for i in $(seq 120); do kill -0 %1 2>/dev/null || break; sleep 0.25; done; sleep 2' 2>/dev/null || true
