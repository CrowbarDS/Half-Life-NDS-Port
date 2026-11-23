#!/usr/bin/env bash
# Make the side folder the real game is run from when it is used as a
# reference: <Half-Life>/hlds_ref, a "mod" with nothing of its own, which
# falls back to the game's own folder (valve) for everything. Whatever the
# game writes when it is run by these tools -- its config, its screenshots,
# its saves -- goes there, and the player's own valve folder is not touched.
# tools/ref/remove.sh takes it away again.
#
# REF_GAME=bshift (or another game folder beside valve): that game instead,
# from a side folder of its own, <Half-Life>/hlds_ref_<game>, which falls
# back to the game's folder (and the engine to valve under it).
set -euo pipefail
HL="${HL_DIR:-$HOME/.steam/debian-installation/steamapps/common/Half-Life}"
[ -f "$HL/hl_linux" ] || { echo "no Half-Life at $HL (set HL_DIR)" >&2; exit 1; }
GAME="${REF_GAME:-valve}"
[ -f "$HL/$GAME/liblist.gam" ] || { echo "no game $GAME at $HL" >&2; exit 1; }
REF="$HL/hlds_ref"
[ "$GAME" = valve ] || REF="$HL/hlds_ref_$GAME"
mkdir -p "$REF/maps"
# REF_DIAG=1: the game's code is the diagnostic build of the SDK
# (github.com/CrowbarDS/hlsdk-portable-diag, built 32-bit), copied in from
# HLDS_DIAG_BUILD, which logs what tools/ref/diag.sh compares. Otherwise the
# game's own, and any copy left from a diagnostic run is taken away (the
# engine would load a client found here before the game's own).
# (The game's own library: its liblist.gam says which.)
LIB="$(sed -n 's/^gamedll_linux *"\([^"]*\)".*/\1/p' "$HL/$GAME/liblist.gam" | head -1)"
LIB="$(basename "${LIB:-dlls/hl.so}")"
DLL="../$GAME/dlls/$LIB"
if [ -n "${REF_DIAG:-}" ]; then
    # (Blue Shift's: the fork's bshift-diag branch, in a worktree beside it.)
    SIDE=hlsdk-portable-diag
    [ "$GAME" = valve ] || SIDE="hlsdk-portable-diag-$GAME"
    BUILD="${HLDS_DIAG_BUILD:-$(cd "$(dirname "$0")/../.." && pwd)/../$SIDE/build}"
    [ -f "$BUILD/dlls/$LIB" ] && [ -f "$BUILD/cl_dll/client.so" ] \
        || { echo "no diagnostic build at $BUILD (set HLDS_DIAG_BUILD)" >&2; exit 1; }
    mkdir -p "$REF/dlls" "$REF/cl_dlls" "$REF/diag"
    cp "$BUILD/dlls/$LIB" "$REF/dlls/$LIB"
    cp "$BUILD/cl_dll/client.so" "$REF/cl_dlls/client.so"
    DLL="dlls/$LIB"
else
    rm -rf "$REF/dlls" "$REF/cl_dlls"
fi
cat > "$REF/liblist.gam" <<GAM
// Made by the Half-Life NDS port's tools/ref/setup.sh: the real game, run
// as a reference, with a folder of its own to write in. Safe to delete.
game "HLDS reference"
gamedll "..\\$GAME\\dlls\\hl.dll"
gamedll_linux "$DLL"
type "singleplayer_only"
fallback_dir "$GAME"
GAM
echo "$REF"
