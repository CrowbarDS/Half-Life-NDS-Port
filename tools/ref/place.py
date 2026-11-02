#!/usr/bin/env python3
"""place.py <in.bsp> <out.bsp> <x> <y> <z> <yaw>

A copy of a map of the game's in which the player starts where they are
told to: every info_player_start is moved there and turned that way. The
real game has no command that puts the player at a place, and this is how
a reference run begins at the same spot as a run of the port.

Only the entity text is changed. (A BSP's header is a version and fifteen
{offset, length} pairs; the entities are the first. The new text is put at
the end of the file and the pair pointed at it.)

With REF_BARE set in the environment, only the world and the start are kept,
so that nothing is heard but the player (the map's machinery and voices would
drown a footstep); REF_KEEP, a pattern, keeps the classes that match it too.
"""
import os
import re
import struct
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    x, y, z, yaw = (float(v) for v in sys.argv[3:7])
    data = bytearray(open(src, 'rb').read())
    version, offset, length = struct.unpack_from('<iii', data, 0)
    if version != 30:
        sys.exit('not a Half-Life map: %s' % src)
    text = data[offset:offset + length].split(b'\0')[0].decode('latin-1')
    # The entities, one {...} each -- told apart with an eye on the quotes:
    # a texture's name may begin with a brace ("{blood2").
    blocks, start, quoted = [], -1, False
    for i, c in enumerate(text):
        if c == '"':
            quoted = not quoted
        elif c == '{' and not quoted:
            start = i
        elif c == '}' and not quoted and start >= 0:
            blocks.append(text[start:i + 1])
            start = -1
    out, moved = [], 0
    keep = None
    if os.environ.get('REF_BARE'):
        keep = 'worldspawn|info_player_start'
        if os.environ.get('REF_KEEP'):
            keep += '|' + os.environ['REF_KEEP']
    for block in blocks:
        kind = re.search(r'"classname" "([^"]*)"', block)
        if keep and not (kind and re.fullmatch(keep, kind[1])):
            continue
        if '"classname" "info_player_start"' in block:
            block = re.sub(r'"(origin|angle|angles)" "[^"]*"\s*', '', block)
            block = block.replace('{', '{\n"origin" "%g %g %g"\n"angles" "0 %g 0"\n' % (x, y, z, yaw), 1)
            moved += 1
        out.append(block)
    if moved == 0:
        # (A map the game only ever comes into from another has none: one
        # is added, where the player is to be.)
        out.append('{\n"classname" "info_player_start"\n"origin" "%g %g %g"\n"angles" "0 %g 0"\n}' % (x, y, z, yaw))
    lump = ('\n'.join(out) + '\n').encode('latin-1') + b'\0'
    while len(data) % 4:
        data.append(0)
    struct.pack_into('<ii', data, 4, len(data), len(lump))
    data += lump
    open(dst, 'wb').write(data)


if __name__ == '__main__':
    main()
