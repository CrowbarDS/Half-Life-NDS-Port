#!/usr/bin/env python3
"""Hold the port's diagnostic log against the real game's, frame by frame.

    diagcmp.py <name> [--pos 0.5] [--vel 2] [--dir build/ref/diag]

reads <dir>/<name>_pc.csv (tools/ref/diag.sh, the real game) and
<dir>/<name>_ds.csv (diag_replay, the port), says where they part and by how
much, and writes the two side by side, frame by frame, to <dir>/<name>_cmp.csv.

Frames are the script's (both sides are given the same input each frame, at
the same sixtieth of a second). Each side's rows of frame N are taken as they
are: the player after its move N; a box after the move that followed move
N-1 -- the real game logs its boxes before frame N's physics, which moved
them on from move N-1's touch, and the port after its logicTick N, which
does the same. Box speeds are worked out from where they went.
"""
import argparse
import csv
import difflib
import math
import os
import sys


def load(path):
    cols = {}
    rows = {'_who': 'port'}
    with open(path, newline='') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('#diag '):
                rows['_who'] = line.split()[1]
                continue
            if line.startswith('#cols '):
                parts = line.split()
                cols[parts[1]] = parts[2:]
                continue
            if not line or line.startswith('#'):
                continue
            fields = next(csv.reader([line]))
            kind = fields[0]
            if kind not in cols:
                continue
            row = dict(zip(cols[kind], fields[1:]))
            rows.setdefault(kind, []).append(row)
    return rows


def last_per_frame(rows):
    out = {}
    for r in rows:
        out[int(r['frame'])] = r  # a slow frame's last command wins
    return out


def boxes_per_frame(rows):
    out = {}
    for r in rows:
        out.setdefault(r['id'], {})[int(r['frame'])] = r
    return out


def f(r, k):
    return float(r[k])


def spans(frs):
    """Frames as runs: [3, 4, 5, 9] -> '3..5, 9'."""
    out = []
    for fr in frs:
        if out and fr == out[-1][1] + 1:
            out[-1][1] = fr
        else:
            out.append([fr, fr])
    return ', '.join(f'{a}..{b}' if a != b else f'{a}' for a, b in out) or '-'


def ladders(pc, ds, pl_pc, pl_ds, frames, table):
    """The ladder: on it or not (mt 5, the game's fly), which, and what its
    move made of it (the lad rows): the face's normal, solid underfoot, the
    climb speeds the keys asked for."""
    if not frames or 'mt' not in pl_pc[frames[0]] or 'mt' not in pl_ds[frames[0]]:
        return
    on_pc = [fr for fr in frames if pl_pc[fr]['mt'] == '5']
    on_ds = [fr for fr in frames if pl_ds[fr]['mt'] == '5']
    touch_pc = [fr for fr in frames if pl_pc[fr]['lad'] != '-']
    touch_ds = [fr for fr in frames if pl_ds[fr]['lad'] != '-']
    if not (on_pc or on_ds or touch_pc or touch_ds):
        return
    print(' ladder (real game / port):')
    print(f'  {"climbing":<14}frames {spans(on_pc)} / {spans(on_ds)}')
    print(f'  {"touching":<14}frames {spans(touch_pc)} / {spans(touch_ds)}')
    which = [fr for fr in frames if pl_pc[fr]['lad'] != pl_ds[fr]['lad']]
    if which:
        print(f'  {"which":<14}differs on {len(which)} frames, first {which[0]} '
              f'({pl_pc[which[0]]["lad"]} / {pl_ds[which[0]]["lad"]})')
    for fr in frames:
        table[fr]['pc_mt'] = pl_pc[fr]['mt']
        table[fr]['ds_mt'] = pl_ds[fr]['mt']
    lad_pc, lad_ds = last_per_frame(pc.get('lad', [])), last_per_frame(ds.get('lad', []))
    both = [fr for fr in frames if fr in lad_pc and fr in lad_ds]
    if not both:
        return
    hit = [fr for fr in both if lad_pc[fr]['hit'] != lad_ds[fr]['hit']]
    if hit:
        print(f'  {"face found":<14}differs on {len(hit)} frames, first {hit[0]} '
              f'({lad_pc[hit[0]]["hit"]} / {lad_ds[hit[0]]["hit"]})')
    angles = []
    for fr in both:
        if lad_pc[fr]['hit'] != '1' or lad_ds[fr]['hit'] != '1':
            continue
        n_pc = [f(lad_pc[fr], k) for k in ('nx', 'ny', 'nz')]
        n_ds = [f(lad_ds[fr], k) for k in ('nx', 'ny', 'nz')]
        dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(n_pc, n_ds))))
        angles.append((fr, math.degrees(math.acos(dot)), n_pc, n_ds))
        table[fr]['pc_n'] = ' '.join(f'{v:.3f}' for v in n_pc)
        table[fr]['ds_n'] = ' '.join(f'{v:.3f}' for v in n_ds)
    if angles:
        apart = [a for a in angles if a[1] > 1.0]
        if apart:
            fr, deg, n_pc, n_ds = apart[0]
            worst = max(angles, key=lambda a: a[1])
            print(f'  {"normal":<14}apart on {len(apart)} frames, from {fr} by {deg:.1f} deg '
                  f'({" ".join(f"{v:.2f}" for v in n_pc)} / {" ".join(f"{v:.2f}" for v in n_ds)}), '
                  f'worst {worst[1]:.1f} at {worst[0]}')
        else:
            print(f'  {"normal":<14}same within 1 deg on {len(angles)} frames')
    for key in ('onfloor',):
        apart = [fr for fr in both if lad_pc[fr][key] != lad_ds[fr][key]]
        if apart:
            print(f'  {key:<14}differs on {len(apart)} frames: {spans(apart)}')
        else:
            print(f'  {key:<14}same')
    for key in ('fwd', 'right'):
        apart = [fr for fr in both if abs(f(lad_pc[fr], key) - f(lad_ds[fr], key)) > 0.5]
        if apart:
            print(f'  {key:<14}differs on {len(apart)} frames, first {apart[0]} '
                  f'({lad_pc[apart[0]][key]} / {lad_ds[apart[0]][key]})')


def vec(r, keys):
    return [f(r, k) for k in keys]


def angle(a, b):
    """Degrees between two vectors (0 if either is nothing)."""
    la, lb = math.hypot(*a), math.hypot(*b)
    if la == 0 or lb == 0:
        return 0.0
    dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b)) / (la * lb)))
    return math.degrees(math.acos(dot))


def weapons(pc, ds, frames):
    """The weapon in hand (the wpn rows): when each is taken out, and its
    clip and ammo."""
    w_pc, w_ds = last_per_frame(pc.get('wpn', [])), last_per_frame(ds.get('wpn', []))
    both = [fr for fr in frames if fr in w_pc and fr in w_ds]
    if not both:
        return
    print(' weapon (real game / port):')
    for side, w in (('real game', w_pc), ('port', w_ds)):
        held, last = [], None
        for fr in frames:
            if fr in w and w[fr]['cls'] != last:
                last = w[fr]['cls']
                held.append(f'{last} {fr}')
        print(f'  {"in hand, " + side:<24}{", ".join(held)}')
    for key in ('clip', 'ammo1', 'ammo2'):
        apart = [fr for fr in both if w_pc[fr]['cls'] == w_ds[fr]['cls'] and w_pc[fr][key] != w_ds[fr][key]]
        if apart:
            print(f'  {key:<14}differs on {len(apart)} frames: {spans(apart)} '
                  f'(first {w_pc[apart[0]][key]} / {w_ds[apart[0]][key]})')
        else:
            print(f'  {key:<14}same')


def shots(pc, ds, pos):
    """The rounds fired (the shot rows), volley by volley: when, from
    where, aimed which way, the cone and the spread drawn, what was hit."""
    def volleys(rows):
        out = {}
        for r in rows:
            out.setdefault(int(r['frame']), []).append(r)
        return [out[fr] for fr in sorted(out)]
    v_pc, v_ds = volleys(pc.get('shot', [])), volleys(ds.get('shot', []))
    if not v_pc and not v_ds:
        return
    print(' shots (real game / port):')
    print(f'  {"volleys":<14}{len(v_pc)} / {len(v_ds)}, frames {" ".join(v[0]["frame"] for v in v_pc) or "-"}'
          f' / {" ".join(v[0]["frame"] for v in v_ds) or "-"}')
    print(f'  {"rounds each":<14}{" ".join(str(len(v)) for v in v_pc) or "-"} / {" ".join(str(len(v)) for v in v_ds) or "-"}')
    pairs = list(zip(v_pc, v_ds))
    if not pairs:
        return
    src = max(math.dist(vec(p[0], ('sx', 'sy', 'sz')), vec(d[0], ('sx', 'sy', 'sz'))) for p, d in pairs)
    aim = max(angle(vec(p[0], ('ax', 'ay', 'az')), vec(d[0], ('ax', 'ay', 'az'))) for p, d in pairs)
    print(f'  {"from":<14}{"same within " + format(pos, "g") + " u" if src <= pos else f"apart by up to {src:.3f} u"}')
    print(f'  {"aimed":<14}{"same within 0.1 deg" if aim <= 0.1 else f"apart by up to {aim:.2f} deg"}')
    cones = sorted({(p[0]['cx'], d[0]['cx']) for p, d in pairs})
    print(f'  {"cone":<14}{", ".join(f"{float(c):.4f} / {float(e):.4f}" for c, e in cones)}')
    rounds = [(p, d) for vp, vd in pairs for p, d in zip(vp, vd)]
    # (The spread is drawn from the command's seed: only rounds fired on the
    # same frame on both sides can be drawn the same.)
    level = [(p, d) for p, d in rounds if p['frame'] == d['frame']]
    late = len([1 for vp, vd in pairs if vp[0]['frame'] != vd[0]['frame']])
    if level:
        draws = max(max(abs(f(p, 'x') - f(d, 'x')), abs(f(p, 'y') - f(d, 'y'))) for p, d in level)
        said = 'the same' if draws < 0.002 else f'apart by up to {draws:.3f} (of -1..1)'
        print(f'  {"spread drawn":<14}{said} on the {len(level)} rounds fired on the same frame'
              + (f' ({late} volley{"s" if late > 1 else ""} on another)' if late else ''))
    for side, rs in (('real game', [p for p, _ in rounds]), ('port', [d for _, d in rounds])):
        n = len(rs)
        mx = sum(abs(f(r, 'x')) for r in rs) / n
        my = sum(abs(f(r, 'y')) for r in rs) / n
        print(f'  {"  " + side:<14}mean |x| {mx:.3f}, |y| {my:.3f} over {n}')
    if level:
        ways = max(angle([f(p, 'ex') - f(p, 'sx'), f(p, 'ey') - f(p, 'sy'), f(p, 'ez') - f(p, 'sz')],
                         [f(d, 'ex') - f(d, 'sx'), f(d, 'ey') - f(d, 'sy'), f(d, 'ez') - f(d, 'sz')]) for p, d in level)
        print(f'  {"way gone":<14}apart by up to {ways:.3f} deg on those')
    # (A monster is its model in the game's rows, its class in the port's:
    # models/headcrab.mdl and monster_headcrab are the one thing.)
    def what(name):
        if name.startswith('models/') and name.endswith('.mdl'):
            return 'monster ' + name[len('models/'):-len('.mdl')]
        if name.startswith('monster_'):
            return 'monster ' + name[len('monster_'):]
        return name
    hits = [(p['hit'], d['hit'], p['frame']) for p, d in rounds if what(p['hit']) != what(d['hit'])]
    flesh = sum(1 for p, d in rounds if what(p['hit']).startswith('monster ') and what(p['hit']) == what(d['hit']))
    print(f'  {"hit":<14}{"same" if not hits else f"differs on {len(hits)} rounds (first {hits[0][0]} / {hits[0][1]} at frame {hits[0][2]})"}'
          + (f'; {flesh} in a monster on both' if flesh else ''))


def flights(rows):
    """Each thing thrown or fired, by its id and the frame it appeared: a
    list of its rows, frame by frame."""
    out, seen = [], {}
    for r in rows:
        key, fr = r['id'], int(r['frame'])
        cur = seen.get(key)
        if cur is None or fr > int(cur[-1]['frame']) + 1:
            cur = []
            seen[key] = cur
            out.append(cur)
        if int(cur[-1]['frame']) == fr if cur else False:
            cur[-1] = r
        else:
            cur.append(r)
    return out


def projectiles(pc, ds, pos, vel, table):
    """What was thrown or fired (the proj rows), paired by kind in the order
    they appeared, and compared row by row from their first (each side's
    first row is where it was let go). Then the blasts, in order."""
    f_pc, f_ds = flights(pc.get('proj', [])), flights(ds.get('proj', []))
    if f_pc or f_ds:
        kinds = []
        for fl in f_pc + f_ds:
            k = (fl[0]['cls'], fl[0]['sub'])
            if k not in kinds:
                kinds.append(k)
        for cls, sub in kinds:
            a = [fl for fl in f_pc if (fl[0]['cls'], fl[0]['sub']) == (cls, sub)]
            b = [fl for fl in f_ds if (fl[0]['cls'], fl[0]['sub']) == (cls, sub)]
            print(f' {cls} {sub} (real game / port): {len(a)} / {len(b)}')
            for i, (p, d) in enumerate(zip(a, b)):
                tag = f'{cls}{i + 1}'
                print(f'  #{i + 1:<13}from frame {p[0]["frame"]} / {d[0]["frame"]}, last {p[-1]["frame"]} / {d[-1]["frame"]}')
                n = min(len(p), len(d))
                at = math.dist(vec(p[0], ('ox', 'oy', 'oz')), vec(d[0], ('ox', 'oy', 'oz')))
                sp = math.dist(vec(p[0], ('vx', 'vy', 'vz')), vec(d[0], ('vx', 'vy', 'vz')))
                print(f'  {"let go":<14}{at:.3f} u apart, speed {sp:.3f} u/s apart '
                      f'({" ".join(p[0][k] for k in ("vx", "vy", "vz"))} / {" ".join(d[0][k] for k in ("vx", "vy", "vz"))})')
                apart = next((k for k in range(n) if math.dist(vec(p[k], ('ox', 'oy', 'oz')), vec(d[k], ('ox', 'oy', 'oz'))) > pos), None)
                if apart is None:
                    print(f'  {"path":<14}same within {pos:g} u for {n} frames')
                else:
                    print(f'  {"path":<14}apart from its frame {apart} (frame {p[apart]["frame"]} / {d[apart]["frame"]}): '
                          f'{" ".join(p[apart][k] for k in ("ox", "oy", "oz"))} / {" ".join(d[apart][k] for k in ("ox", "oy", "oz"))}')
                def rest(fl):
                    # (Where it stops going anywhere: a bolt in a wall keeps
                    # its speed on the port's side.)
                    for k in range(1, len(fl)):
                        if all(math.dist(vec(fl[j], ('ox', 'oy', 'oz')), vec(fl[k], ('ox', 'oy', 'oz'))) < 0.01
                               for j in range(k, len(fl))):
                            return fl[k]
                    return None
                rp, rd = rest(p), rest(d)
                if rp or rd:
                    def where(r):
                        return f'frame {r["frame"]} at {" ".join(r[k] for k in ("ox", "oy", "oz"))}' if r else 'never'
                    print(f'  {"at rest":<14}{where(rp)} / {where(rd)}')
                for k in range(n):
                    fr = int(p[0]['frame']) + k
                    if fr in table:
                        for key in ('ox', 'oy', 'oz'):
                            table[fr][f'pc_{tag}_{key}'] = p[k][key]
                            table[fr][f'ds_{tag}_{key}'] = d[k][key]
    b_pc, b_ds = pc.get('boom', []), ds.get('boom', [])
    if b_pc or b_ds:
        print(f' blasts (real game / port): {len(b_pc)} / {len(b_ds)}')
        for i, (p, d) in enumerate(zip(b_pc, b_ds)):
            at = math.dist(vec(p, ('x', 'y', 'z')), vec(d, ('x', 'y', 'z')))
            print(f'  #{i + 1:<13}frame {p["frame"]} / {d["frame"]}, {at:.3f} u apart '
                  f'({p["x"]} {p["y"]} {p["z"]} / {d["x"]} {d["y"]} {d["z"]}), '
                  f'damage {float(p["damage"]):g} / {float(d["damage"]):g}, radius {float(p["radius"]):g} / {float(d["radius"]):g}')


def turning(pc, ds):
    """The turning brushes (the rot rows): how far apart their angles are.
    The port's row of a frame is after its turn that frame, the game's
    before: the game's frame N + 1 is held against the port's N."""
    r_pc, r_ds = boxes_per_frame(pc.get('rot', [])), boxes_per_frame(ds.get('rot', []))
    for rid in sorted(set(r_pc) & set(r_ds)):
        a, b = r_pc[rid], r_ds[rid]
        worst, at, n = 0.0, None, 0
        for fr in sorted(b):
            if fr + 1 not in a:
                continue
            n += 1
            for k in ('ax', 'ay', 'az'):
                d = (f(a[fr + 1], k) - f(b[fr], k) + 180.0) % 360.0 - 180.0
                if abs(d) > worst:
                    worst, at = abs(d), fr
        speed = f(a[min(a)], 'wx') or f(a[min(a)], 'wy') or f(a[min(a)], 'wz')
        print(f' turning {rid} (real game / port): angle {"same within 0.5 deg" if worst <= 0.5 else f"apart by up to {worst:.2f} deg, at {at}"}'
              f' over {n} frames; turning at {speed:g} deg/s')


def hurts(pc, ds):
    """The player hurt (the dmg rows), in order: how much, and the push."""
    h_pc, h_ds = pc.get('dmg', []), ds.get('dmg', [])
    if not h_pc and not h_ds:
        return
    print(f' player hurt (real game / port): {len(h_pc)} / {len(h_ds)}')
    for i in range(max(len(h_pc), len(h_ds))):
        p = h_pc[i] if i < len(h_pc) else None
        d = h_ds[i] if i < len(h_ds) else None
        def said(r):
            if r is None:
                return '-'
            push = [f(r, k) - f(r, k + '0') for k in ('vx', 'vy', 'vz')]
            return (f'frame {r["frame"]} {float(r["amount"]):g} of type {r["type"]}, '
                    f'health {float(r["hp0"]):g}->{float(r["hp"]):g}, armour {float(r["ap0"]):g}->{float(r["ap"]):g}, '
                    f'push {" ".join(f"{v:.1f}" for v in push)}')
        print(f'  #{i + 1:<13}{said(p)}')
        print(f'  {"":<14}{said(d)}')


# The game's decal names (without "{" and number) as the port's families.
MARK_FAMILY = {'smscorch': 'scorch', 'bproof': 'break', 'lambda': 'sign', 'mommablob': 'yblood',
               'gargstomp': 'wall'}


def marks(pc, ds):
    """The marks left (the decal rows), by family: as many, the same places
    and walls. The game's player bullets are marked by its client, not its
    server: their marks are taken from the shot rows that stopped on the
    world or a brush. Pictures and turns are not compared (each side draws
    its own randoms)."""
    m_pc = [dict(r, family=MARK_FAMILY.get(r['family'], r['family'])) for r in pc.get('decal', [])]
    for r in pc.get('shot', []):
        if float(r['frac']) < 1.0 and (r['hit'] == 'world' or r['hit'].startswith('*')):
            m_pc.append({'frame': r['frame'], 'x': r['ex'], 'y': r['ey'], 'z': r['ez'], 'nx': 'nan', 'ny': 'nan',
                         'nz': 'nan', 'family': 'shot', 'on': r['hit']})
    # (A port's mark that took one of its family's place -- a row whose
    # name ends in "m" -- is still one left there.)
    m_ds = ds.get('decal', [])
    moved = sum(1 for r in m_ds if r['name'].endswith('m'))
    if not m_pc and not m_ds:
        return
    print(f' marks (real game / port): {len(m_pc)} / {len(m_ds)}' + (f' ({moved} of the port\'s in the place of one before)' if moved else ''))
    for fam in sorted({r['family'] for r in m_pc} | {r['family'] for r in m_ds}):
        a = [r for r in m_pc if r['family'] == fam]
        b = [r for r in m_ds if r['family'] == fam]
        left, pairs = list(b), []
        for p in a:
            best = None
            for d in left:
                if abs(int(d['frame']) - int(p['frame'])) > 6:
                    continue
                gap = math.dist(vec(p, ('x', 'y', 'z')), vec(d, ('x', 'y', 'z')))
                if best is None or gap < best[0]:
                    best = (gap, d)
            if best is not None and best[0] < 64:
                left.remove(best[1])
                pairs.append((p, best[1], best[0]))
        line = f'  {fam:<14}{len(a)} / {len(b)}'
        if pairs:
            gaps = [g for _, _, g in pairs]
            turns = [angle(vec(p, ('nx', 'ny', 'nz')), vec(d, ('nx', 'ny', 'nz'))) for p, d, _ in pairs
                     if p['nx'] != 'nan']
            line += f', {len(pairs)} paired: {sum(gaps) / len(gaps):.2f} u apart on average, at most {max(gaps):.2f}'
            if turns:
                line += f'; walls up to {max(turns):.1f} deg apart'
            walls = sum(1 for p, d, _ in pairs if (p['on'] == 'world') != (d['on'] == 'world'))
            if walls:
                line += f'; {walls} on a brush on one side only'
        print(line)
        for p in [r for r in a if all(r is not q for q, _, _ in pairs)][:4]:
            print(f'  {"":<14}game only: frame {p["frame"]} at {p["x"]} {p["y"]} {p["z"]} on {p["on"]}')
        for d in left[:4]:
            print(f'  {"":<14}port only: frame {d["frame"]} at {d["x"]} {d["y"]} {d["z"]} on {d["on"]}')


def lamps(pc, ds, pl_pc, pl_ds, frames):
    """The flashlight: on or off, its battery (the pl rows), and while it is
    on where its light sits (the lamp rows): how far apart, and whether the
    line stopped on the same kind of thing (the world or a brush, or a
    monster)."""
    if not any('lamp' in pl_pc[fr] and pl_pc[fr]['lamp'] == '1' for fr in frames) and not pc.get('lamp'):
        return
    on = [fr for fr in frames if pl_pc[fr].get('lamp') != pl_ds[fr].get('lamp')]
    bat = [fr for fr in frames if pl_pc[fr].get('bat') != pl_ds[fr].get('bat')]
    print(' lamp (real game / port):')
    print(f'  {"on":<14}' + (f'differs on {len(on)} frames, first {on[0]}' if on else 'same'))
    if bat:
        worst = max(bat, key=lambda fr: abs(int(pl_pc[fr]['bat']) - int(pl_ds[fr]['bat'])))
        print(f'  {"battery":<14}differs on {len(bat)} frames, first {bat[0]} '
              f'({pl_pc[bat[0]]["bat"]} / {pl_ds[bat[0]]["bat"]}), worst at {worst} '
              f'({pl_pc[worst]["bat"]} / {pl_ds[worst]["bat"]})')
    else:
        print(f'  {"battery":<14}same')
    l_pc, l_ds = last_per_frame(pc.get('lamp', [])), last_per_frame(ds.get('lamp', []))
    both = sorted(set(l_pc) & set(l_ds))
    if not both:
        print(f'  {"light":<14}no frame lit on both sides ({len(l_pc)} / {len(l_ds)})')
        return
    gaps = [(math.dist(vec(l_pc[fr], ('lx', 'ly', 'lz')), vec(l_ds[fr], ('lx', 'ly', 'lz'))), fr) for fr in both]
    worst = max(gaps)
    near = sum(1 for g, _ in gaps if g <= 8)
    def kind(r):
        return '-' if r['hit'] == '-' else ('model' if r['hit'].startswith('models/') else 'world')
    kinds = [fr for fr in both if kind(l_pc[fr]) != kind(l_ds[fr])]
    print(f'  {"light":<14}{len(both)} frames lit on both: {near} within 8 u; worst {worst[0]:.1f} u at {worst[1]} '
          f'({l_pc[worst[1]]["hit"]} / {l_ds[worst[1]]["hit"]})')
    if kinds:
        print(f'  {"stopped on":<14}differs on {len(kinds)} frames, first {kinds[0]} '
              f'({l_pc[kinds[0]]["hit"]} / {l_ds[kinds[0]]["hit"]})')


def monsters(pc, ds, pos):
    """The monsters, each paired with the port's of its class that started
    nearest (mstart rows): their AI's events as two sequences, aligned in
    order (a frame or so apart is the same event: the game's monsters think
    after the player's move, the port's before it), the first that differs;
    and where they are at each think, on the frames both thought."""
    starts_pc, starts_ds = pc.get('mstart', []), ds.get('mstart', [])
    if not starts_pc or not starts_ds:
        return
    pairs, used = [], set()
    for m in starts_pc:
        best, best_d = None, 64.0
        for n in starts_ds:
            if n['id'] in used or n['cls'] != m['cls']:
                continue
            d = math.dist(vec(m, ('sx', 'sy', 'sz')), vec(n, ('sx', 'sy', 'sz')))
            if d < best_d:
                best, best_d = n, d
        if best is not None:
            used.add(best['id'])
            pairs.append((m, best))

    def events(rows, mid):
        # (A monster's thinking: its schedules, tasks, failures, and the
        # events of its clips, without the sounds -- 1000 and over -- that
        # the port plays apart from them.)
        out = []
        for r in rows:
            if r['id'] != mid or r['what'] not in ('sched', 'task', 'fail', 'anim'):
                continue
            if r['what'] == 'anim' and int(r['a']) >= 1000:
                continue
            key = r['what'] + ' ' + (r['name'] if r['what'] in ('sched', 'fail') else r['a'])
            out.append((int(r['frame']), key))
        return out

    for m, n in pairs:
        ev_pc, ev_ds = events(pc.get('aiev', []), m['id']), events(ds.get('aiev', []), n['id'])
        print(f' {m["cls"]} {m["id"]} / {n["id"]} (real game / port): {len(ev_pc)} / {len(ev_ds)} events')
        # (Aligned as a diff: an event one side has and the other has not --
        # often a wait begun before the script, at a moment the two sides do
        # not share -- leaves the rest paired.)
        ops = difflib.SequenceMatcher(None, [e[1] for e in ev_pc], [e[1] for e in ev_ds], autojunk=False).get_opcodes()
        late = [ev_ds[j1 + i][0] - ev_pc[i1 + i][0] for tag, i1, i2, j1, j2 in ops if tag == 'equal' for i in range(i2 - i1)]
        if late:
            print(f'  {"paired":<14}{len(late)}, the port later by {min(late)}..{max(late)} frames'
                  f' (mostly {max(set(late), key=late.count)})')
        apart = [(tag, i1, i2, j1, j2) for tag, i1, i2, j1, j2 in ops if tag != 'equal']
        for tag, i1, i2, j1, j2 in apart[:4]:
            def show(ev, a, b):
                if a == b:
                    return 'none'
                more = f' (+{b - a - 1})' if b - a > 1 else ''
                return f'frame {ev[a][0]} {ev[a][1]}{more}'
            print(f'  {"apart":<14}{show(ev_pc, i1, i2)} / {show(ev_ds, j1, j2)}')
        if len(apart) > 4:
            print(f'  {"":<14}... and {len(apart) - 4} more')
        if not apart:
            print(f'  {"apart":<14}none')
        # (Its thinks taken in order: the game's float time makes one now
        # and then seven frames after the last, not six.)
        th_pc = [r for r in pc.get('mon', []) if r['id'] == m['id']]
        th_ds = [r for r in ds.get('mon', []) if r['id'] == n['id']]
        k = min(len(th_pc), len(th_ds))
        offset = [int(th_ds[i]['frame']) - int(th_pc[i]['frame']) for i in range(k)]
        print(f'  {"thinks":<14}{len(th_pc)} / {len(th_ds)}, frames apart {min(offset, default=0)}..{max(offset, default=0)}')
        far = [(int(th_pc[i]['frame']), math.dist(vec(th_pc[i], ('ox', 'oy', 'oz')), vec(th_ds[i], ('ox', 'oy', 'oz'))))
               for i in range(k)]
        off = [x for x in far if x[1] > pos]
        if off:
            worst = max(far, key=lambda x: x[1])
            print(f'  {"where":<14}apart from think at frame {off[0][0]} ({off[0][1]:.2f} u), worst {worst[1]:.2f} at {worst[0]}')
        elif k:
            print(f'  {"where":<14}same within {pos:g} u')
        draws_pc = sum(1 for r in pc.get('rnd', []) if r['id'] == m['id'])
        draws_ds = sum(1 for r in ds.get('rnd', []) if r['id'] == n['id'])
        print(f'  {"random draws":<14}{draws_pc} / {draws_ds}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('--pos', type=float, default=0.5, help='units apart that count')
    ap.add_argument('--vel', type=float, default=2.0, help='units/s apart that count')
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument('--dir', default=os.path.join(root, 'build', 'ref', 'diag'))
    a = ap.parse_args()
    pc = load(os.path.join(a.dir, a.name + '_pc.csv'))
    ds = load(os.path.join(a.dir, a.name + '_ds.csv'))

    pl_pc, pl_ds = last_per_frame(pc.get('pl', [])), last_per_frame(ds.get('pl', []))
    frames = sorted(set(pl_pc) & set(pl_ds))
    if not frames:
        print('no frames in common')
        return 1
    print(f'{a.name}: {len(frames)} frames (real game {len(pl_pc)}, port {len(pl_ds)})')

    table = {fr: {'frame': fr} for fr in frames}

    def report(label, series, tol, unit):
        """series: [(frame, pc, ds)] -> one line: first frame apart, worst, last."""
        if not series:
            return
        first = next(((fr, p, d) for fr, p, d in series if abs(p - d) > tol), None)
        worst = max(series, key=lambda s: abs(s[1] - s[2]))
        fr, p, d = series[-1]
        line = f'  {label:<14}'
        if first is None:
            line += f'same within {tol:g} {unit}'
        else:
            line += (f'apart from frame {first[0]} ({first[1]:.3f} / {first[2]:.3f}), '
                     f'worst {abs(worst[1] - worst[2]):.3f} at {worst[0]}')
        line += f'; at the end {p:.3f} / {d:.3f}'
        print(line)

    print(' player (real game / port):')
    for key, tol, unit in (('ox', a.pos, 'u'), ('oy', a.pos, 'u'), ('oz', a.pos, 'u'),
                           ('vx', a.vel, 'u/s'), ('vy', a.vel, 'u/s'), ('vz', a.vel, 'u/s')):
        series = [(fr, f(pl_pc[fr], key), f(pl_ds[fr], key)) for fr in frames]
        report(key, series, tol, unit)
        for fr, p, d in series:
            table[fr]['pc_' + key] = p
            table[fr]['ds_' + key] = d
    for key in ('og', 'duck'):
        apart = [fr for fr in frames if pl_pc[fr][key] != pl_ds[fr][key]]
        if apart:
            print(f'  {key:<14}differs on {len(apart)} frames, first {apart[0]} '
                  f'({pl_pc[apart[0]][key]} / {pl_ds[apart[0]][key]})')
        else:
            print(f'  {key:<14}same')

    ladders(pc, ds, pl_pc, pl_ds, frames, table)
    weapons(pc, ds, frames)
    shots(pc, ds, a.pos)
    projectiles(pc, ds, a.pos, a.vel, table)
    hurts(pc, ds)
    marks(pc, ds)
    lamps(pc, ds, pl_pc, pl_ds, frames)
    turning(pc, ds)
    monsters(pc, ds, a.pos)

    box_pc, box_ds = boxes_per_frame(pc.get('box', [])), boxes_per_frame(ds.get('box', []))
    moved = []
    for bid in sorted(set(box_pc) & set(box_ds)):
        bp, bd = box_pc[bid], box_ds[bid]
        series = {}
        for fr in frames:
            if fr in bp and fr in bd:
                series[fr] = (bp[fr], bd[fr])
        if not series:
            continue
        start_pc, start_ds = series[min(series)]
        went = any(math.dist([f(p, 'cx'), f(p, 'cy'), f(p, 'cz')], [f(start_pc, 'cx'), f(start_pc, 'cy'), f(start_pc, 'cz')]) > 0.1
                   or math.dist([f(d, 'cx'), f(d, 'cy'), f(d, 'cz')], [f(start_ds, 'cx'), f(start_ds, 'cy'), f(start_ds, 'cz')]) > 0.1
                   for p, d in series.values())
        rest = [abs(f(start_pc, k) - f(start_ds, k)) for k in ('cx', 'cy', 'cz')]
        if not went:
            if max(rest) > a.pos:
                print(f' box {bid}: still on both sides, {max(rest):.3f} apart')
            continue
        moved.append(bid)
        print(f' box {bid} (real game / port):')
        keys = sorted(series)
        for key in ('cx', 'cy', 'cz'):
            s = [(fr, f(series[fr][0], key), f(series[fr][1], key)) for fr in keys]
            report(key, s, a.pos, 'u')
            for fr, p, d in s:
                table[fr][f'pc_{bid}_{key}'] = p
                table[fr][f'ds_{bid}_{key}'] = d
        # Speeds from where it went, frame to frame.
        sp = []
        for prev, fr in zip(keys, keys[1:]):
            dt = 1 / 60
            vp = math.hypot(f(series[fr][0], 'cx') - f(series[prev][0], 'cx'),
                            f(series[fr][0], 'cy') - f(series[prev][0], 'cy')) / dt
            vd = math.hypot(f(series[fr][1], 'cx') - f(series[prev][1], 'cx'),
                            f(series[fr][1], 'cy') - f(series[prev][1], 'cy')) / dt
            sp.append((fr, vp, vd))
            table[fr][f'pc_{bid}_speed'] = vp
            table[fr][f'ds_{bid}_speed'] = vd
        report('speed', sp, a.vel, 'u/s')
        moving_pc = [fr for fr, vp, _ in sp if vp > 0.5]
        moving_ds = [fr for fr, _, vd in sp if vd > 0.5]
        if moving_pc or moving_ds:
            print(f'  {"moving":<14}frames {moving_pc[0] if moving_pc else "-"}..{moving_pc[-1] if moving_pc else "-"}'
                  f' / {moving_ds[0] if moving_ds else "-"}..{moving_ds[-1] if moving_ds else "-"}')

    for side, rows in (('real game', pc), ('port', ds)):
        touches = rows.get('touch', [])
        if not touches:
            continue
        by = {}
        for t in touches:
            by.setdefault((t['id'], t['result']), []).append(int(t['frame']))
        for (bid, res), frs in sorted(by.items()):
            print(f' touches, {side}: {bid} {res}: {len(frs)}, frames {frs[0]}..{frs[-1]}')

    out = os.path.join(a.dir, a.name + '_cmp.csv')
    names = ['frame'] + sorted({k for row in table.values() for k in row if k != 'frame'})
    with open(out, 'w', newline='') as fo:
        w = csv.DictWriter(fo, fieldnames=names)
        w.writeheader()
        for fr in frames:
            w.writerow(table[fr])
    print(f' -> {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
