#!/usr/bin/env python3
"""Names the build: the commit, and if the tree differs from it, a '+' and
the time it was built. Run by make.sh before the ARM build.

Writes source/BuildId.gen.cpp (only when something changes, so an unchanged
tree is not relinked) and prints the name; make.sh keeps the build's ELFs
under build/elf/<name>/, so that the addresses on a crash screen -- which
shows the name -- can be looked up afterwards.

The same file has the version the title shows, from `git describe`: the one
the converter gets the same way (tools/hlds-convert/cmake/Version.cmake).
"""
import datetime
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'source', 'BuildId.gen.cpp')


def git(*args):
    try:
        return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ''


def main():
    name = git('rev-parse', '--short=7', 'HEAD') or 'nogit'
    if git('status', '--porcelain', '--untracked-files=no'):
        name += '+' + datetime.datetime.now().strftime('%m%d-%H%M')
    version = git('describe', '--tags', '--dirty', '--always') or 'unknown'
    text = ('// Written by tools/buildid.py: do not edit.\n'
            f'extern "C" const char hlBuildId[] = "{name}";\n'
            f'extern "C" const char hlVersion[] = "{version}";\n')
    try:
        with open(OUT) as f:
            same = f.read() == text
    except OSError:
        same = False
    if not same:
        with open(OUT, 'w') as f:
            f.write(text)
    print(name)


if __name__ == '__main__':
    main()
