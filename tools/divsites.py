#!/usr/bin/env python3
"""Where the hardware's divides and roots are asked for, from perf.txt's
"divsites:" and "rootsites:" lines; and the library's 32-bit divides, from
"div32sites:".

usage: divsites.py <perf.txt> [<map name>] [--elf ARM9.elf] [--last N] [--top N]

A profiling build (HLDS_PROFILE=1) notes the return address of every call
to the divide and the root (source/HwMath.s calls hlProfileSite in
source/DebugLog.cpp), and writes the busiest after each "time:" line, with
how many of the divides had both numbers fitting 32 bits. Here they are
named with addr2line -i, from the ELF of the build that wrote them
(build/arm9/arm9.elf by default): the line of the call, and the functions it
is inlined into. Counts are per frame, over the lines read (all of them, or
the map's, or the last N).
"""
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDR2LINE = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-addr2line'


def parse(lines, map_name=None):
    """{'divsites'|'rootsites'|'div32sites': [(frames, fits, lost, {address: count})]},
    each line kept if its "time:" line is of `map_name` (or any). (For
    div32sites, `fits` is how many divides in all.)"""
    out = collections.defaultdict(list)
    current_map = None
    for line in lines:
        if line.startswith('time:'):
            current_map = line.split()[1]
            continue
        m = re.match(r'(divsites|rootsites|div32sites): (\d+) frames'
                     r'(?:, (\d+) (?:fit 32 bits|divides), (\d+) lost)?(.*)$', line)
        if not m or (map_name is not None and current_map != map_name):
            continue
        sites = {}
        for part in m.group(5).split(','):
            part = part.strip()
            if part:
                address, count = part.split()
                sites[int(address, 16)] = int(count)
        out[m.group(1)].append((int(m.group(2)), int(m.group(3) or 0), int(m.group(4) or 0), sites))
    return out


def where(elf, addresses):
    """{address: [(function, file:line), ...]} -- the call's own line first,
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
    for name in ('--elf', '--last', '--top'):
        if name in argv:
            k = argv.index(name)
            opts[name] = argv[k + 1]
            del argv[k:k + 2]
    if not argv:
        sys.exit(__doc__)
    perf, map_name = argv[0], (argv[1] if len(argv) > 1 else None)
    elf = opts.get('--elf', os.path.join(ROOT, 'build', 'arm9', 'arm9.elf'))
    top = int(opts.get('--top', 30))
    found = parse(open(perf).read().splitlines(), map_name)
    if not found:
        sys.exit('divsites: no "divsites:" lines%s' % (' for ' + map_name if map_name else ''))
    for kind in ('divsites', 'rootsites', 'div32sites'):
        chosen = found.get(kind, [])
        if '--last' in opts:
            chosen = chosen[-int(opts['--last']):]
        if not chosen:
            continue
        frames = sum(c[0] for c in chosen) or 1
        totals = collections.Counter()
        for _f, _fits, _lost, sites in chosen:
            totals.update(sites)
        names = where(elf, list(totals))
        whole = sum(totals.values())
        what = {'divsites': 'divides', 'rootsites': 'roots', 'div32sites': "the library's 32-bit divides"}[kind]
        print('%s: %.1f a frame at the sites shown, over %d frames' % (what, whole / frames, frames))
        if kind == 'div32sites':
            print('  (%.1f a frame in all; %d calls found no room in the table)'
                  % (sum(c[1] for c in chosen) / frames, sum(c[2] for c in chosen)))
        if kind == 'divsites':
            fits = sum(c[1] for c in chosen)
            lost = sum(c[2] for c in chosen)
            print('  (%.1f a frame had both numbers fitting 32 bits; %d calls found no room in the table)'
                  % (fits / frames, lost))
        # By the line of the call, then by the outermost function it is in.
        by_line = collections.Counter()
        outer = {}
        for address, count in totals.items():
            chain = names.get(address) or [('?', '?')]
            # (Without line numbers -- a build without -g -- by the function.)
            key = chain[0][1] if not chain[0][1].startswith('?') and ':?' not in chain[0][1] else chain[0][0].split('(')[0]
            by_line[key] += count
            outer.setdefault(key, set()).add(chain[-1][0].split('(')[0])
        for place, count in by_line.most_common(top):
            print('  %7.1f  %-48s in %s' % (count / frames, place, ', '.join(sorted(outer[place]))[:90]))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
