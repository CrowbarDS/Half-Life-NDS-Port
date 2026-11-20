#!/usr/bin/env python3
"""The BlocksDS the game is built with, and a check that it is the one in use.

The version is docker/versions.env's BLOCKSDS_VERSION, which the build image is
made from as well. A newer libnds can change what the program counts on (its
threads, the FIFO's handlers, the DTCM layout), so a build with another one
stops here, saying so, rather than making a ROM that fails on the console.

  blocksds_pin.py                print the version wanted
  blocksds_pin.py --check DIR    exit 1 unless DIR is a BlocksDS of that version
  blocksds_pin.py --pick DIR...  print the first DIR that is (exit 1 if none)

HLDS_BLOCKSDS_ANY=1 lets any BlocksDS through (for trying a newer one).
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def wanted():
    with open(os.path.join(ROOT, 'docker', 'versions.env'), encoding='utf-8') as f:
        for line in f:
            key, _, value = line.strip().partition('=')
            if key == 'BLOCKSDS_VERSION':
                return value
    raise SystemExit('blocksds_pin: no BLOCKSDS_VERSION in docker/versions.env')


def version_of(path):
    try:
        with open(os.path.join(path, 'version.txt'), encoding='utf-8') as f:
            return f.read().strip()
    except OSError:
        return None


def is_pinned(path):
    return bool(path) and version_of(path) == wanted()


def check(path):
    """Stops the build unless `path` (BLOCKSDS) is the pinned BlocksDS."""
    if os.environ.get('HLDS_BLOCKSDS_ANY'):
        return
    if not is_pinned(path):
        raise SystemExit(
            f'BlocksDS at {path} is {version_of(path) or "missing"}, the game is built with {wanted()}.\n'
            f'Install that one (docker/versions.env: BLOCKSDS_COMMIT) and set HLDS_BLOCKSDS to it, '
            f'or HLDS_BLOCKSDS_ANY=1 to try another.')


def main(argv):
    if len(argv) >= 3 and argv[1] == '--check':
        check(argv[2])
        return 0
    if len(argv) >= 2 and argv[1] == '--pick':
        for path in argv[2:]:
            if is_pinned(path):
                print(path)
                return 0
        return 1
    print(wanted())
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
