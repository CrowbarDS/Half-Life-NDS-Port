#!/usr/bin/env bash
# Make the side folder the real game is run from when it is used as a
# reference: <Half-Life>/hlds_ref, a "mod" with nothing of its own, which
# falls back to the game's own folder (valve) for everything. Whatever the
# game writes when it is run by these tools -- its config, its screenshots,
# its saves -- goes there, and the player's own valve folder is not touched.
# tools/ref/remove.sh takes it away again.
set -euo pipefail
HL="${HL_DIR:-$HOME/.steam/debian-installation/steamapps/common/Half-Life}"
[ -f "$HL/hl_linux" ] || { echo "no Half-Life at $HL (set HL_DIR)" >&2; exit 1; }
REF="$HL/hlds_ref"
mkdir -p "$REF/maps"
# REF_DIAG=1: the game's code is the diagnostic build of the SDK
# (github.com/CrowbarDS/hlsdk-portable-diag, built 32-bit), copied in from
# HLDS_DIAG_BUILD, which logs what tools/ref/diag.sh compares. Otherwise the
# game's own, and any copy left from a diagnostic run is taken away (the
# engine would load a client found here before the game's own).
DLL="../valve/dlls/hl.so"
if [ -n "${REF_DIAG:-}" ]; then
    BUILD="${HLDS_DIAG_BUILD:-$(cd "$(dirname "$0")/../.." && pwd)/../hlsdk-portable-diag/build}"
    [ -f "$BUILD/dlls/hl.so" ] && [ -f "$BUILD/cl_dll/client.so" ] \
        || { echo "no diagnostic build at $BUILD (set HLDS_DIAG_BUILD)" >&2; exit 1; }
    mkdir -p "$REF/dlls" "$REF/cl_dlls" "$REF/diag"
    cp "$BUILD/dlls/hl.so" "$REF/dlls/hl.so"
    cp "$BUILD/cl_dll/client.so" "$REF/cl_dlls/client.so"
    DLL="dlls/hl.so"
else
    rm -rf "$REF/dlls" "$REF/cl_dlls"
fi
cat > "$REF/liblist.gam" <<GAM
// Made by the Half-Life NDS port's tools/ref/setup.sh: the real game, run
// as a reference, with a folder of its own to write in. Safe to delete.
game "HLDS reference"
gamedll "..\\valve\\dlls\\hl.dll"
gamedll_linux "$DLL"
type "singleplayer_only"
fallback_dir "valve"
GAM
echo "$REF"
