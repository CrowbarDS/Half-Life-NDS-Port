#!/usr/bin/env python3
"""Where the ARM9 spends its time, by function, from perf.txt's "pcs:" lines.

usage: pcsample.py <perf.txt> [<map name>] [--last N]

A profiling build (HLDS_PROFILE=1) samples where the ARM9 is, several
thousand times a second (source/DebugLog.cpp), and writes the busiest
256-byte pieces of code with each "time:" line: "itcm+<offset>" (ITCM),
"prog+<offset>" (the program, from __text_start) and "dsl+<offset>" (the
game library, linked at 0). Here they are named from build/arm9/arm9.elf and
build/hl/hl.elf -- the ELFs of the build that wrote them -- and summed by
function. two functions is named after the first and the last it holds.
"""
import bisect
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NM = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm'


def symbols(elf):
    """Sorted (address, name) of the functions in an ELF."""
    out = subprocess.run([NM, '-C', '-n', '--defined-only', elf], capture_output=True, text=True).stdout
    found = []
    for line in out.splitlines():
        part = line.split(' ', 2)
        if len(part) == 3 and part[1] in 'TtWw':
            found.append((int(part[0], 16) & ~1, part[2]))
    found.sort()
    return found


def name_at(table, address):
    keys = [a for a, _ in table]
    i = bisect.bisect_right(keys, address) - 1
    return table[i][1] if i >= 0 else '?'


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        sys.exit(__doc__)
    last = None
    if '--last' in sys.argv:
        last = int(sys.argv[sys.argv.index('--last') + 1])
        args = [a for a in args if a != str(last)]
    perf, map_name = args[0], (args[1] if len(args) > 1 else None)
    prog = symbols(os.path.join(ROOT, 'build', 'arm9', 'arm9.elf'))
    dsl = symbols(os.path.join(ROOT, 'build', 'hl', 'hl.elf'))
    text_start = next(a for a, n in prog if n == '__text_start') if any(n == '__text_start' for _, n in prog) else 0x02000800
    lines = open(perf).read().splitlines()
    chosen = []
    for i, line in enumerate(lines):
        if line.startswith('pcs:') and i > 0 and lines[i - 1].startswith('time:'):
            if map_name is None or lines[i - 1].split()[1] == map_name:
                chosen.append(line)
    if last is not None:
        chosen = chosen[-last:]
    by_function = collections.Counter()
    where = {}
    total = outside = idle = 0
    for line in chosen:
        m = re.match(r'pcs: (\d+) samples, (\d+) outside(?:, (\d+) idle)?', line)
        total += int(m.group(1))
        outside += int(m.group(2))
        idle += int(m.group(3) or 0)
        for kind, offset, count in re.findall(r'(itcm|prog|dsl)\+([0-9a-f]+) (\d+)', line):
            start = int(offset, 16)
            table, base = (prog, 0x01000000) if kind == 'itcm' else (prog, text_start) if kind == 'prog' else (dsl, 0)
            first, last_name = name_at(table, base + start), name_at(table, base + start + 255)
            # (A piece holding more than one function says so: the time may
            # be either's.)
            name = first if first == last_name else '%s .. %s' % (first[:50], last_name[:50])
            by_function[name] += int(count)
            where[name] = kind
    if total == 0:
        sys.exit('pcsample: no "pcs:" lines%s' % (' for ' + map_name if map_name else ''))
    shown = sum(by_function.values())
    print('%d samples in %d lines; %.1f%% idle (asleep), %.1f%% outside the code (the BIOS); '
          'the busiest pieces cover %.1f%%'
          % (total, len(chosen), 100.0 * idle / total, 100.0 * outside / total, 100.0 * shown / total))
    for name, count in by_function.most_common(40):
        print('%6.2f%%  %-4s  %s' % (100.0 * count / total, where[name], name[:110]))


if __name__ == '__main__':
    main()
