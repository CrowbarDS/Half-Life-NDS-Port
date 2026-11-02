#!/usr/bin/env bash
# usage: play.sh <map> "<x y z yaw>" "<script>"        (see README.md)
#
# Run the REAL Half-Life -- the player's own copy, out of the side folder
# tools/ref/setup.sh makes -- without a screen, with the player put at a
# place and the game driven by a script, to have its pictures to hold the
# port's against.
#
# The script is tools/emu/xin.py's, the same that drives the port in the
# emulator: `sleep 1.5`, `key a 0.1` (a: attack, as L is on the console;
# Up/Down/Left/Right walk and turn), `shot <png>`, `burst <dir> <n>`. The
# game's picture is 640x480 in the middle of an 800x600 screen: REF_CROP
# says where, for whoever cuts it out.
#
# The player starts with every weapon and the crowbar in hand.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HL="${HL_DIR:-$HOME/.steam/debian-installation/steamapps/common/Half-Life}"
REF="$HL/hlds_ref"
MAP="${1:?map}"
AT="${2:?x y z yaw}"
SCRIPT="${3:-sleep 2}"
# The game asks Steam who is playing, and stops if nobody answers.
pgrep -x steam >/dev/null || { echo "Steam is not running: the real game cannot start without it" >&2; exit 3; }
[ -f "$REF/liblist.gam" ] || "$ROOT/tools/ref/setup.sh" >/dev/null
RUNTIME="$HOME/.steam/debian-installation/ubuntu12_32/steam-runtime/run.sh"
LOG="$HL/qconsole.log"

# The map, with the player's start moved (place.py), in the side folder:
# found there before the game's own.
# shellcheck disable=SC2086
python3 "$ROOT/tools/ref/place.py" "$HL/valve/maps/$MAP.bsp" "$REF/maps/$MAP.bsp" $AT || exit 1

# The keys, in the side folder's own config (which the game reads whenever
# it likes, and writes back when it stops: it is ours, not the player's).
cat > "$REF/config.cfg" <<'CFG'
// (Made by tools/ref/play.sh.)
unbindall
bind a "+attack"
bind UPARROW "+forward"
bind DOWNARROW "+back"
bind LEFTARROW "+left"
bind RIGHTARROW "+right"
bind F5 "impulse 101"
bind F6 "weapon_crowbar"
bind F7 "weapon_9mmhandgun"
bind F8 "god"
bind F9 "weapon_shotgun"
bind F10 "impulse 100"
bind e "impulse 100"
bind z "+attack2"
bind t "+reload"
bind 4 "weapon_handgrenade"
bind 5 "weapon_9mmAR"
bind 6 "weapon_357"
bind 7 "weapon_crossbow"
bind 8 "weapon_satchel"
bind 9 "weapon_tripmine"
bind 0 "weapon_snark"
bind 1 "weapon_rpg"
bind 2 "weapon_gauss"
bind 3 "weapon_egon"
bind h "weapon_hornetgun"
bind u "+use"
bind SPACE "+jump"
bind SHIFT "+speed"
bind CTRL "+duck"
bind m "give monster_zombie"
crosshair 1
cl_bob 0
fps_max 60
CFG
# How bright the game is drawn is the player's own choice, in their own
# config (read, never written): the reference is the game as THEY see it.
for cvar in brightness gamma gl_overbright texgamma lightgamma; do
    grep -i "^$cvar " "$HL/valve/config.cfg" >> "$REF/config.cfg" 2>/dev/null
done
cat > "$REF/ref.cfg" <<'CFG'
// (Made by tools/ref/play.sh.)
sv_cheats 1
echo REF-START
CFG

# REF_AUDIO=<file>: the game's sound, written to that file (SDL's own disk
# driver, as tools/emu/play.sh's EMU_AUDIO): nothing is played, and nothing of
# the computer's sound is touched.
SOUND=-nosound
if [ -n "${REF_AUDIO:-}" ]; then
    SOUND=
    export SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$REF_AUDIO"
    rm -f "$REF_AUDIO"
fi
rm -f "$LOG"
DISP=":$((70 + RANDOM % 20))"
Xvfb "$DISP" -screen 0 800x600x24 >/dev/null 2>&1 &
XVFB=$!
# (The game is started through two scripts of Steam's: it is a group of its
# own, and the whole group is ended -- asked first, then made to -- or the
# game itself outlives its starter.)
GAME=
finish() {
    if [ -n "$GAME" ]; then
        kill -TERM -- "-$GAME" 2>/dev/null
        sleep 1
        kill -KILL -- "-$GAME" 2>/dev/null
    fi
    kill "$XVFB" 2>/dev/null
    rm -f "$REF/maps/$MAP.bsp" "$LOG"
}
trap finish EXIT
trap 'exit 1' INT TERM
sleep 1
cd "$HL" || exit 1
DISPLAY="$DISP" LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 SteamAppId=70 SteamGameId=70 \
    setsid "$RUNTIME" ./hl.sh -game hlds_ref -windowed -w 640 -h 480 -novid $SOUND -nomouse -condebug \
    +sv_cheats 1 +map "$MAP" +exec ref.cfg >"$REF/run.log" 2>&1 &
GAME=$!
cd "$ROOT" || exit 1
export DISPLAY="$DISP"
# The game says, before anything else, that it has not the locale it would
# like: the notice is clicked away as soon as it is up. Then the map loads
# (the log says when), and the game is given a moment to settle.
for _ in $(seq 1 60); do
    sleep 0.5
    python3 tools/emu/xin.py "tap 478 338" >/dev/null 2>&1
    grep -q "REF-START" "$LOG" 2>/dev/null && break
done
grep -q "REF-START" "$LOG" 2>/dev/null || { echo "the game did not come up (see $REF/run.log)" >&2; exit 1; }
# (The map is in when the game has looked for its own config, which it does
# last; a big one takes its time without a graphics card.)
for _ in $(seq 1 80); do
    grep -q "${MAP}_load.cfg" "$LOG" 2>/dev/null && break
    sleep 0.5
done
sleep "${REF_SETTLE:-6}"
# Everything to hand, the crowbar in it, and nothing can hurt.
python3 tools/emu/xin.py "key F8 0.1; sleep 0.2; key F5 0.1; sleep 0.6; key F6 0.1; sleep 1.5; $SCRIPT"
