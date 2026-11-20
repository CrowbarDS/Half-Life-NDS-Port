#!/usr/bin/env python3
"""What the program's link does once its ELF is written (build.py: post_link):

  arm9_post_link.py pack|rom <manifest.json> <arm9.elf>

1. its overlays checked against the rules of tools/ovl.py (none in a
   manifest with no regions): the link fails, and the ELF is removed, on
   any problem -- the next build links again;
2. (rom) the overlays' sums stamped into the program (hlOvlSums): what the
   loader checks each file read from the ROM's overlay table against;
3. its ID stamped in (tools/program_id.py: the families are built for this
   program alone, and the overlay pack is checked against it) -- after the
   sums, so that the ID is of the program as it will be;
4. the overlays written beside the manifest, each time (the build counts
   them made by this step): pack, arm9.ovl for the ROM's NitroFS; rom, the
   ROM's own ARM9 overlay table (y9.bin) and its files (ovl/).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ovl  # noqa: E402
import program_id  # noqa: E402


def main(argv):
    if len(argv) != 4 or argv[1] not in ('pack', 'rom'):
        print(__doc__)
        return 2
    mode, manifest_path, elf = argv[1], argv[2], argv[3]
    manifest = ovl.load_manifest(manifest_path)
    out_dir = os.path.dirname(manifest_path)
    code = 0
    if manifest.regions:
        map_path = os.path.join(os.path.dirname(elf), 'arm9.map')
        code = ovl.cmd_check({'elf': elf, 'map': map_path, 'manifest': manifest_path})
        if code == 0 and mode == 'rom':
            code = ovl.cmd_stamp({'elf': elf, 'manifest': manifest_path})
    if code == 0:
        code = program_id.main(['program_id.py', 'stamp', elf])
    if code == 0 and manifest.regions:
        if mode == 'rom':
            code = ovl.cmd_rom({'elf': elf, 'manifest': manifest_path, 'table': os.path.join(out_dir, 'y9.bin'),
                                'dir': os.path.join(out_dir, 'ovl')})
        else:
            pack = os.path.join(out_dir, 'arm9.ovl')
            code = ovl.cmd_pack({'elf': elf, 'manifest': manifest_path, 'out': pack})
            if code == 0:
                os.utime(pack)
    if code != 0:
        os.remove(elf)
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv))
