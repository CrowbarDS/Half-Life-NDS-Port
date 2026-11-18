#!/usr/bin/env python3
"""The program's build ID: what ties a family library to the one program it
was linked against.

  program_id.py stamp ARM9.elf     put the ID into the program, in place
  program_id.py read ARM9.elf      print it
  program_id.py --self-test        the same on a made-up ELF (ctest)

A family (dsl/<family>.dsl) is linked against the program's ELF: its calls
into the core are to the program's addresses, written into it as numbers.
Against any other program they are wrong, and nothing in the library would
say so. So the program carries an ID (hlProgramId, source/game/ProgramId.cpp)
-- the first 8 bytes of a SHA-1 of its ELF, taken with the ID's own bytes
zeroed -- and tools/dsl_family.py stamps the same ID into every family it
writes (hlFamilyProgramId). The loader (source/game/Families.cpp) refuses a
family whose ID is not its program's.

Run by the program's link (build.py: post_link) as part of the same step,
so the ELF on disk always carries its own ID.
"""
import hashlib
import struct
import sys

PROGRAM_SYMBOL = 'hlProgramId'
SIZE = 8
# What the source puts there, for the link (source/game/ProgramId.cpp): not
# zero, so the ID is kept in the program's constants and not its zeroed data.
PLACEHOLDER = b'HLPROGID'


def symbol_offset(data, name):
    """(file offset, size) of the data symbol `name` in an ELF32 file."""
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise ValueError('not a little-endian ELF32 file')
    (shoff,) = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2E)
    sections = []
    for i in range(shnum):
        sections.append(struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize))
    for s in sections:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[s[6]]
        for at in range(s[4], s[4] + s[5], 16):
            st_name, value, size, _info, _other, shndx = struct.unpack_from('<IIIBBH', data, at)
            end = data.index(b'\0', strtab[4] + st_name)
            if data[strtab[4] + st_name:end].decode('latin-1') != name:
                continue
            if shndx == 0 or shndx >= len(sections):
                raise ValueError('%s: not defined in a section' % name)
            sec = sections[shndx]
            if sec[1] == 8:  # SHT_NOBITS
                raise ValueError('%s: in zeroed data, not in the file' % name)
            return sec[4] + (value - sec[3]), size
    raise ValueError('%s: no such symbol' % name)


def compute(data, at):
    blank = bytearray(data)
    blank[at:at + SIZE] = bytes(SIZE)
    digest = hashlib.sha1(bytes(blank)).digest()[:SIZE]
    # (Never the placeholder, and never zero: both would look unstamped.)
    if digest in (bytes(SIZE), PLACEHOLDER):
        digest = bytes([digest[0] ^ 1]) + digest[1:]
    return digest


def stamp_bytes(data):
    at, size = symbol_offset(data, PROGRAM_SYMBOL)
    if size != SIZE:
        raise ValueError('%s is %d bytes, not %d' % (PROGRAM_SYMBOL, size, SIZE))
    out = bytearray(data)
    out[at:at + SIZE] = compute(data, at)
    return bytes(out)


def read_bytes(data):
    at, _ = symbol_offset(data, PROGRAM_SYMBOL)
    value = bytes(data[at:at + SIZE])
    if value in (bytes(SIZE), PLACEHOLDER):
        raise ValueError('the program has no ID stamped')
    if value != compute(data, at):
        raise ValueError('the program\'s ID is not its own (changed after it was stamped)')
    return value


def read(path):
    with open(path, 'rb') as f:
        return read_bytes(f.read())


def self_test():
    # A made-up ELF: header, one data section holding the symbol, a symbol
    # table and its names.
    names = b'\0' + PROGRAM_SYMBOL.encode() + b'\0'
    shnames = b'\0.data\0.symtab\0.strtab\0'
    payload = b'abcd' + PLACEHOLDER + b'efgh'
    head = 52
    data_off = head
    sym_off = data_off + len(payload)
    syms = bytes(16) + struct.pack('<IIIBBH', 1, 0x02000004, SIZE, 0x11, 0, 1)
    str_off = sym_off + len(syms)
    shs_off = str_off + len(names)
    sh_off = (shs_off + len(shnames) + 3) & ~3
    body = bytearray(sh_off)
    body[data_off:data_off + len(payload)] = payload
    body[sym_off:sym_off + len(syms)] = syms
    body[str_off:str_off + len(names)] = names
    body[shs_off:shs_off + len(shnames)] = shnames
    sects = [struct.pack('<IIIIIIIIII', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
             struct.pack('<IIIIIIIIII', 1, 1, 3, 0x02000000, data_off, len(payload), 0, 0, 4, 0),
             struct.pack('<IIIIIIIIII', 7, 2, 0, 0, sym_off, len(syms), 3, 1, 4, 16),
             struct.pack('<IIIIIIIIII', 15, 3, 0, 0, str_off, len(names), 0, 0, 1, 0),
             struct.pack('<IIIIIIIIII', 15, 3, 0, 0, shs_off, len(shnames), 0, 0, 1, 0)]
    body += b''.join(sects)
    body[0:4] = b'\x7fELF'
    body[4], body[5] = 1, 1
    struct.pack_into('<I', body, 0x20, sh_off)
    struct.pack_into('<HHH', body, 0x2E, 40, len(sects), 4)
    elf = bytes(body)
    failed = []
    try:
        read_bytes(elf)
        failed.append('an unstamped program was read')
    except ValueError:
        pass
    a = stamp_bytes(elf)
    if stamp_bytes(a) != a:
        failed.append('stamping twice changed the ID')
    if read_bytes(a) == PLACEHOLDER:
        failed.append('the ID is the placeholder')
    other = bytearray(elf)
    other[data_off] ^= 1
    if read_bytes(stamp_bytes(bytes(other))) == read_bytes(a):
        failed.append('two programs got the same ID')
    changed = bytearray(a)
    changed[data_off + len(payload) - 1] ^= 1
    try:
        read_bytes(bytes(changed))
        failed.append('a program changed after its stamp was read')
    except ValueError:
        pass
    for f in failed:
        print('program_id self-test:', f)
    return 1 if failed else 0


def main(argv):
    if len(argv) == 2 and argv[1] == '--self-test':
        return self_test()
    if len(argv) != 3 or argv[1] not in ('stamp', 'read'):
        print(__doc__)
        return 2
    path = argv[2]
    try:
        if argv[1] == 'stamp':
            with open(path, 'rb') as f:
                data = f.read()
            out = stamp_bytes(data)
            if out != data:
                with open(path, 'wb') as f:
                    f.write(out)
        else:
            print(read(path).hex())
    except (OSError, ValueError) as e:
        print('%s: %s' % (path, e), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
