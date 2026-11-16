#!/usr/bin/env python3
"""A heap trace (HLDS_HEAP_TRACE=1 ./make.sh; source/Guard.hpp), with its
callers named.

  heaptrace.py [heaptrace.txt] [arm9.elf]

Reads what the ROM wrote to data/heaptrace.txt -- at each map's load, the
blocks still held from before it, by generation and caller; the load's peak --
and prints it again with each caller's function and line (addr2line, against
the ELF of the build that wrote it: build/elf/<name>/arm9.elf if it is not
the last). A caller outside the program is a family's (its library is on the
heap) or the allocator's own.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(os.environ.get('WONDERFUL_TOOLCHAIN', '/opt/wonderful'), 'toolchain', 'gcc-arm-none-eabi', 'bin')


def names(elf, addresses):
    """{address: 'function (file:line)'}"""
    if not addresses:
        return {}
    listed = sorted(addresses)
    out = subprocess.run([os.path.join(TOOLS, 'arm-none-eabi-addr2line'), '-f', '-C', '-s', '-e', elf] +
                         ['%x' % (a & ~1) for a in listed], capture_output=True, text=True).stdout.splitlines()
    found = {}
    for i, a in enumerate(listed):
        function = out[2 * i] if 2 * i < len(out) else '?'
        line = out[2 * i + 1] if 2 * i + 1 < len(out) else '?'
        found[a] = '%s (%s)' % (function[:90], line)
    return found


def main(argv):
    trace = argv[1] if len(argv) > 1 else os.path.join(ROOT, 'sdroot', '_nds', 'hl_nds', 'data', 'heaptrace.txt')
    elf = argv[2] if len(argv) > 2 else os.path.join(ROOT, 'build', 'arm9', 'arm9.elf')
    lines = open(trace).read().splitlines()
    group = re.compile(r'^\s+gen (\d+) caller ([0-9a-f]{8}): (\d+) blocks, (\d+) bytes')
    callers = {int(m.group(2), 16) for m in map(group.match, lines) if m}
    inside = {a for a in callers if 0x02000000 <= a < 0x02400000 or 0x01000000 <= a < 0x01008000}
    named = names(elf, inside)
    fail = re.compile(r'^(trace: .* caller )([0-9a-f]{8})$')
    for m in map(fail.match, lines):
        if m:
            callers.add(int(m.group(2), 16))
    inside = {a for a in callers if 0x02000000 <= a < 0x02400000 or 0x01000000 <= a < 0x01008000}
    named = names(elf, inside)
    for line in lines:
        f = fail.match(line)
        if f:
            print(line + '  ' + named.get(int(f.group(2), 16), '?'))
            continue
        m = group.match(line)
        if not m:
            print(line)
            continue
        a = int(m.group(2), 16)
        who = named.get(a, 'a family, or not the program\'s' if a not in inside else '?')
        print('  gen %-4s %7d bytes %5d blocks  %08x %s' % (m.group(1), int(m.group(4)), int(m.group(3)), a, who))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
