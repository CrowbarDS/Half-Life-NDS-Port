#!/usr/bin/env python3
"""What tools/emu/room.sh's run says of kDsMapHeap (shared/HlDsRoom.hpp).

usage: room_margin.py <run folder> <stage maps folder>

The heap free as a DS began to load each map of the run ("room:" lines'
"before the map live N, free N"), the least of them, against kDsMapHeap:
what is left of the margin it keeps (32 KB when it was measured). Under
16 KB, it says so: measure again and set it there.

And with that heap -- less the same margin, as kDsMapHeap is -- each
single-player map's room for its sounds, from its need before them (the
stage's <map>.dsneed, which the converter wrote): the maps within 4 KB of
the 128 KB the converter keeps for them, the least first. (A DS's real
heap, not the converter's figure for it: the build's own check,
tools/arm9-report.sh --check, sees only the program's size.)
"""
import glob
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
FLOOR = 128 * 1024
NEAR = 4 * 1024
WARN = 16 * 1024


def main(run, stage):
    free = []
    for path in glob.glob(os.path.join(run, "*.txt")):
        for line in open(path, errors="replace"):
            m = re.search(r"^room: \S+ .*before the map live \d+, free (\d+)", line)
            if m:
                free.append(int(m.group(1)))
    room = open(os.path.join(ROOT, "shared", "HlDsRoom.hpp")).read()
    heap = re.search(r"kDsMapHeap = (\d+) - (\d+) \* 1024;", room)
    if not free or not heap:
        print("heap margin: no map's heap measured" if not free else "heap margin: kDsMapHeap not found")
        return 0
    measured, margin = int(heap.group(1)), int(heap.group(2)) * 1024
    least = min(free)
    left = least - (measured - margin)
    print("heap free before a map: %d (the least of %d); kDsMapHeap %d (measured %d): %d KB of its margin left%s"
          % (least, len(free), measured - margin, measured, left // 1024,
             "" if left >= WARN else " -- UNDER 16 KB: set kDsMapHeap from this measure (shared/HlDsRoom.hpp)"))
    near = []
    for path in sorted(glob.glob(os.path.join(stage, "*.dsneed"))):
        name = os.path.basename(path)[:-len(".dsneed")]
        if not re.match(r"^(c\d|t0a0)", name):
            continue  # (a match's map: the floor is the campaign's)
        need = struct.unpack("<I", open(path, "rb").read(4))[0]
        sounds = least - margin - need
        if sounds < FLOOR + NEAR:
            near.append((sounds, name))
    if near:
        print("within 4 KB of the 128 KB floor for sounds (with this heap): " +
              ", ".join("%s %.1f KB%s" % (name, sounds / 1024, " UNDER" if sounds < FLOOR else "")
                        for sounds, name in sorted(near)))
    else:
        print("no single-player map within 4 KB of the 128 KB floor for sounds (with this heap)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
