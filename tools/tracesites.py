#!/usr/bin/env python3
"""Who traces, and how long it takes them, from perf.txt's "tracesites:"
lines.

usage: tracesites.py <perf.txt> [<map name>] [--elf-dir DIR] [--last N] [--top N]

A profiling build (HLDS_PROFILE=1) counts each of the traces the game asks
for -- traceAll, traceHull, traceShot, walkStep, lineClear, walkClear
(shared/HlTraceCount.hpp) -- by the line that called it: how many times and
the time they took (source/DebugLog.cpp), and writes the costliest after each
"time:" line. Here they are named with addr2line -i -- the line of the call
and the functions it is inlined into -- from the ELFs of the build that wrote
them: DIR (default build/arm9's arm9.elf; a build kept by make.sh is
build/elf/<name>/, which has the families' ELFs too). A call from a family of
the game (military, boss, ...) is placed by the "family:" lines the same
build writes as it loads one. Times are microseconds a frame, the frame's own
and the think worker's apart: the worker's comes out of the frame's wait for
the screen.
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDR2LINE = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-addr2line'
NM = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm'
KINDS = ['traceAll', 'traceHull', 'traceShot', 'walkStep', 'lineClear', 'walkClear']
ON_WORKER = 8


def parse(lines, map_name=None):
    """[(frames, lost, [(address, kind, count, micros)])], and {family: its
    GetFamilyAPI where it was loaded}."""
    out, families, current = [], {}, None
    for line in lines:
        if line.startswith('time:'):
            current = line.split()[1]
            continue
        m = re.match(r'family: (\S+) ([0-9a-f]+)', line)
        if m:
            families[m.group(1)] = int(m.group(2), 16)
            continue
        m = re.match(r'tracesites: (\d+) frames, (\d+) lost(.*)$', line)
        if not m or (map_name is not None and current != map_name):
            continue
        sites = []
        for part in m.group(3).split(','):
            part = part.strip()
            if part:
                address, kind, count, micros = part.split()
                sites.append((int(address, 16), int(kind), int(count), int(micros)))
        out.append((int(m.group(1)), int(m.group(2)), sites))
    return out, families


def symbol(elf, name):
    out = subprocess.run([NM, '--defined-only', elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        part = line.split()
        if len(part) == 3 and part[2] == name:
            return int(part[0], 16)
    return None


def text_range(elf):
    """(first, end) of the program's code."""
    first = symbol(elf, '__text_start')
    end = symbol(elf, '__text_end')
    return (first or 0x02000000, end or 0x02400000)


def where(elf, addresses):
    """{address: [(function, file:line), ...]}: the call's own line first,
    then each function it is inlined into, outwards."""
    if not addresses:
        return {}
    # (A return address: the call is just before it. Thumb's have bit 0 set.)
    asked = [(a & ~1) - 2 for a in addresses]
    out = subprocess.run([ADDR2LINE, '-e', elf, '-i', '-f', '-C', '-a'] + ['%x' % a for a in asked],
                         capture_output=True, text=True).stdout.splitlines()
    result, current, i = {}, None, 0
    back = dict(zip(asked, addresses))
    while i < len(out):
        line = out[i]
        if line.startswith('0x'):
            current = back.get(int(line, 16))
            result[current] = []
            i += 1
            continue
        function = line
        place = out[i + 1] if i + 1 < len(out) else '?'
        place = os.path.relpath(place.split(' (')[0], ROOT) if place.startswith('/') else place
        result[current].append((function, place))
        i += 2
    return result


def main():
    argv = sys.argv[1:]
    opts = {}
    for name in ('--elf-dir', '--last', '--top'):
        if name in argv:
            k = argv.index(name)
            opts[name] = argv[k + 1]
            del argv[k:k + 2]
    if not argv:
        sys.exit(__doc__)
    perf, map_name = argv[0], (argv[1] if len(argv) > 1 else None)
    elf_dir = opts.get('--elf-dir', os.path.join(ROOT, 'build', 'arm9'))
    top = int(opts.get('--top', 12))
    found, families = parse(open(perf).read().splitlines(), map_name)
    if '--last' in opts:
        found = found[-int(opts['--last']):]
    if not found:
        sys.exit('tracesites: no "tracesites:" lines%s' % (' for ' + map_name if map_name else ''))
    frames = sum(f for f, _l, _s in found) or 1
    lost = sum(l for _f, l, _s in found)
    totals = collections.defaultdict(lambda: [0, 0])
    for _f, _l, sites in found:
        for address, kind, count, micros in sites:
            totals[(address, kind)][0] += count
            totals[(address, kind)][1] += micros
    # Where each address is: the program, or a family loaded at its place.
    program = os.path.join(elf_dir, 'arm9.elf')
    first, end = text_range(program)
    places = {}
    by_elf = collections.defaultdict(list)
    for address, _kind in totals:
        if first <= address < end or address < 0x02000000:
            by_elf[(program, 0)].append(address)
            continue
        for family, api in families.items():
            felf = os.path.join(elf_dir, family + '.elf')
            if not os.path.exists(felf):
                continue
            base = api - (symbol(felf, 'GetFamilyAPI') or 0)
            if 0 <= address - base < 0x40000:
                by_elf[(felf, base)].append(address)
                break
        else:
            places[address] = [('(somewhere else)', '?')]
    for (elf, base), addresses in by_elf.items():
        named = where(elf, [a - base for a in addresses])
        tag = '' if base == 0 else '[%s] ' % os.path.basename(elf)[:-4]
        for a in addresses:
            places[a] = [(tag + f, p) for f, p in named.get(a - base, [('?', '?')])]
    print('%d frames; the costliest callers kept (%d calls found no room in the table)' % (frames, lost))
    for worker in (False, True):
        for kind, name in enumerate(KINDS):
            rows = collections.defaultdict(lambda: [0, 0, set()])
            for (address, k), (count, micros) in totals.items():
                if (k & 7) != kind or bool(k & ON_WORKER) != worker:
                    continue
                chain = places.get(address) or [('?', '?')]
                key = chain[0][1] if ':' in chain[0][1] and ':?' not in chain[0][1] else chain[0][0].split('(')[0]
                rows[key][0] += count
                rows[key][1] += micros
                rows[key][2].add(chain[-1][0].split('(')[0])
            if not rows:
                continue
            whole = sum(r[1] for r in rows.values())
            print('%s%s: %.0f us a frame, %.1f calls' % (name, ' (on the worker)' if worker else '', whole / frames,
                                                       sum(r[0] for r in rows.values()) / frames))
            for key, (count, micros, outer) in sorted(rows.items(), key=lambda kv: -kv[1][1])[:top]:
                print('  %7.0f us %6.1f calls  %-44s in %s' % (micros / frames, count / frames, key[:44],
                                                             ', '.join(sorted(outer))[:70]))
        print()


if __name__ == '__main__':
    main()
