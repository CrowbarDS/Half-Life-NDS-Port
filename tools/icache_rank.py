#!/usr/bin/env python3
"""Which functions to put in ITCM, by what the instruction cache misses.

usage: icache_rank.py <measuring elf> <current elf> <from> <to> [<from> <to>...]
                      [--families=<file>[,<file>...]] [--family-elfs=<dir>] [--budget N] [--top N]
                      [--by-phase --frames=N[,N...] --miss-cycles=C --copy-cycles=B --swaps=S --hz=F
                       --groups=name:phase,phase...;name:...] [--exclude=word,word...]

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

--by-phase (snapshots from the phase-counting profiler, a build with
HLDS_ICACHE_PHASES: the misses of each part of the frame, by the profiling
sections of source/DebugLog.hpp, and of the interrupts): whether an ITCM
region swapped by phase would beat today's one fixed set. For each size of
region R (4, 8, 12, 16 KB), a resident core of the budget less R is filled
from all the misses, best first, and a region of R for each phase from that
phase's misses (of what the core left); against it, the budget filled from
all the misses at once. A layout's saving is its misses kept off main memory
times what a miss costs (--miss-cycles, ARM9 cycles; tools/hwtest/itcmswap
measures it on the console), less what copying the region in costs (R times
--copy-cycles a byte, --swaps times a frame), per frame over --frames frames
(the snapshots say how many: the profiler counts the game's frames; or
--frames, for older ones). --groups puts phases together, one region's set
for each group (a frame then swaps once a group: --swaps defaults to how
many there are), the phases left out of every group served by the core
alone -- "logic:1,2,3,9;draw:4,5,6,7", say: the game's side, then the
drawing's. A model to decide by, not a measure: what it
proposes is then built and timed on the console.

--exclude leaves out, from the ranking and its totals, every function whose
name holds one of the words: by default the measuring build's own code
(the profiler's: DebugLog::Perf, hlProfileSite, the timers it reads), which
a game played has none of; --exclude= (empty) keeps it.

The profiling melonDS is for ranking only: its timings are not the console's.
"""
EXCLUDE_DEFAULT = 'DebugLog::Perf,hlProfileSite,cpuStartTiming,cpuGetTiming,cpuEndTiming,hwMathCheck'
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


def snapshot(path, phase=None):
    """{line address: (lookups, misses)}: all of them, or one phase's (a
    phase-counting snapshot's lines are "addr phase lookups misses"; an older
    one's, "addr lookups misses", are all phase 0)."""
    table = {}
    if path == '-':
        return table
    for line in open(path):
        if line.startswith('#'):
            continue
        part = line.split()
        if len(part) == 3:
            at, p, n, m = int(part[0], 16), 0, int(part[1], 16), int(part[2], 16)
        elif len(part) == 4:
            at, p, n, m = int(part[0], 16), int(part[1], 16), int(part[2], 16), int(part[3], 16)
        else:
            continue
        if phase is not None and p != phase:
            continue
        n0, m0 = table.get(at, (0, 0))
        table[at] = (n0 + n, m0 + m)
    return table


def frames_at(path):
    """How many frames had begun when a phase-counting snapshot was written
    (its head: "# lookups N phases frames F"); None if it does not say."""
    if path == '-':
        return 0
    with open(path) as f:
        head = f.readline().split()
    if 'frames' in head:
        return int(head[head.index('frames') + 1])
    return None


def phases_in(path):
    """The phases a snapshot has lines of."""
    found = set()
    for line in open(path):
        part = line.split()
        if len(part) == 4 and not line.startswith('#'):
            found.add(int(part[1], 16))
    return sorted(found)


# What a phase is: source/DebugLog.hpp's sections, plus one; 15 the interrupts.
PHASE_NAMES = {0: 'outside', 1: 'input', 2: 'logic', 3: 'move', 4: 'walk', 5: 'emit', 6: 'pose', 7: 'models',
               8: 'frame', 9: 'monsters', 10: 'look', 11: 'hear', 12: 'going', 13: 'keys', 15: 'irq'}


def fill(order, size, misses, room, exclude=()):
    """Best first, what fits in `room` bytes: (the names, the bytes, their misses)."""
    chosen, used = [], 0
    for name in order:
        if name in exclude:
            continue
        if used + size[name] <= room:
            chosen.append(name)
            used += size[name]
    return chosen, used, sum(misses[n] for n in chosen)


def by_phase(opts, pairs, program, family_files, family_elfs, size, stays, family, room, fixed):
    """--by-phase: the per-phase misses, and the swapped region modelled."""
    phase_misses = collections.defaultdict(collections.Counter)
    phases = sorted(set(p for _, end in pairs for p in phases_in(end)))
    for k, (start, end) in enumerate(pairs):
        theirs = family_functions(family_files[k] if k < len(family_files) else '', family_elfs)
        table = sorted(program + theirs)
        for p in phases:
            lookups = collections.Counter()
            attribute(table, snapshot(start, p), snapshot(end, p), lookups, phase_misses[p])
    total = collections.Counter()
    for p in phases:
        total.update(phase_misses[p])
    grouped = None
    if opts.get('groups'):
        grouped = collections.OrderedDict()
        for part in opts['groups'].split(';'):
            gname, members = part.split(':')
            grouped[gname] = [int(x) for x in members.split(',')]
    if 'frames' in opts:
        frames = sum(int(f) for f in opts['frames'].split(','))
    else:
        counted = [(frames_at(end), frames_at(start)) for start, end in pairs]
        if any(e is None or b is None for e, b in counted):
            print('(the snapshots do not count frames: --frames=N[,N...] gives them)', file=sys.stderr)
            return 2
        frames = sum(e - b for e, b in counted)
    miss_cycles = float(opts.get('miss-cycles', 40))
    copy_cycles = float(opts.get('copy-cycles', 1.0))
    swaps = float(opts.get('swaps', len(opts.get('groups', '').split(';')) if opts.get('groups') else 2))
    cpu_hz = float(opts.get('hz', 67027964))  # (a DSi in its own mode: 134055928)

    def usable(names):
        return [n for n in names if n not in stays and n not in family and n in size and size[n] > 0]

    every = sum(total.values())
    print('misses by phase (of %d), over %d frames:' % (every, frames))
    for p in phases:
        m = sum(phase_misses[p].values())
        print('  %-9s %9d  %5.1f%%  %8.0f a frame' % (PHASE_NAMES.get(p, str(p)), m, 100.0 * m / max(every, 1), m / frames))
    order_all = sorted(usable(total), key=lambda n: total[n] / size[n], reverse=True)
    one, one_used, one_kept = fill(order_all, size, total, room)
    print('\none fixed set of %d bytes: %d misses kept (%.1f%%), %.3f ms a frame at %.0f cycles a miss'
          % (one_used, one_kept, 100.0 * one_kept / max(every, 1), one_kept / frames * miss_cycles / cpu_hz * 1000,
             miss_cycles))
    # The busiest phases' own sets: how much of one phase's best is another's.
    busiest = sorted(phases, key=lambda p: -sum(phase_misses[p].values()))[:4]
    print('\neach phase\'s best %d bytes, and what of it the others\' best hold:' % room)
    best = {}
    for p in busiest:
        order = sorted(usable(phase_misses[p]), key=lambda n: phase_misses[p][n] / size[n], reverse=True)
        best[p] = set(fill(order, size, phase_misses[p], room)[0])
    for p in busiest:
        others = ', '.join('%s %d%%' % (PHASE_NAMES.get(q, q),
                                       100 * sum(size[n] for n in best[p] & best[q]) // max(sum(size[n] for n in best[p]), 1))
                           for q in busiest if q != p)
        print('  %-9s %s' % (PHASE_NAMES.get(p, p), others))
    print('\nregion  core   kept by core  kept by region  vs one set   copies     net a frame')
    for region in (4096, 8192, 12288, 16384):
        if region >= room:
            continue
        core, core_used, core_kept = fill(order_all, size, total, room - region)
        region_kept = 0
        sets = [(g, members) for g, members in grouped.items()] if grouped else [(PHASE_NAMES.get(p, p), [p]) for p in phases]
        for _g, members in sets:
            theirs = collections.Counter()
            for p in members:
                theirs.update(phase_misses[p])
            order = sorted(usable(theirs), key=lambda n: theirs[n] / size[n], reverse=True)
            names, _, _ = fill(order, size, theirs, region, exclude=set(core))
            region_kept += sum(theirs[n] for n in names)
        gain = (core_kept + region_kept - one_kept) / frames * miss_cycles / cpu_hz * 1000
        copy = swaps * region * copy_cycles / cpu_hz * 1000
        print('%6d %6d %14d %15d %+10.3f ms %8.3f ms %+10.3f ms'
              % (region, core_used, core_kept, region_kept, gain, copy, gain - copy))
    return 0


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

    excluded = [w for w in opts.get('exclude', EXCLUDE_DEFAULT).split(',') if w]
    program = [(a, s, n) for a, s, n in functions(measuring) if not any(w in n for w in excluded)]
    size = {name: s for _, s, name in program}
    if 'by-phase' in [a[2:] for a in argv[1:] if a.startswith('--')]:
        low, high = symbol(measuring, '__itcm_start'), symbol(measuring, '__itcm_end')
        fixed = (high - low) if low is not None and high is not None else 0
        stays = {name for a, _, name in program if ITCM_LOW <= a < ITCM_HIGH}
        family = set()
        for k in range(len(pairs)):
            for _, s, name in family_functions(family_files[k] if k < len(family_files) else '', family_elfs):
                size[name] = s
                family.add(name)
        return by_phase(opts, pairs, program, family_files, family_elfs, size, stays, family, budget - fixed, fixed)
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
