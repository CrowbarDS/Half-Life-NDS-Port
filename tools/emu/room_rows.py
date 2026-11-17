#!/usr/bin/env python3
"""Rows of tools/emu/room.sh's table from a run's perf.txt lines.

usage: room_rows.py <lines file>

A row for each "room:" line (a map loaded), in order, with the "play:" line
that follows it for the same map (the map left), and the models refused and
the animation keys read from the card (KeyCache) at the end. "go:" lines (a
walk that found no way on) are printed as they are.
"""
import re
import sys


def num(pattern, text):
    m = re.search(pattern, text)
    return m.group(1) if m else "-"


def kb(v):
    return v if v == "-" else f"{int(v) / 1024:.1f}K"


def map_kb(text):
    before, now = num(r"before the map live (\d+)", text), num(r"now live (\d+)", text)
    return "-" if "-" in (before, now) else f"{(int(now) - int(before)) / 1024:.1f}K"


def main(path):
    lines = open(path).read().splitlines()
    for i, room in enumerate(lines):
        if room.startswith("go: "):
            print("  " + room)
            continue
        if not room.startswith("room: "):
            continue
        name = room.split()[1]
        play = next((l for l in lines[i + 1:] if l.startswith("play: " + name + " ")), "")
        sounds = re.search(r"sounds (?:NONE \(no room for their list\), )?(\d+) of (\d+)", room)
        keys = (f"missing {num(r'MISSING (-?\d+)', room)}; keys {num(r'keys apart (\d+) models', play)} models "
                f"{kb(num(r'keys kept (\d+)', room))}, {num(r'clips read (\d+)', play)} reads "
                f"({num(r'clips read \d+ \((\d+) KB', play)} KB), {num(r'KB\), (\d+) failed', play)} failed")
        print(f"{name:<8} {kb(num(r'top (\d+)', room)):>9} {kb(num(r'largest hole (\d+)', room)):>9} "
              f"{(sounds.group(1) + '/' + sounds.group(2)) if sounds else '-':>9} {num(r'reserve ([^;]*)', room):<22} "
              f"{kb(num(r'rise (\d+)', play)):>9} {kb(num(r'top (\d+)', play)):>9} {num(r'reserve ([^;]*)', play):<28} "
              f"{kb(num(r'before the map live \d+, free (\d+)', room)):>9} {map_kb(room):>9} {keys}")


if __name__ == "__main__":
    main(sys.argv[1])
