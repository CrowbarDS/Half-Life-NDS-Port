#!/usr/bin/env bash
# Take away the side folder tools/ref/setup.sh made.
set -euo pipefail
HL="${HL_DIR:-$HOME/.steam/debian-installation/steamapps/common/Half-Life}"
REF="$HL/hlds_ref"
[ "${REF_GAME:-valve}" = valve ] || REF="$HL/hlds_ref_$REF_GAME"
[ -f "$REF/liblist.gam" ] && grep -q "tools/ref/setup.sh" "$REF/liblist.gam" || { echo "nothing of ours at $REF" >&2; exit 0; }
rm -rf "${REF:?}"
echo "removed $REF"
