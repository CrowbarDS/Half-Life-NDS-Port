#!/usr/bin/env python3
"""A family of the game (source/game/FamilyApi.hpp) made ready to load.

  dsl_family.py PROGRAM.elf FULL.dsl OUT.dsl
  dsl_family.py --self-test

A family is linked with the program's ELF taken first (build.py:
--just-symbols), so that what it calls of the core is the program's, at the
program's addresses: the link writes those addresses into it, whole. dsltool
does not know that: it lists the program's symbols as the library's own --
public ones even, a thousand and more -- and the loader would move each word
that points at one by the address the library is loaded at. So, in OUT:

- a symbol outside the library's own space is the program's: marked so
  (MAIN_BINARY), and no longer public;
- a word that holds a program address (R_ARM_ABS32, R_ARM_TARGET1 on one of
  those) is left as the link wrote it: its relocation goes. (The loader
  would put the symbol's address there and lose what was added to it:
  &table[3].) A branch to the program keeps its relocation, as the program's.
- public: GetFamilyAPI and hlFamilyProgramId, nothing else;
- then dsl_strip's strip, held to the same loading test;
- and the program's ID (tools/program_id.py) stamped into hlFamilyProgramId.

A family may not reach into an overlay region of the program (tools/ovl.py):
what is there is not always there, and a family cannot ask.

A family may not have constructors: the loader runs them as it loads, before
the ID can be checked.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsl_strip  # noqa: E402
import program_id  # noqa: E402
from dsl_strip import Dsl, MAIN_BINARY, MOVED, PUBLIC, PROGBITS  # noqa: E402

EXPORTS = ('GetFamilyAPI', 'hlFamilyProgramId')
ID_SYMBOL = 'hlFamilyProgramId'
# A program's addresses: ITCM, main RAM, DTCM (and their mirrors).
PROGRAM_LOW, PROGRAM_HIGH = 0x01000000, 0x03000000
# What may be added to a program symbol's address in a word (an element of
# one of its tables), at most.
MOST_ADDEND = 0x10000


def fix(full, regions=()):
    """`full` with the program's symbols the program's: (the DSL, problems).
    `regions`: the program's overlay regions, (name, start, end)."""
    problems = []
    if full.space >= PROGRAM_LOW:
        problems.append('the library is %d bytes: its addresses meet the program\'s' % full.space)
    out = Dsl.__new__(Dsl)
    out.name = full.name
    out.space = full.space
    out.sections = [list(s) for s in full.sections]
    out.data = dict(full.data)
    out.rel_index = full.rel_index
    out.symbols = [list(s) for s in full.symbols]

    def programs(value):
        return value >= full.space

    starts = {s[0]: s[1] for s in full.symbols}
    if starts.get('__bothinit_array_start') != starts.get('__bothinit_array_end'):
        problems.append('it has constructors (global objects built at load): a family may not')

    image = {}
    for i, (address, size, _, kind) in enumerate(full.sections):
        if kind == PROGBITS:
            image[i] = (address, full.data[i])

    def word(at):
        for address, data in image.values():
            if address <= at and at + 4 <= address + len(data):
                return struct.unpack_from('<I', data, at - address)[0]
        return None

    relocs = []
    used = set()
    for offset, info in full.relocs:
        kind, sym = info & 0xFF, info >> 8
        name, value, _ = full.symbols[sym]
        if programs(value):
            if not PROGRAM_LOW <= value < PROGRAM_HIGH:
                problems.append('%s (%#x): neither the library\'s nor the program\'s' % (name, value))
            for region, start, end in regions:
                if start <= value < end:
                    problems.append('%s (%#x) is in the program\'s overlay region %s: a family may not reach into one'
                                    % (name, value, region))
            if kind in MOVED:
                w = word(offset)
                if w is None:
                    problems.append('a word at +%#x (%s) is not in the library\'s data' % (offset, name))
                elif not (0 <= w - value < MOST_ADDEND):
                    problems.append('the word at +%#x holds %#x, not %s (%#x) or near it'
                                    % (offset, w, name, value))
                continue  # (as the link wrote it)
        relocs.append((offset, info))
        used.add(sym)
    out.relocs = relocs
    out.data[out.rel_index] = b''.join(struct.pack('<II', o, i) for o, i in relocs)
    for i, s in enumerate(out.symbols):
        if programs(s[1]):
            # The program's; one no relocation reads is of no use.
            s[2] = MAIN_BINARY if i in used else 0
        elif s[0] in EXPORTS:
            s[2] |= PUBLIC
        else:
            s[2] &= ~PUBLIC
    for name in EXPORTS:
        if not any(s[0] == name and s[2] & PUBLIC for s in out.symbols):
            problems.append('%s is not in it' % name)
    return Dsl(out.write(), full.name), problems


def stamp(dsl, ident):
    """`dsl` with `ident` written over hlFamilyProgramId's placeholder."""
    at = [s[1] for s in dsl.symbols if s[0] == ID_SYMBOL and s[2] & PUBLIC]
    if len(at) != 1:
        raise ValueError('%s: no public %s' % (dsl.name, ID_SYMBOL))
    for i, (address, size, _, kind) in enumerate(dsl.sections):
        if kind == PROGBITS and address <= at[0] and at[0] + len(ident) <= address + size:
            data = bytearray(dsl.data[i])
            here = at[0] - address
            if bytes(data[here:here + len(ident)]) != program_id.PLACEHOLDER:
                raise ValueError('%s: %s does not hold the placeholder' % (dsl.name, ID_SYMBOL))
            data[here:here + len(ident)] = ident
            dsl.data[i] = bytes(data)
            return dsl
    raise ValueError('%s: %s is not in the library\'s data' % (dsl.name, ID_SYMBOL))


def make(full, ident, regions=()):
    """(the family ready to load, problems)."""
    fixed, problems = fix(full, regions)
    if problems:
        return None, problems
    out = Dsl(dsl_strip.strip(fixed).write(), full.name + ' (family)')
    problems = dsl_strip.check(fixed, out, data_symbol=None)
    if problems:
        return None, problems
    return Dsl(stamp(out, ident).write(), out.name), []


def made_up():
    """A family with a call and a word into the program, its two exports,
    a word of its own, and a program symbol nothing reads."""
    words = [0xE1A00000] * 8
    words[0] = 0xEB000000        # bl program_fn
    words[1] = 0x02004008        # .word program_table+8 (as the link wrote it)
    words[2] = 0x00000010        # .word own data
    data = struct.pack('<8I', *words) + program_id.PLACEHOLDER + bytes(8)
    symbols = [['', 0, 0],
               ['program_fn', 0x02001000, PUBLIC],
               ['program_table', 0x02004000, PUBLIC],
               ['own', 0x10, 0],
               ['GetFamilyAPI', 0x0, PUBLIC],
               ['hlFamilyProgramId', 0x20, PUBLIC],
               ['program_unused', 0x02008000, PUBLIC]]
    relocs = [(0, dsl_strip.R_CALL, 1), (4, dsl_strip.R_ABS32, 2), (8, dsl_strip.R_ABS32, 3)]
    rel = b''.join(struct.pack('<II', o, (s << 8) | k) for o, k, s in relocs)
    lib = Dsl.__new__(Dsl)
    lib.name = 'made-up family'
    lib.space = len(data)
    lib.sections = [[0, len(data), 0, PROGBITS], [0, len(rel), 0, dsl_strip.RELOCATIONS]]
    lib.data = {0: data, 1: rel}
    lib.rel_index = 1
    lib.relocs = [(o, (s << 8) | k) for o, k, s in relocs]
    lib.symbols = symbols
    return Dsl(lib.write(), lib.name)


def self_test():
    failed = []
    ident = b'\x01\x02\x03\x04\x05\x06\x07\x08'
    out, problems = make(made_up(), ident)
    failed += problems
    if out is not None:
        names = {s[0]: s for s in out.symbols}
        if 'program_unused' in names:
            failed.append('a program symbol nothing reads was kept')
        if names.get('program_fn', [0, 0, 0])[2] != MAIN_BINARY:
            failed.append('the program function called is not marked the program\'s')
        public = sorted(s[0] for s in out.symbols if s[2] & PUBLIC)
        if public != sorted(EXPORTS):
            failed.append('public: %s' % public)
        image, _, _, _ = dsl_strip.load(out, 0x02200000)
        if struct.unpack_from('<I', image, 4)[0] != 0x02004008:
            failed.append('the word into the program\'s table was moved')
        if struct.unpack_from('<I', image, 8)[0] != 0x02200010:
            failed.append('the word into its own data was not moved')
        if image[0x20:0x28] != ident:
            failed.append('the ID was not stamped')
    # A constructor, and a word that does not hold its symbol's address,
    # must each be refused.
    bad = made_up()
    bad.symbols += [['__bothinit_array_start', 0x18, PUBLIC], ['__bothinit_array_end', 0x1C, PUBLIC]]
    if not make(Dsl(bad.write(), 'bad'), ident)[1]:
        failed.append('a family with a constructor went through')
    bad = made_up()
    bad.data[0] = bad.data[0][:4] + struct.pack('<I', 0x02100000) + bad.data[0][8:]
    if not make(Dsl(bad.write(), 'bad'), ident)[1]:
        failed.append('a word far from its program symbol went through')
    # A call into an overlay region of the program must be refused too.
    if not make(made_up(), ident, [('A', 0x02000800, 0x02002000)])[1]:
        failed.append('a family that reaches into an overlay region went through')
    if make(made_up(), ident, [('A', 0x02100000, 0x02108000)])[1]:
        failed.append('a family clear of the overlay regions was refused')
    return failed


def overlay_regions(program):
    """The program's overlay regions (tools/ovl.py): (name, start, end)."""
    import ovl
    elf = ovl.Elf(open(program, 'rb').read())
    names = {s['name']: s['value'] for s in elf.symbols}
    out = []
    for name, start in names.items():
        if name.startswith('__ovl_') and name.endswith('_start') and name[6:7].isupper():
            out.append((name[6:-6], start, names.get(name[:-6] + '_end', start)))
    return out


def main(argv):
    if argv[1:] == ['--self-test']:
        failed = self_test()
        for f in failed:
            print('dsl_family: ' + f)
        print('dsl_family: self-test %s' % ('FAILED' if failed else 'passed'))
        return 1 if failed else 0
    if len(argv) != 4:
        print(__doc__)
        return 2
    program, full_path, out_path = argv[1:]
    try:
        ident = program_id.read(program)
        regions = overlay_regions(program)
        full = Dsl(open(full_path, 'rb').read(), full_path)
    except (OSError, ValueError) as e:
        print('dsl_family: %s' % e)
        return 1
    out, problems = make(full, ident, regions)
    if problems:
        for p in problems:
            print('dsl_family: %s: %s' % (full_path, p))
        return 1
    with open(out_path, 'wb') as f:
        f.write(out.write())
    print('dsl_family: %s: %d bytes on the heap (%d image, %d relocations, %d symbols), for program %s'
          % (out_path, out.heap_bytes(), out.space, len(out.relocs), len(out.symbols), ident.hex()))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
