#!/usr/bin/env python3
"""The game's monster routes against the port's, for every pair of nodes.

usage: nodroutes.py <map.nod> <diag_replay output with DIAG_LINKS and DIAG_ROUTES>
       nodroutes.py <map.nod> <the same> --route x y z x y z
                    (one way: the game's by its table from the node nearest
                    the first place to the one nearest the second, for a
                    soldier, and the brushes on it; for aiming a scenario)

The real game routes its monsters by the tables its node graph keeps
(the .nod's routing info: the first step from each node to each other,
per hull and per whether the monster opens doors), made when the graph
was built, with the doors as they were then. The port works each way out
as it is asked (pathStep), with the doors as they are now (linkShut).
This holds the two together for a person's size: the port's first steps
come from diag_replay (DIAG_LINKS=<frame> DIAG_ROUTES=1), the game's are
read from its table. Nodes are matched by place (the converter keeps the
game's nodes but not their order). For each pair it says whether the
first step and the whole way are the same, and where they are not, the
brushes (doors, crates) on the game's way and how the port has them.
"""
import collections
import math
import re
import struct
import sys

GRAPH_BYTES, NODE_BYTES, LINK_BYTES, DIST_BYTES = 8396, 88, 24, 16
HUMAN_HULL = 1  # the table's hulls: small, human, large, fly
NO_WAY = 255
SHUT = {0: 'open', 1: 'a door', 2: 'no way'}


def read_nod(path):
    data = open(path, 'rb').read()
    graph = 4
    nodes, links, route = struct.unpack_from('<iii', data, graph + 24)
    at = graph + GRAPH_BYTES
    origins, offsets = [], []
    for n in range(nodes):
        origins.append(struct.unpack_from('<fff', data, at + n * NODE_BYTES))
        offsets.append(struct.unpack_from('<8i', data, at + n * NODE_BYTES + 40))
    start = at + nodes * NODE_BYTES + links * LINK_BYTES + nodes * DIST_BYTES
    return origins, offsets, data[start:start + route]


def next_in_route(table, offsets, count, current, dest, hull, cap):
    """The first step from `current` to `dest` in the game's table. Its
    runs, as the game reads them: a negative byte -n, the next n
    destinations are reached straight (the step is the destination); a
    byte n >= 0 and a signed byte d after it, the next n + 1 destinations
    are all reached by the node d after this one (wrapping round)."""
    left = dest + 1
    at = offsets[current][hull * 2 + cap]
    while left > 0:
        ch = struct.unpack_from('<b', table, at)[0]
        at += 1
        if ch < 0:
            if left <= -ch:
                return dest
            left += ch
        else:
            if left <= ch + 1:
                step = current + struct.unpack_from('<b', table, at)[0]
                return step - count if step >= count else step + count if step < 0 else step
            left -= ch + 1
            at += 1
    return current


def walk(first, a, b, limit=300):
    """The whole way from a to b by a first-step function; None if none."""
    way = [a]
    while way[-1] != b:
        step = first(way[-1], b)
        if step is None or step == way[-1] or len(way) > limit:
            return None
        way.append(step)
    return way


def main():
    nod, dump = sys.argv[1], sys.argv[2]
    origins, offsets, table = read_nod(nod)
    port_pos, port_next, links = {}, {}, {}
    link_line = re.compile(r"link (\d+)>(\d+) .* brush (\S+) (\S+) name '([^']*)' master '([^']*)' flags (\S*) ?shut (-?\d+)")
    for line in open(dump):
        p = line.split()
        if line.startswith('node '):
            port_pos[int(p[1])] = tuple(float(v) for v in p[2:5])
        elif line.startswith('next '):
            port_next[(int(p[1]), int(p[2]), int(p[3]))] = int(p[4])
        elif line.startswith('link '):
            m = link_line.match(line)
            if m:
                a, b = int(m[1]), int(m[2])
                info = (m[4], m[3], m[5], m[6], int(m[8]), m[7])
                links[(a, b)] = links[(b, a)] = info
    # Game node -> port node, by place.
    to_port = {}
    for g, o in enumerate(origins):
        best = min(port_pos, key=lambda n: math.dist(o, port_pos[n]), default=None)
        if best is not None and math.dist(o, port_pos[best]) <= 4:
            to_port[g] = best
    print(f'{nod}: {len(origins)} nodes in the game\'s graph, {len(port_pos)} in the port\'s, {len(to_port)} matched')
    from_port = {p: g for g, p in to_port.items()}
    if '--route' in sys.argv:
        v = [float(x) for x in sys.argv[sys.argv.index('--route') + 1:][:6]]
        near = lambda p: min(range(len(origins)), key=lambda n: math.dist(p, origins[n]))
        a, b = near(v[:3]), near(v[3:])
        for cap in (1, 0):
            way = walk(lambda c, d: next_in_route(table, offsets, len(origins), c, d, HUMAN_HULL, cap), a, b)
            print(f'  {"opens doors" if cap else "does not"}: ' + ('no way' if way is None else ''))
            for x, y in zip(way or [], (way or [])[1:]):
                px, py = to_port.get(x), to_port.get(y)
                brush = links.get((px, py))
                o = origins[y]
                print(f'    -> {o[0]:.0f} {o[1]:.0f} {o[2]:.0f}' +
                      (f'   through {brush[0]} {brush[1]} ({brush[2]}) flags {brush[5] or "-"} port {SHUT.get(brush[4])}'
                       if brush else ''))
        return
    for cap in (1, 0):
        who = 'opens doors (a soldier)' if cap else 'does not open doors'
        same_first = same_way = 0
        kinds = collections.Counter()
        brush_ways = collections.Counter()
        examples = collections.defaultdict(list)
        pairs = 0
        for ga in to_port:
            for gb in to_port:
                if ga == gb:
                    continue
                pa, pb = to_port[ga], to_port[gb]
                pairs += 1
                g_way = walk(lambda c, d: next_in_route(table, offsets, len(origins), c, d, HUMAN_HULL, cap), ga, gb)
                p_way = walk(lambda c, d: (None if port_next.get((cap, c, d), NO_WAY) == NO_WAY
                                           else port_next[(cap, c, d)]), pa, pb)
                g_mapped = [to_port.get(n) for n in g_way] if g_way else None
                if g_way is not None and None in g_mapped:
                    kinds['the game goes by a node the port has not'] += 1
                    continue
                # (The brushes on the game's way, and how the port has them.)
                crossed = []
                if g_mapped:
                    for x, y in zip(g_mapped, g_mapped[1:]):
                        if (x, y) in links:
                            crossed.append(links[(x, y)])
                if g_mapped == p_way:
                    same_way += 1
                    same_first += 1
                    kind = 'the same way'
                elif g_way is None and p_way is None:
                    same_way += 1
                    same_first += 1
                    kind = 'no way, both'
                elif g_way is None:
                    kind = 'the port has a way, the game none'
                elif p_way is None:
                    kind = 'the game has a way, the port none'
                elif g_mapped[1] == p_way[1]:
                    same_first += 1
                    kind = 'same first step, then apart'
                else:
                    kind = 'apart from the first step'
                kinds[kind] += 1
                if kind not in ('the same way', 'no way, both'):
                    shut_on_game = [c for c in crossed if c[4] != 0]
                    key = ('through ' + ', '.join(sorted({f'{c[0]} {c[1]}{" ("+c[2]+")" if c[2] else ""} '
                                                           f'[{SHUT.get(c[4], c[4])}]' for c in shut_on_game}))
                           if shut_on_game else 'no shut brush on the game\'s way')
                    brush_ways[(kind, key)] += 1
                    if len(examples[(kind, key)]) < 1:
                        o1, o2 = origins[ga], origins[gb]
                        examples[(kind, key)].append(f'{o1[0]:.0f} {o1[1]:.0f} {o1[2]:.0f} > {o2[0]:.0f} {o2[1]:.0f} {o2[2]:.0f}')
        print(f'  a person\'s size that {who}: {pairs} pairs; first step the same {same_first} '
              f'({100.0 * same_first / max(pairs, 1):.1f}%), whole way the same {same_way} '
              f'({100.0 * same_way / max(pairs, 1):.1f}%)')
        for kind, n in kinds.most_common():
            print(f'    {n:6d}  {kind}')
        for (kind, key), n in brush_ways.most_common(10):
            print(f'      {n:6d}  {kind}: {key}   e.g. {examples[(kind, key)][0]}')


if __name__ == '__main__':
    main()
