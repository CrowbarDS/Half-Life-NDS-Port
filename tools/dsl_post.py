#!/usr/bin/env python3
"""What runs on the game library after dsltool (build.py's post_tool).

usage: dsl_post.py <hl.full.dsl> <hl.dsl>

The library stripped of what the loader would keep for nothing
(tools/dsl_strip.py), then the table of its functions to be moved into ITCM
at load (tools/dsl_kernels.py), from its ELF beside it, as <hl>.kernels.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsl_kernels  # noqa: E402
import dsl_strip  # noqa: E402


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    full, out = argv[1], argv[2]
    failed = dsl_strip.main([argv[0], full, out])
    if failed:
        return failed
    elf = full[:-len('.full.dsl')] + '.elf'
    return dsl_kernels.main([argv[0], elf, os.path.splitext(out)[0] + '.kernels'])


if __name__ == '__main__':
    sys.exit(main(sys.argv))
