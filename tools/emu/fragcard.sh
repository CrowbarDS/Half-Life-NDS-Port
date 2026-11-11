#!/usr/bin/env bash
# usage: tools/emu/fragcard.sh <ds|dsi> <npac_frag.nds> [keep [<hl_cardread_probe.nds>]]
#
# Runs NEA's fragmented-archive test (nitro-engine tests/npac_frag) in melonDS
# on a card image of its own: a fresh FAT32 image (4 KB clusters) in
# build/emu/frag/, with _nds/hl_nds/FRAGTEST_OK on it (and FRAGTEST_KEEP with
# "keep", for the card-read probe to time the archives after). The emulator
# is given that image directly (folder sync off): the repository's sdroot/,
# which its owner plays from, is not touched. The test's results are read
# back out of the image: build/emu/frag/npac_frag-<mode>.txt. With "keep" and
# the card-read probe (tools/hwtest/cardread) after it, the probe is run next
# on the same card and times the archives the test left: what a seek costs
# with no fast-seek table, in build/emu/frag/cardread-<mode>.txt.
#
# The image is made again at each run, so every run starts on an empty card.
# (melonDS is not the console: the times it gives are not a card's.)
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:?ds or dsi}"
ROM="$(realpath "${2:?the test ROM}")"
KEEP="${3:-}"
PROBE="${4:+$(realpath "$4")}"
cd "$ROOT"
[ -d "build/emu/cfg_$MODE" ] || tools/emu/setup.sh >/dev/null
FRAG="$ROOT/build/emu/frag"
CFG="$ROOT/build/emu/cfg_frag$MODE"
IMG="$FRAG/card-$MODE.img"
mkdir -p "$FRAG"

# A private config, the mode's own with its card made this image.
rm -rf "$CFG"
mkdir -p "$CFG"
cp -a "build/emu/cfg_$MODE/melonDS" "$CFG/"
python3 -I - "$CFG/melonDS/melonDS.toml" "$IMG" <<'EOF'
import re, sys
path, image = sys.argv[1], sys.argv[2]
text = open(path).read()
for section in ("DLDI", "DSi.SD"):
    block = re.search(r"(?ms)^\[" + re.escape(section) + r"\]\n(.*?)(?=^\[|\Z)", text)
    if not block:
        continue
    body = block.group(1)
    body = re.sub(r'(?m)^ImagePath = .*$', 'ImagePath = "%s"' % image, body)
    body = re.sub(r'(?m)^FolderSync = .*$', 'FolderSync = false', body)
    body = re.sub(r'(?m)^ReadOnly = .*$', 'ReadOnly = false', body)
    text = text[:block.start(1)] + body + text[block.end(1):]
open(path, "w").write(text)
EOF
grep -q "FolderSync = true" "$CFG/melonDS/melonDS.toml" && { echo "folder sync still on: stopping" >&2; exit 1; }

# The card: 512 MB, FAT32, clusters of 4 KB.
rm -f "$IMG"
mkfs.fat -C -F 32 -S 512 -s 8 -n FRAGTEST "$IMG" $((512 * 1024)) >/dev/null || exit 1
export MTOOLS_SKIP_CHECK=1
mmd -i "$IMG" ::/_nds ::/_nds/hl_nds
: > "$FRAG/empty"
mcopy -i "$IMG" "$FRAG/empty" ::/_nds/hl_nds/FRAGTEST_OK
[ "$KEEP" = keep ] && mcopy -i "$IMG" "$FRAG/empty" ::/_nds/hl_nds/FRAGTEST_KEEP

# (EMU_KEEP_SAVE: play.sh would otherwise clear sdroot/'s saves, which are
# not this card's.)
EMU_KEEP_SAVE=1 EMU_ROM="$ROM" timeout 900 tools/emu/play.sh "frag$MODE" \
    "sleep ${FRAG_WAIT:-90}; shot $FRAG/npac_frag-$MODE.png" >/dev/null 2>&1
rm -f "$FRAG/npac_frag-$MODE.txt"
mcopy -i "$IMG" ::/_nds/hl_nds/npac_frag.txt "$FRAG/npac_frag-$MODE.txt" 2>/dev/null &&
    cat "$FRAG/npac_frag-$MODE.txt" || echo "no results on the card (see $FRAG/npac_frag-$MODE.png)"

# The card-read probe on the same card, timing what the test left.
if [ "$KEEP" = keep ] && [ -n "$PROBE" ]; then
    EMU_KEEP_SAVE=1 EMU_ROM="$PROBE" timeout 900 tools/emu/play.sh "frag$MODE" \
        "sleep ${PROBE_WAIT:-120}; shot $FRAG/cardread-$MODE.png" >/dev/null 2>&1
    rm -f "$FRAG/cardread-$MODE.txt"
    mcopy -i "$IMG" ::/_nds/hl_nds/cardread.txt "$FRAG/cardread-$MODE.txt" 2>/dev/null &&
        cat "$FRAG/cardread-$MODE.txt" || echo "no probe results on the card (see $FRAG/cardread-$MODE.png)"
fi
