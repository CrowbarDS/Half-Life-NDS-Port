#!/usr/bin/env python3
"""Hold the port's diagnostic log against the real game's, frame by frame.

    diagcmp.py <name> [--pos 0.5] [--vel 2] [--dir build/ref/diag]

reads <dir>/<name>_pc.csv (tools/ref/diag.sh, the real game) and
<dir>/<name>_ds.csv (diag_replay, the port), says where they part and by how
much, and writes the two side by side, frame by frame, to <dir>/<name>_cmp.csv.

Frames are the script's (both sides are given the same input each frame, at
the same sixtieth of a second). Where each side's frame ends:
  - the player: its "pl" row of the frame (after its move);
  - a box: the real game moves its boxes after the player, so a box where the
    real game's frame N left it is its "box" row of frame N+1 (logged before
    that frame's physics); the port moves them first, so it is its row of
    frame N. (Which a log is, its "#diag pc|port" line says: two of the same
    kind can be held against each other too.) Box speeds are worked out from where they went.
"""
import argparse
import csv
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

    box_pc, box_ds = boxes_per_frame(pc.get('box', [])), boxes_per_frame(ds.get('box', []))
    # (Each log's box rows as its own kind of log has them: see the top.)
    lag_pc = 1 if pc['_who'] == 'pc' else 0
    lag_ds = 1 if ds['_who'] == 'pc' else 0
    moved = []
    for bid in sorted(set(box_pc) & set(box_ds)):
        bp, bd = box_pc[bid], box_ds[bid]
        series = {}
        for fr in frames:
            if fr + lag_pc in bp and fr + lag_ds in bd:
                series[fr] = (bp[fr + lag_pc], bd[fr + lag_ds])
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
