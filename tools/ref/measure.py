#!/usr/bin/env python3
"""measure.py [--delta] <name> <map> "<x y z yaw>" ["<x y z yaw>" ...]

How bright the real game and the port each draw the same views, as
numbers: both are photographed at each place (tools/ref/compare.sh), each
picture is cut into a grid of patches, and the mean colour of every patch
on both sides goes into build/ref/measure_<name>.csv -- with, at the end, a
summary: how much brighter or darker the port is, dark patches and light
ones apart.

The port must be on the card with <map> as its start map. The bottom of the
picture (the HUD) and its right-hand third (the weapon) are left out.

--delta: the light the flashlight adds. Each place is photographed with the
lamp off, then on (key e: bound to impulse 100 in the real game, Select in
the port), on a finer grid; what each patch gains is set beside the other
side's, in build/ref/measure_<name>.csv, with a summary: in the pool (where
the game adds light) and outside it (where it adds none).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
COLS, ROWS = 4, 3


def patches(path, crop, size, cols=COLS, rows=ROWS):
    """Mean R, G, B of each patch of the picture's upper left two thirds."""
    w, h = size
    raw = subprocess.run(['convert', path, '-crop', crop, '+repage', '-resize', '%dx%d!' % (w, h), '-depth', '8',
                          'rgb:-'], capture_output=True, check=True).stdout
    used_w, used_h = w * 2 // 3, h * 3 // 4
    out = []
    for row in range(rows):
        for col in range(cols):
            x0, x1 = used_w * col // cols, used_w * (col + 1) // cols
            y0, y1 = used_h * row // rows, used_h * (row + 1) // rows
            total, n = [0, 0, 0], 0
            for y in range(y0, y1):
                base = (y * w + x0) * 3
                line = raw[base:base + (x1 - x0) * 3]
                for c in range(3):
                    total[c] += sum(line[c::3])
                n += x1 - x0
            out.append([t / n for t in total])
    return out


def luma(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def delta(name, game_map, spots):
    """The lamp's added light, real game against port, patch by patch."""
    rows = []
    for index, spot in enumerate(spots):
        tag = '%s_%02d' % (name, index)
        done = subprocess.run([os.path.join(ROOT, 'tools/ref/compare.sh'), tag, game_map, spot,
                               'sleep %s; shot; key e 0.15; sleep 1; shot' % os.environ.get('DELTA_WAIT', '2')],
                              check=False, stdout=subprocess.DEVNULL)
        if done.returncode == 3:
            sys.exit('the real game did not start: nothing measured (is Steam running?)')
        shots = [os.path.join(ROOT, 'build/ref', tag, '%s_%02d.png' % (side, n)) for side in ('real', 'nds')
                 for n in (0, 1)]
        if not all(os.path.exists(p) for p in shots):
            print('no pictures for', spot, file=sys.stderr)
            continue
        grids = [patches(p, '640x480+80+60' if 'real_' in p else '256x192+0+55', (256, 192), 8, 6) for p in shots]
        for patch in range(len(grids[0])):
            ro, rn, no, nn = (luma(g[patch]) for g in grids)
            rows.append((spot, patch, ro, rn, no, nn))
    out = os.path.join(ROOT, 'build/ref', 'measure_%s.csv' % name)
    with open(out, 'w') as f:
        f.write('spot,patch,real_off,real_on,nds_off,nds_on,real_add,nds_add\n')
        for spot, patch, ro, rn, no, nn in rows:
            f.write('"%s",%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n' % (spot, patch, ro, rn, no, nn, rn - ro, nn - no))
    print('%s: %d patches at %d places' % (name, len(rows), len(spots)))
    pool = [(rn - ro, nn - no) for _, _, ro, rn, no, nn in rows if rn - ro > 4]
    rest = [(rn - ro, nn - no) for _, _, ro, rn, no, nn in rows if rn - ro <= 4]
    if pool:
        real = sum(a for a, _ in pool) / len(pool)
        port = sum(b for _, b in pool) / len(pool)
        off = sum(abs(a - b) for a, b in pool) / len(pool)
        print('  in the pool: %3d patches, the game adds %5.1f, the port %5.1f (port/real %.2f), apart by %.1f a patch' %
              (len(pool), real, port, port / real if real else 0, off))
    if rest:
        print('  outside it:  %3d patches, the game adds %5.1f, the port %5.1f' %
              (len(rest), sum(a for a, _ in rest) / len(rest), sum(b for _, b in rest) / len(rest)))
    print(out)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--delta':
        delta(sys.argv[2], sys.argv[3], sys.argv[4:])
        return
    name, game_map, spots = sys.argv[1], sys.argv[2], sys.argv[3:]
    rows = []
    for index, spot in enumerate(spots):
        tag = '%s_%02d' % (name, index)
        done = subprocess.run([os.path.join(ROOT, 'tools/ref/compare.sh'), tag, game_map, spot, 'sleep 0.5; shot'],
                              check=False, stdout=subprocess.DEVNULL)
        if done.returncode == 3:
            sys.exit('the real game did not start: nothing measured (is Steam running?)')
        real = os.path.join(ROOT, 'build/ref', tag, 'real_00.png')
        nds = os.path.join(ROOT, 'build/ref', tag, 'nds_00.png')
        if not (os.path.exists(real) and os.path.exists(nds)):
            print('no pictures for', spot, file=sys.stderr)
            continue
        a = patches(real, '640x480+80+60', (256, 192))
        b = patches(nds, '256x192+0+55', (256, 192))
        for patch, (p, q) in enumerate(zip(a, b)):
            rows.append((spot, patch, p, q))
    out = os.path.join(ROOT, 'build/ref', 'measure_%s.csv' % name)
    with open(out, 'w') as f:
        f.write('spot,patch,real_r,real_g,real_b,nds_r,nds_g,nds_b,real_luma,nds_luma\n')
        for spot, patch, p, q in rows:
            f.write('"%s",%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n' % (spot, patch, *p, *q, luma(p), luma(q)))
    # The summary: by how bright the real game's patch is.
    bands = [(0, 24), (24, 48), (48, 80), (80, 128), (128, 256)]
    print('%s: %d patches at %d places' % (name, len(rows), len(spots)))
    for low, high in bands:
        pairs = [(luma(p), luma(q)) for _, _, p, q in rows if low <= luma(p) < high]
        if not pairs:
            continue
        real = sum(p for p, _ in pairs) / len(pairs)
        port = sum(q for _, q in pairs) / len(pairs)
        print('  real %3d..%3d: %3d patches, real %5.1f, port %5.1f (port/real %.2f)' %
              (low, high, len(pairs), real, port, port / real if real else 0))
    print(out)


if __name__ == '__main__':
    main()
