#!/usr/bin/env python3
"""What the program's link does once its ELF is written (build.py: post_link):

  arm9_post_link.py <manifest.json> <arm9.elf>

1. its ID stamped in (tools/program_id.py: the families are built for this
   program alone, and the overlay pack is checked against it);
2. its overlays checked against the rules of tools/ovl.py (none in a
   manifest with no regions): the link fails, and the ELF is removed, on
   any problem -- the next build links again;
3. their pack written beside the manifest (arm9.ovl, for the ROM's
   NitroFS), written each time: the build counts it made by this step.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ovl  # noqa: E402
import program_id  # noqa: E402


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    manifest_path, elf = argv[1], argv[2]
    if program_id.main(['program_id.py', 'stamp', elf]) != 0:
        return 1
    manifest = ovl.load_manifest(manifest_path)
    if not manifest.regions:
        return 0
    map_path = os.path.join(os.path.dirname(elf), 'arm9.map')
    code = ovl.cmd_check({'elf': elf, 'map': map_path, 'manifest': manifest_path})
    if code == 0:
        pack = os.path.join(os.path.dirname(manifest_path), 'arm9.ovl')
        code = ovl.cmd_pack({'elf': elf, 'manifest': manifest_path, 'out': pack})
        if code == 0:
            os.utime(pack)
    if code != 0:
        os.remove(elf)
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv))
