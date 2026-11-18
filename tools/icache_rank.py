#!/usr/bin/env python3
"""Which functions to put in ITCM, by what the instruction cache misses.

usage: icache_rank.py <measuring elf> <current elf> <from> <to> [<from> <to>...]
                      [--families=<file>[,<file>...]] [--family-elfs=<dir>] [--budget N] [--top N]

The measuring ELF is an HLDS_ITCM_OFF=1 build (build.py): the program's code in
main memory, but for what stays in ITCM whatever (the interrupts', the divide's,
libnds's own). It ran in the profiling melonDS (tools/icache/setup.sh builds
it; tools/icache/scenes.sh runs the scenes), which wrote snapshots of each
32-byte line's lookups and misses to $HLDS_ICACHE_OUT.<n>; a scene is the
difference of two of them (the first may be "-": nothing before it). Several
scenes are added together.

The game's families (boss, military, net: dsl/<family>.dsl) are loaded onto
the heap with the map that needs them, wherever there is room: the measuring
build says where in perf.txt ("family: <name> <address of its GetFamilyAPI>"),
scenes.sh keeps those lines in <scene>/families.txt, and --families gives them,
one file to a scene in the scenes' order. Their code is then named from
<family-elfs>/<family>/<family>.elf (the measuring build's: build/ by
default), and marked: a family cannot go in ITCM, so it is
left out of the filling below.

Each line is the function's whose code it starts in. A line's lookups and misses are shared among the functions it
holds, by their bytes in it. A function's worth is its
misses for each byte of ITCM it would take -- what is missed often is what
leaves the cache and comes back, the code ITCM saves the most on (a loop that
stays in the cache misses little, whatever its share of the time). The budget
(tools/arm9-report.sh's ITCM_BUDGET, less what stays in ITCM in the measuring
build) is filled greedily, best first, and set against what the current build
has in ITCM: what would go in, what would come out -- and what share of the
misses each of the two layouts would have kept off main memory.

The profiling melonDS is for ranking only: its timings are not the console's.
"""
import bisect
import os
import collections
import subprocess
import sys

NM = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-nm'
ITCM_LOW, ITCM_HIGH = 0x01000000, 0x01008000


def functions(elf):
    """Sorted (address, size, name) of the ELF's functions with a size."""
    out = subprocess.run([NM, '-C', '-n', '-S', '--defined-only', elf], capture_output=True, text=True).stdout
    found = []
    for line in out.splitlines():
        part = line.split(' ', 3)
        if len(part) == 4 and part[2] in 'TtWw':
            size = int(part[1], 16)
            if size > 0:
                found.append((int(part[0], 16) & ~1, size, part[3]))
    found.sort()
    return found


def symbol(elf, name):
    out = subprocess.run([NM, elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        part = line.split()
        if len(part) == 3 and part[2] == name:
            return int(part[0], 16)
    return None


def snapshot(path):
    """{line address: (lookups, misses)}"""
    table = {}
    if path == '-':
        return table
    for line in open(path):
        if line.startswith('#'):
            continue
        part = line.split()
        if len(part) == 3:
            table[int(part[0], 16)] = (int(part[1], 16), int(part[2], 16))
    return table


ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FAMILY_LOW = 0x01000000  # (a family's own symbols are from 0; the program's, linked in, above)


def family_functions(lines_file, elfs):
    """(address, size, name) of the families' functions, where the scene had them."""
    found = []
    if not lines_file:
        return found
    for line in open(lines_file):
        part = line.split()
        if len(part) != 3 or part[0] != 'family:':
            continue
        name, api = part[1], int(part[2], 16)
        elf = os.path.join(elfs, name, name + '.elf')
        if not os.path.exists(elf):
            print('(no %s: the %s family\'s lines go unnamed)' % (elf, name), file=sys.stderr)
            continue
        offset = symbol(elf, 'GetFamilyAPI')
        if offset is None:
            continue
        base = api - (offset & ~1)
        for at, size, fname in functions(elf):
            if at < FAMILY_LOW:
                found.append((base + at, size, '[%s] %s' % (name, fname)))
    return found


def attribute(table, before, after, lookups, misses):
    """Adds a scene's lookups and misses to each function's; returns what fell outside any."""
    starts = [a for a, _, _ in table]
    lost = 0
    for line, (n, m) in after.items():
        n0, m0 = before.get(line, (0, 0))
        n, m = n - n0, m - m0
        if n == 0 and m == 0:
            continue
        # (Shared among the functions the 32-byte line holds, by their bytes
        # in it: moving one to ITCM saves only its own share of the fills.)
        i = bisect.bisect_right(starts, line + 31) - 1
        shared = 0
        while i >= 0 and table[i][0] + table[i][1] > line:
            low, high = max(line, table[i][0]), min(line + 32, table[i][0] + table[i][1])
            if high > low:
                name = table[i][2]
                lookups[name] += n * (high - low) / 32
                misses[name] += m * (high - low) / 32
                shared += high - low
            i -= 1
        lost += m * (32 - min(shared, 32)) / 32
    return lost


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    opts = dict(a[2:].split('=', 1) for a in argv[1:] if a.startswith('--') and '=' in a)
    if len(args) < 4 or len(args) % 2 != 0:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    measuring, current = args[0], args[1]
    pairs = [(args[k], args[k + 1]) for k in range(2, len(args), 2)]
    family_files = opts.get('families', '').split(',') if opts.get('families') else []
    family_elfs = opts.get('family-elfs', os.path.join(ROOT, 'build'))
    budget = int(opts.get('budget', 32000))
    top = int(opts.get('top', 60))

    program = functions(measuring)
    size = {name: s for _, s, name in program}
    lookups, misses = collections.Counter(), collections.Counter()
    lost = 0
    family = set()
    for k, (start, end) in enumerate(pairs):
        theirs = family_functions(family_files[k] if k < len(family_files) else '', family_elfs)
        for _, s, name in theirs:
            size[name] = s
            family.add(name)
        table = sorted(program + theirs)
        lost += attribute(table, snapshot(start), snapshot(end), lookups, misses)

    # What stays in ITCM in the measuring build, whatever: taken off the budget.
    low, high = symbol(measuring, '__itcm_start'), symbol(measuring, '__itcm_end')
    fixed = (high - low) if low is not None and high is not None else 0
    room = budget - fixed
    now = {name for a, _, name in functions(current) if ITCM_LOW <= a < ITCM_HIGH}
    stays = {name for a, _, name in program if ITCM_LOW <= a < ITCM_HIGH}

    ranked = sorted((n for n in misses if n not in stays and misses[n] > 0),
                    key=lambda n: misses[n] / size[n], reverse=True)
    chosen, used = [], 0
    for name in ranked:
        if name not in family and used + size[name] <= room:
            chosen.append(name)
            used += size[name]
    total = sum(misses.values())
    covered = sum(misses[n] for n in chosen)
    today = [n for n in now if n not in stays and n in size]
    kept_today = sum(misses[n] for n in today)

    print('misses in the scenes: %d (%d outside any function, %d in the families); budget %d, %d of it kept by '
          'what stays (%d free)' % (total, lost, sum(misses[n] for n in family), budget, fixed, room))
    print('%-7s %7s %9s %9s %8s  %s' % ('itcm', 'bytes', 'misses', 'lookups', 'per byte', 'function'))
    for name in ranked[:top]:
        mark = 'family' if name in family else ('now' if name in now else '')
        print('%-7s %7d %9d %9d %8.2f  %s' % (mark, size[name], misses[name], lookups[name], misses[name] / size[name],
                                             name[:100]))
    into = [n for n in chosen if n not in now]
    out = [n for n in today if n not in chosen]
    print('\ntoday\'s ITCM: %d bytes, %d of the misses (%.1f%%)' % (sum(size[n] for n in today), kept_today,
                                                                   100.0 * kept_today / max(total, 1)))
    print('the budget filled: %d bytes, %d of the misses (%.1f%%)' % (used, covered, 100.0 * covered / max(total, 1)))
    print('move in (%d bytes):' % sum(size[n] for n in into))
    for n in into:
        print('  %6d %8d  %s' % (size[n], misses[n], n[:110]))
    print('move out (%d bytes):' % sum(size[n] for n in out))
    for n in sorted(out, key=lambda n: misses[n]):
        print('  %6d %8d  %s' % (size[n], misses[n], n[:110]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
