#!/usr/bin/env python3
"""The game library made smaller on the heap, and held to loading as before.

  dsl_strip.py FULL.dsl OUT.dsl      write OUT, then check it against FULL
  dsl_strip.py --check FULL OUT      only the check (ctest)
  dsl_strip.py --self-test           the same on a made-up library (ctest)

libnds's loader keeps two things on the heap for as long as the library is
loaded: 8 bytes per relocation, set aside for branch veneers, and the whole
symbol table. Half of hl.dsl's relocations are branches inside the library,
which the loader leaves alone; they cost their 8 bytes all the same. The
symbol table holds a name for every symbol any relocation points at, though
the loader only reads a name to hand it to the program's resolver, and only
uses the value of one that is outside the library.

So, in OUT:
- a relocation the loader does nothing with is gone (a call or jump within
  the library, R_ARM_V4BX);
- a word the loader moves by the load address (R_ARM_ABS32 or R_ARM_TARGET1
  on something of the library) points at one nameless private symbol;
- the symbols left are that one, those outside the library (the loader puts
  their address in), the public ones (dlsym looks them up by name), and any a
  remaining relocation still reads.

The check (a model of the loader, from the DSL format, libnds dsl.h, and the
ARM relocations' meaning) loads both files at two addresses through the same
resolver: the loading test wants the same bytes and the same public symbols;
the checksum test wants the same sum over the library's code and constants,
the guard's (source/Guard.arm.cpp: from where it is put to hlDslData).
"""

import struct
import sys

MAGIC = 0x304C5344
NOBITS, PROGBITS, RELOCATIONS = 0, 1, 2
PUBLIC, MAIN_BINARY, UNRESOLVED = 1, 2, 4

R_ABS32, R_REL32, R_THM_CALL, R_CALL, R_JUMP24, R_TARGET1, R_V4BX, R_TLS_LE32 = 2, 3, 10, 28, 29, 38, 40, 108
BRANCHES = (R_THM_CALL, R_CALL, R_JUMP24)
MOVED = (R_ABS32, R_TARGET1)
TCB_SIZE = 8


class Dsl:
    def __init__(self, data, name='?'):
        self.name = name
        magic, version, count, _, _, self.space = struct.unpack_from('<IBBBBI', data, 0)
        if magic != MAGIC or version != 0:
            raise ValueError('%s: not a DSL file' % name)
        self.sections = []  # [address, size, offset, type]
        at = 12
        for _ in range(count):
            address, size, offset, kind = struct.unpack_from('<IIIB', data, at)
            self.sections.append([address, size, offset, kind])
            at += 16
        # (The loader takes every section with data in turn, each where the
        # one before ended, and the symbol table from there to the end.)
        cursor = at
        for s in self.sections:
            if s[3] == NOBITS:
                continue
            if s[2] != cursor:
                raise ValueError('%s: sections not in order' % name)
            cursor += s[1]
        self.data = {i: data[s[2]:s[2] + s[1]] for i, s in enumerate(self.sections) if s[3] != NOBITS}
        rel = [i for i, s in enumerate(self.sections) if s[3] == RELOCATIONS]
        if len(rel) != 1:
            raise ValueError('%s: %d relocation sections (the loader applies the first)' % (name, len(rel)))
        self.rel_index = rel[0]
        raw = self.data[self.rel_index]
        self.relocs = [struct.unpack_from('<II', raw, i * 8) for i in range(len(raw) // 8)]
        table = data[cursor:]
        (n,) = struct.unpack_from('<H', table, 0)
        self.symbols = []  # [name, value, attributes]
        for i in range(n):
            offset, value, attributes = struct.unpack_from('<III', table, 4 + 12 * i)
            end = table.index(b'\0', offset)
            self.symbols.append([table[offset:end].decode('latin-1'), value, attributes])

    def heap_bytes(self):
        """What the loader keeps: the image, 8 bytes a relocation, the table."""
        return self.space + 8 * len(self.relocs) + len(self.table_bytes(self.symbols))

    @staticmethod
    def table_bytes(symbols):
        names = bytearray()
        where = {}
        base = 4 + 12 * len(symbols)
        entries = bytearray(struct.pack('<HH', len(symbols), 0))
        for name, value, attributes in symbols:
            if name not in where:
                where[name] = base + len(names)
                names += name.encode('latin-1') + b'\0'
            entries += struct.pack('<III', where[name], value, attributes)
        return bytes(entries + names)

    def write(self):
        out = bytearray(struct.pack('<IBBBBI', MAGIC, 0, len(self.sections), 0, 0, self.space))
        cursor = 12 + 16 * len(self.sections)
        heads = bytearray()
        for i, (address, size, _, kind) in enumerate(self.sections):
            if kind == NOBITS:
                heads += struct.pack('<IIIB3x', address, size, 0, kind)
            else:
                heads += struct.pack('<IIIB3x', address, len(self.data[i]), cursor, kind)
                cursor += len(self.data[i])
        out += heads
        for i, s in enumerate(self.sections):
            if s[3] != NOBITS:
                out += self.data[i]
        out += self.table_bytes(self.symbols)
        return bytes(out)


def external(attributes):
    return (attributes & (MAIN_BINARY | UNRESOLVED)) != 0


def strip(full):
    """A copy of `full` with only what the loader acts on."""
    syms = full.symbols
    kept_relocs = []
    new_symbols = [['', 0, 0]]  # 0: the nameless one the moved words point at
    index = {}

    def keep(i):
        if i not in index:
            index[i] = len(new_symbols)
            new_symbols.append(list(syms[i]))
        return index[i]

    # Public and outside symbols first, in their order (dlsym, the resolver).
    for i, (name, value, attributes) in enumerate(syms):
        if attributes & PUBLIC or external(attributes):
            keep(i)
    for offset, info in full.relocs:
        kind, sym = info & 0xFF, info >> 8
        outside = external(syms[sym][2])
        if kind in BRANCHES and not outside:
            continue  # within the library: the loader leaves it be
        if kind == R_V4BX:
            continue  # nothing to do for it either
        if kind in MOVED and not outside:
            kept_relocs.append((offset, kind))  # moved by the load address; no symbol read
            continue
        kept_relocs.append((offset, (keep(sym) << 8) | kind))
    out = Dsl.__new__(Dsl)
    out.name = full.name + ' (stripped)'
    out.space = full.space
    out.sections = [list(s) for s in full.sections]
    out.data = dict(full.data)
    out.rel_index = full.rel_index
    out.relocs = [(o, i) for o, i in kept_relocs]
    out.data[out.rel_index] = b''.join(struct.pack('<II', o, i) for o, i in out.relocs)
    out.symbols = new_symbols
    if len(new_symbols) > 0xFFFF:
        raise ValueError('too many symbols')
    return out


# --- The model of the loader -------------------------------------------------

def resolver(name, value, attributes):
    """The game's (source/game/GameDll.arm.cpp, nearItcm): ITCM addresses to
    the mirror under main RAM, the heap's functions to the guarded ones."""
    if external(attributes):
        if 0x01000000 <= value < 0x01008000:
            value = 0x01FF8000 | (value & 0x7FFF)
        wrapped = {'malloc': 0x0200F000, 'calloc': 0x0200F010, 'realloc': 0x0200F020, 'free': 0x0200F030,
                   'memalign': 0x0200F040, 'aligned_alloc': 0x0200F050, 'posix_memalign': 0x0200F060}
        if name in wrapped:
            value = wrapped[name]
    return value


def load(dsl, base):
    """The library as it would be in memory at `base`: its image and veneer
    pool, the names the resolver was asked for, and its public symbols."""
    pool = 8 * len(dsl.relocs)
    mem = bytearray(dsl.space + pool)
    for i, (address, size, _, kind) in enumerate(dsl.sections):
        if kind == PROGBITS:
            mem[address:address + size] = dsl.data[i]
        elif kind == NOBITS:
            mem[address:address + size] = bytes(size)
    symbols = [list(s) for s in dsl.symbols]
    veneer = dsl.space
    asked = []

    def word(at):
        return struct.unpack_from('<I', mem, at)[0]

    def put(at, v):
        struct.pack_into('<I', mem, at, v & 0xFFFFFFFF)

    for offset, info in dsl.relocs:
        kind, sym = info & 0xFF, info >> 8
        s = symbols[sym]
        s[1] = resolver(s[0], s[1], s[2])  # (the loader lets it change the value for good)
        outside = external(s[2])
        if outside:
            asked.append(s[0])
        here = base + offset
        if kind in MOVED:
            put(offset, s[1] if outside else word(offset) + base)
        elif kind == R_CALL:
            if outside:
                to = s[1]
                delta = (to & ~1) - (here + 8)
                if not -(1 << 25) <= delta < (1 << 25):
                    raise ValueError('BL out of reach')
                if to & 1:  # to Thumb: BLX, with the half-word bit
                    put(offset, 0xFA000000 | ((delta >> 2) & 0xFFFFFF) | (((delta >> 1) & 1) << 24))
                else:
                    put(offset, (word(offset) & 0xFF000000) | ((delta >> 2) & 0xFFFFFF))
        elif kind == R_JUMP24:
            if outside:
                to = s[1]
                if to & 1:  # a B cannot change state: through a veneer, LDR pc, =to
                    if veneer + 8 > dsl.space + pool:
                        raise ValueError('veneer pool used up')
                    put(veneer, 0xE51FF004)
                    put(veneer + 4, to)
                    to = base + veneer
                    veneer += 8
                delta = to - (here + 8)
                put(offset, (word(offset) & 0xFF000000) | ((delta >> 2) & 0xFFFFFF))
        elif kind == R_THM_CALL:
            if outside:
                to = s[1]
                delta = (to & ~1) - (here + 4)
                lo = 0xF800 if to & 1 else 0xE800
                struct.pack_into('<HH', mem, offset, 0xF000 | ((delta >> 12) & 0x7FF), lo | ((delta >> 1) & 0x7FF))
        elif kind == R_TLS_LE32:
            put(offset, s[1] + TCB_SIZE)
        elif kind == R_V4BX:
            pass
        else:
            raise ValueError('relocation type %d the loader does not know' % kind)
    public = {s[0]: base + s[1] for s in symbols if s[2] & PUBLIC}
    return bytes(mem[:dsl.space]), bytes(mem[dsl.space:veneer]), asked, public


def guard_sum(image, size):
    h = 0x811C9DC5
    for (w,) in struct.iter_unpack('<I', image[:size - size % 4]):
        h = ((h ^ w) * 0x01000193) & 0xFFFFFFFF
    return h


def check(full, out, data_symbol='hlDslData'):
    """Problems found loading `out` in place of `full`; none is good."""
    failed = []
    for base in (0x02100000, 0x0213A9C4 & ~7):
        a, va, asked_a, pub_a = load(full, base)
        b, vb, asked_b, pub_b = load(out, base)
        if a != b:
            first = next(i for i in range(len(a)) if a[i] != b[i])
            failed.append('loading at %#x: the images differ (first at +%#x)' % (base, first))
        if va != vb:
            failed.append('loading at %#x: the veneers differ' % base)
        if asked_a != asked_b:
            failed.append('loading at %#x: the resolver is asked other names' % base)
        if pub_a != pub_b:
            failed.append('loading at %#x: the public symbols differ' % base)
        if data_symbol is not None:
            if data_symbol not in pub_b:
                failed.append('%s is not public in the stripped library (the guard needs it)' % data_symbol)
            else:
                code = pub_a[data_symbol] - base
                if guard_sum(a, code) != guard_sum(b, code):
                    failed.append('loading at %#x: the guard\'s sum of the code differs' % base)
    return failed


# --- A made-up library, for the test without a ROM build ----------------------

def made_up():
    words = [0xE1A00000] * 24  # nop
    words[0] = 0xEB000000      # bl (internal)
    words[1] = 0xEA000000      # b (internal)
    words[2] = 0xEB000000      # bl puts (ARM, outside)
    words[3] = 0xEB000000      # bl thumb_fn (Thumb, outside)
    words[4] = 0xEA000000      # b thumb_tail (Thumb, outside: a veneer)
    words[5] = 0x00000040      # .word data (internal)
    words[6] = 0x00000000      # .word malloc (outside, wrapped by the resolver)
    words[7] = 0x00000044      # .word data+4 (TARGET1, internal)
    words[8] = 0x00000000      # .word hw_divide (ITCM, outside)
    words[9] = 0xE12FFF1E      # bx lr (V4BX)
    words[10] = 0x00000000     # tls offset
    words[11] = 0xEA000000     # b puts (ARM, outside)
    progbits = struct.pack('<24I', *words) + bytes(range(0x40, 0x60))  # code, then data at 0x60
    symbols = [
        ['local_a', 0x00, 0],
        ['local_b', 0x10, 0],
        ['puts', 0x02001000, MAIN_BINARY],
        ['thumb_fn', 0x02002001, MAIN_BINARY],
        ['thumb_tail', 0x02003001, MAIN_BINARY],
        ['data', 0x40, 0],
        ['malloc', 0x02004000, MAIN_BINARY],
        ['hw_divide', 0x01000120, MAIN_BINARY],
        ['tls_var', 0x4, 0],
        ['GetGameAPI', 0x00, PUBLIC],
        ['hlDslData', 0x60, PUBLIC],
        ['unused_private', 0x20, 0],
    ]
    relocs = [(0, R_CALL, 1), (4, R_JUMP24, 0), (8, R_CALL, 2), (12, R_CALL, 3), (16, R_JUMP24, 4),
              (20, R_ABS32, 5), (24, R_ABS32, 6), (28, R_TARGET1, 5), (32, R_ABS32, 7), (36, R_V4BX, 0),
              (40, R_TLS_LE32, 8), (44, R_JUMP24, 2), (0, R_CALL, 11)]
    rel = b''.join(struct.pack('<II', o, (s << 8) | k) for o, k, s in relocs)
    lib = Dsl.__new__(Dsl)
    lib.name = 'made-up'
    lib.space = len(progbits) + 16
    lib.sections = [[0, len(progbits), 0, PROGBITS], [0, len(rel), 0, RELOCATIONS], [len(progbits), 16, 0, NOBITS]]
    lib.data = {0: progbits, 1: rel}
    lib.rel_index = 1
    lib.relocs = [(o, (s << 8) | k) for o, k, s in relocs]
    lib.symbols = symbols
    return Dsl(lib.write(), 'made-up')


def self_test():
    failed = []
    full = made_up()
    out = Dsl(strip(full).write(), 'made-up (stripped)')
    # (The internal call, jump, V4BX and the call pointing at an unused
    # private symbol go; the rest stays.)
    if len(out.relocs) != len(full.relocs) - 4:
        failed.append('made-up: %d relocations kept, expected %d' % (len(out.relocs), len(full.relocs) - 4))
    names = [s[0] for s in out.symbols]
    if 'local_a' in names or 'unused_private' in names or 'data' in names:
        failed.append('made-up: private names kept: %s' % names)
    if names[0] != '' or 'GetGameAPI' not in names or 'malloc' not in names or 'tls_var' not in names:
        failed.append('made-up: symbols wrong: %s' % names)
    failed += check(full, out)
    _, veneers, _, _ = load(out, 0x02100000)
    if len(veneers) != 8:
        failed.append('made-up: %d bytes of veneers, expected one (8)' % len(veneers))
    # The test must see a fault: one byte changed in the code, then in a
    # relocation that moves a word, then a public name.
    bad = Dsl(out.write(), 'bad')
    bad.data[0] = bytes([bad.data[0][0] ^ 1]) + bad.data[0][1:]
    if not check(full, bad):
        failed.append('made-up: a changed code byte went unseen')
    bad = Dsl(out.write(), 'bad')
    bad.relocs = [r for r in bad.relocs if (r[1] & 0xFF) not in MOVED or (r[1] >> 8) != 0][:]
    bad.data[bad.rel_index] = b''.join(struct.pack('<II', o, i) for o, i in bad.relocs)
    if not check(full, Dsl(bad.write(), 'bad')):
        failed.append('made-up: a dropped moved word went unseen')
    bad = Dsl(out.write(), 'bad')
    for s in bad.symbols:
        if s[0] == 'hlDslData':
            s[2] = 0
    if not check(full, Dsl(bad.write(), 'bad')):
        failed.append('made-up: hlDslData no longer public went unseen')
    return failed


def report(full, out):
    print('dsl_strip: %s: %d relocations -> %d, %d symbols -> %d; heap %d -> %d bytes (%d KB less)'
          % (full.name, len(full.relocs), len(out.relocs), len(full.symbols), len(out.symbols),
             full.heap_bytes(), out.heap_bytes(), (full.heap_bytes() - out.heap_bytes()) // 1024))


def main(argv):
    if argv[1:] == ['--self-test']:
        failed = self_test()
        for f in failed:
            print('dsl_strip: ' + f)
        print('dsl_strip: self-test %s' % ('FAILED' if failed else 'passed'))
        return 1 if failed else 0
    if len(argv) == 4 and argv[1] == '--check':
        import os
        if not (os.path.isfile(argv[2]) and os.path.isfile(argv[3])):
            print('dsl_strip: %s or %s not built: skipped' % (argv[2], argv[3]))
            return 77
        full = Dsl(open(argv[2], 'rb').read(), argv[2])
        out = Dsl(open(argv[3], 'rb').read(), argv[3])
        report(full, out)
        failed = check(full, out)
        for f in failed:
            print('dsl_strip: ' + f)
        print('dsl_strip: loading and checksum tests %s' % ('FAILED' if failed else 'passed'))
        return 1 if failed else 0
    if len(argv) != 3:
        print(__doc__)
        return 2
    full = Dsl(open(argv[1], 'rb').read(), argv[1])
    out = Dsl(strip(full).write(), argv[2])
    failed = check(full, out)
    if failed:
        for f in failed:
            print('dsl_strip: ' + f)
        return 1
    with open(argv[2], 'wb') as f:
        f.write(out.write())
    report(full, out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
