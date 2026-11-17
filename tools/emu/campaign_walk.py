#!/usr/bin/env python3
"""A walk through a game's maps by their own ways between them.

usage: campaign_walk.py <stage maps dir> <first map> [--only <prefix>...]
       campaign_walk.py <stage maps dir> --pairs <map>...

From the staged entity files (<map>.ent): which map's trigger_changelevel
leads to which. The walk starts at <first map> and goes, a way at a time, to
a map not yet been to -- a neighbour if there is one, else by the fewest ways
to the nearest -- until every map it can reach (of those --only names) has
been gone into once. Printed as the maps in order, one line.

--pairs: for each map named, each neighbour and back -- "n m n" -- a line
each: into the map by each of its ways, and out by it again ("m n", out
only, where the neighbour has no way back). Only ways walked into count: a
trigger_changelevel with a brush and without flag 2 (as the converter has
it).

For tools/emu/room.sh's walks (debug_start.txt's "go" lines, main.cpp).
"""
import collections
import os
import re
import sys


def ways(folder):
    graph = collections.defaultdict(set)
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".ent"):
            continue
        here = name[:-4]
        text = open(os.path.join(folder, name), encoding="latin-1").read()
        for block in re.findall(r"\{([^}]*)\}", text):
            keys = dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block))
            there = keys.get("map", "").lower()
            # (Only a way walked into, as the converter cooks it: one with a
            # brush, and without flag 2, "only when fired by name" --
            # EntityCook.cpp. A name alone does not stop it being touched.)
            touched = keys.get("model", "").startswith("*") and not (int(keys.get("spawnflags", "0") or 0) & 2)
            if keys.get("classname") == "trigger_changelevel" and there and there != here and touched:
                if os.path.exists(os.path.join(folder, there + ".ent")):
                    graph[here].add(there)
    return graph


def walk(graph, first, allowed):
    seen = {first}
    order = [first]
    here = first
    while True:
        # (The nearest map not been to, by the fewest ways; ties by name.)
        before = {here: None}
        queue = collections.deque([here])
        found = None
        while queue and found is None:
            at = queue.popleft()
            for there in sorted(graph.get(at, ())):
                if there in before or not allowed(there):
                    continue
                before[there] = at
                if there not in seen:
                    found = there
                    break
                queue.append(there)
        if found is None:
            return order
        path = []
        at = found
        while at != here:
            path.append(at)
            at = before[at]
        for at in reversed(path):
            order.append(at)
            seen.add(at)
        here = found


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    graph = ways(argv[1])
    if argv[2] == "--pairs":
        for name in argv[3:]:
            for there in sorted(graph.get(name, ())):
                # In from the neighbour and back out, where it has a way in;
                # else only out, by the map's own way (one way only: the
                # neighbour has none back -- t0a0a to t0a0b).
                if name in graph.get(there, ()):
                    print(there, name, there)
                else:
                    print(name, there)
        return 0
    prefixes = []
    if "--only" in argv:
        prefixes = argv[argv.index("--only") + 1:]
    allowed = lambda m: not prefixes or any(m.startswith(p) for p in prefixes)
    print(" ".join(walk(graph, argv[2], allowed)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
