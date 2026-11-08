#!/usr/bin/env python3
"""The ROM the emulator is given, and its recent list (play.sh).

  emurom.py <rom> <config dir>      prints the path to give melonDS

The game's ROM carries the game code WHLD (build.py: TWiLight Menu++ offers
the 3DS's wide screen to a homebrew so marked). melonDS gives a card (DLDI)
only to what it takes for homebrew, game code ####: the game, so marked,
stops on "No SD card found". So the emulator is given a copy marked ####
(build/emu/<name>, its header's checksum made again); the game never reads
its own code. (Same name as the ROM: melonDS writes what it boots into the
card's root by that name, as it did with the ROM itself.) A ROM already
marked #### is given as it is.

The private configuration's recent list is then set: that ROM first and the
Download Play child second, where EMU_ROM2=child / EMU_ROM3=child pick it.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CHILD = os.path.join(ROOT, 'child', 'out', 'hl-child.nds')


def header_crc(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def emulator_rom(rom):
    rom = os.path.abspath(rom)
    data = bytearray(open(rom, 'rb').read())
    if data[0x0C:0x10] == b'####':
        return rom
    out = os.path.join(ROOT, 'build', 'emu', os.path.basename(rom))
    if os.path.abspath(out) == rom:
        sys.exit('emurom: %s is already where its copy would go' % rom)
    data[0x0C:0x10] = b'####'
    data[0x15E:0x160] = header_crc(data[:0x15E]).to_bytes(2, 'little')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(data)
    return out


def set_recent(config_dir, roms):
    path = os.path.join(config_dir, 'melonDS', 'melonDS.toml')
    if not os.path.isfile(path):
        return
    lines = open(path, encoding='utf-8').read().split('\n')
    out = []
    skipping = False
    for line in lines:
        if line.startswith('RecentROM = ['):
            out.append('RecentROM = [')
            out += ['    "%s",' % r for r in roms]
            out.append(']')
            skipping = not line.rstrip().endswith(']')
            continue
        if skipping:
            skipping = line.strip() != ']'
            continue
        out.append(line)
    open(path, 'w', encoding='utf-8').write('\n'.join(out))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rom = emulator_rom(sys.argv[1])
    set_recent(sys.argv[2], [rom] + ([CHILD] if os.path.isfile(CHILD) else []))
    print(rom)


if __name__ == '__main__':
    main()
