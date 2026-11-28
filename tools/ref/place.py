#!/usr/bin/env python3
"""place.py <in.bsp> <out.bsp> <x> <y> <z> <yaw>

A copy of a map of the game's in which the player starts where they are
told to: every info_player_start is moved there and turned that way. The
real game has no command that puts the player at a place, and this is how
a reference run begins at the same spot as a run of the port.

Only the entity text is changed. (A BSP's header is a version and fifteen
{offset, length} pairs; the entities are the first -- the second in Blue
Shift's maps, which have the first two swapped. The new text is put at the
end of the file and the pair pointed at it.)

With REF_BARE set in the environment, only the world and the start are kept,
so that nothing is heard but the player (the map's machinery and voices would
drown a footstep); REF_KEEP, a pattern, keeps the classes that match it too.

REF_NAME="x y z name; ..." gives the monster standing there (within two
units) a name, for a script to call it by -- the game takes the first of a
class it finds, the port the nearest. REF_ADD is entity text added as it is
("{ ... }" each): a scripted_sequence that sends a monster somewhere, a
trigger to set it off. REF_SET="*N key value; ..." changes (or adds) a key of
the brush entity of model *N: a door made one the monsters may not open, a
door made a crate (and moved down, by its origin, to be seen over).
"""
import os
import re
import struct
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    x, y, z, yaw = (float(v) for v in sys.argv[3:7])
    data = bytearray(open(src, 'rb').read())
    version = struct.unpack_from('<i', data, 0)[0]
    if version != 30:
        sys.exit('not a Half-Life map: %s' % src)
    # (Which of the first two pairs is the text: it starts with a brace, as
    # the converter's BspFile tells them apart.)
    pair = 4
    offset, length = struct.unpack_from('<ii', data, 4)
    if not data[offset:offset + 1] == b'{':
        other = struct.unpack_from('<ii', data, 12)
        if data[other[0]:other[0] + 1] == b'{':
            pair = 12
            offset, length = other
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
    names = []
    for item in os.environ.get('REF_NAME', '').split(';'):
        if item.strip():
            *at, name = item.split()
            names.append(([float(v) for v in at], name))
    named = 0
    for block in blocks:
        kind = re.search(r'"classname" "([^"]*)"', block)
        if keep and not (kind and re.fullmatch(keep, kind[1])):
            continue
        model = re.search(r'"model" "(\*\d+)"', block)
        for item in os.environ.get('REF_SET', '').split(';'):
            if model and item.split()[:1] == [model[1]]:
                key, value = item.split()[1], ' '.join(item.split()[2:])
                if re.search(r'"%s" "[^"]*"' % re.escape(key), block):
                    block = re.sub(r'"%s" "[^"]*"' % re.escape(key), '"%s" "%s"' % (key, value), block)
                else:
                    block = block.replace('{', '{\n"%s" "%s"' % (key, value), 1)
        origin = re.search(r'"origin" "([^"]*)"', block)
        if names and kind and kind[1].startswith('monster_') and origin:
            o = [float(v) for v in origin[1].split()]
            for at, name in names:
                if all(abs(o[i] - at[i]) <= 2 for i in range(3)):
                    block = re.sub(r'"targetname" "[^"]*"\s*', '', block)
                    block = block.replace('{', '{\n"targetname" "%s"' % name, 1)
                    named += 1
        if '"classname" "info_player_start"' in block:
            block = re.sub(r'"(origin|angle|angles)" "[^"]*"\s*', '', block)
            block = block.replace('{', '{\n"origin" "%g %g %g"\n"angles" "0 %g 0"\n' % (x, y, z, yaw), 1)
            moved += 1
        out.append(block)
    if moved == 0:
        # (A map the game only ever comes into from another has none: one
        # is added, where the player is to be.)
        out.append('{\n"classname" "info_player_start"\n"origin" "%g %g %g"\n"angles" "0 %g 0"\n}' % (x, y, z, yaw))
    if named != len(names):
        sys.exit('REF_NAME: %d of %d monsters found' % (named, len(names)))
    if os.environ.get('REF_ADD'):
        out.append(os.environ['REF_ADD'])
    lump = ('\n'.join(out) + '\n').encode('latin-1') + b'\0'
    while len(data) % 4:
        data.append(0)
    struct.pack_into('<ii', data, pair, len(data), len(lump))
    # REF_STANDARD: the first two pairs in Half-Life's order, the entities
    # first. (The engine reads Blue Shift's swapped order only when the game's
    # folder is "bshift" itself: the side folder's copy must be a plain map.)
    if pair == 12 and os.environ.get('REF_STANDARD'):
        first, second = data[4:12], data[12:20]
        data[4:12], data[12:20] = second, first
    data += lump
    open(dst, 'wb').write(data)


if __name__ == '__main__':
    main()
