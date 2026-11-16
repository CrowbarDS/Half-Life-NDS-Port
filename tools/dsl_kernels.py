#!/usr/bin/env python3
"""The game library's functions to be moved into ITCM at load, as built.

usage: dsl_kernels.py <hl.elf> <hl.kernels>        write the table
       dsl_kernels.py --check <hl.elf>             only say what would be moved
       dsl_kernels.py --self-test                  what it refuses, on a made-up object

The engine (source/game/Kernels.cpp) copies each function named in KERNELS
into ITCM as the library loads, aims the calls in it again from where the copy
is, and makes its old entry a jump to the copy (shared/HlKernelMove.hpp). That
is only safe for a function whose every reference outside itself is either a
word the loader fills with an address (left as it is) or a call or jump with a
relocation (aimed again). This finds each one in the library's ELF -- linked
at 0, so an address there is an offset from where the library is loaded --
and stops the build if anything else in it reaches outside it:
  - a relocation of any other kind;
  - a load from or an address of somewhere by the pc, outside the function;
  - a branch out of it with no relocation to say so.
The table (the format: HlKernelMove.hpp) gives each one's offset, size, the
sum of its bytes with those words taken as 0, and the offset and kind of each
of those words -- the only ones the engine changes.
"""
import os
import re
import struct
import subprocess
import sys

TOOL = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-'

# What to move, by the start of its demangled name (each must name one
# function): chosen by the console's own sampler (tools/pcsample.py), and
# held to what ITCM has room for (about 4 KB after the program's own).
KERNELS = [
    'hldata::traceShot(',
    'hldata::detail::lineThrough(',
    'hldata::pointContents(',
]

KIND_ADDRESS, KIND_ARM_BRANCH, KIND_THUMB_BRANCH = 0, 1, 2
RELOCS = {
    'R_ARM_ABS32': KIND_ADDRESS,
    'R_ARM_TARGET1': KIND_ADDRESS,
    'R_ARM_CALL': KIND_ARM_BRANCH,
    'R_ARM_JUMP24': KIND_ARM_BRANCH,
    'R_ARM_THM_CALL': KIND_THUMB_BRANCH,
}
IGNORED = {'R_ARM_V4BX', 'R_ARM_NONE'}  # (the loader writes nothing for these)


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def sections(path):
    """(address, size, file offset) of each loaded section with bytes."""
    data = open(path, 'rb').read()
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x2E)
    found = []
    for i in range(shnum):
        name, kind, flags, addr, offset, size = struct.unpack_from('<IIIIII', data, shoff + i * shentsize)
        # (Allocated and in the file: the library's one section is marked
        # INIT_ARRAY by its linker script, not PROGBITS.)
        if kind != 8 and (flags & 2) and size:  # not NOBITS; ALLOC
            found.append((addr, size, offset))
    return data, found


def read_bytes(data, secs, addr, size):
    for a, s, o in secs:
        if a <= addr and addr + size <= a + s:
            return data[o + addr - a:o + addr - a + size]
    raise ValueError('no section holds %#x..%#x' % (addr, addr + size))


def functions(elf):
    """name -> (address with the Thumb bit, size)."""
    found = {}
    for line in run(TOOL + 'nm', '-S', '-C', '--defined-only', elf).splitlines():
        part = line.split(' ', 3)
        if len(part) == 4 and part[2] in 'TtWw' and part[1]:
            found.setdefault(part[3], (int(part[0], 16), int(part[1], 16)))
    return found


def relocations(elf):
    found = []
    for line in run(TOOL + 'readelf', '-rW', elf).splitlines():
        part = line.split()
        if len(part) >= 3 and part[2].startswith('R_ARM'):
            try:
                found.append((int(part[0], 16), part[2]))
            except ValueError:
                pass
    return found


def kernel_sum(code, words):
    h = 0x811C9DC5
    for i, b in enumerate(code):
        if any(0 <= i - o < 4 for o, _ in words):
            b = 0
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


PC_LOAD = re.compile(r'\[pc(?:, #(-?\d+))?\]')
PC_ADD = re.compile(r'\b(add|sub)\w*\s+(\w+), pc, #(-?\d+)')
BRANCH = re.compile(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4,8}\s+)+(b|bl|blx)(?:eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?\s+([0-9a-f]+)')


def check_code(elf, start, size, thumb, words):
    """What in the function reaches outside it other than the listed words."""
    problems = []
    listed = {o for o, _ in words}
    text = run(TOOL + 'objdump', '-d', '--start-address=%#x' % start, '--stop-address=%#x' % (start + size), elf)
    for line in text.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s', line)
        if not m:
            continue
        at = int(m.group(1), 16)
        if '.word' in line or '.short' in line:
            continue  # (its constants)
        insn = line.split('\t', 2)[-1] if '\t' in line else line
        pc = at + (4 if thumb else 8)
        b = BRANCH.match(line)
        if b:
            target = int(b.group(3), 16)
            if not (start <= target < start + size) and (at - start) not in listed:
                problems.append('%#x: %s out of it, with no relocation' % (at, b.group(2)))
            continue
        load = PC_LOAD.search(insn)
        if load:
            target = ((pc & ~3) if thumb else pc) + int(load.group(1) or 0)
            if not (start <= target < start + size):
                problems.append('%#x: a load by the pc from %#x, outside it' % (at, target))
            continue
        add = PC_ADD.search(insn)
        if add:
            sign = -1 if add.group(1) == 'sub' else 1
            target = ((pc & ~3) if thumb else pc) + sign * int(add.group(3))
            if not (start <= target <= start + size):
                problems.append('%#x: an address by the pc, %#x, outside it' % (at, target))
            continue
        # (Anything else that reads the pc -- not as where to go, as a jump
        # table's `add pc, pc, rN` does -- is not understood: refused.)
        operands = insn.split(None, 1)[1] if len(insn.split(None, 1)) > 1 else ''
        if '{' in operands:
            # (A register list: pop and ldm write the pc -- a return -- and
            # only a push or stm with it in reads it.)
            if re.match(r'\s*(push|stm)', insn) and re.search(r'\bpc\b', operands):
                problems.append('%#x: stores the pc: %s' % (at, insn.strip()))
            continue
        reads = [o.strip() for o in operands.split(',')[1:]]
        if any(o == 'pc' or o.startswith('pc') for o in reads) and not re.match(r'\s*add\w*\s+pc, pc,', insn):
            problems.append('%#x: reads the pc: %s' % (at, insn.strip()))
    return problems


def make(elf, names=KERNELS):
    data, secs = sections(elf)
    funcs = functions(elf)
    relocs = relocations(elf)
    table = []
    problems = []
    for want in names:
        matches = [(n, v) for n, v in funcs.items() if n.startswith(want)]
        if len(matches) != 1:
            problems.append('%s: %d functions by that name' % (want, len(matches)))
            continue
        name, (value, size) = matches[0]
        thumb = value & 1
        start = value & ~1
        words = []
        for at, kind in relocs:
            if not (start <= at < start + size):
                continue
            if kind in IGNORED:
                continue
            if kind not in RELOCS:
                problems.append('%s: %s at %#x: not one that can be moved' % (name, kind, at))
                continue
            words.append((at - start, RELOCS[kind]))
        problems += ['%s: %s' % (name, p) for p in check_code(elf, start, size, thumb, words)]
        minimum = 12 if thumb else 8
        if size < minimum:
            problems.append('%s: %d bytes, too few for the jump at its entry' % (name, size))
        code = read_bytes(data, secs, start, size)
        table.append((start, size, kernel_sum(code, words), thumb, name, words))
    return table, problems


def write(table, out):
    blob = bytearray(struct.pack('<III', 0x4E4B4C48, 1, len(table)))
    for start, size, total, thumb, name, words in table:
        short = name.split('(')[0].encode()[:255]
        blob += struct.pack('<IIIBBH', start, size, total, 1 if thumb else 0, len(short), len(words))
        blob += short
        for offset, kind in words:
            blob += struct.pack('<I', offset | (kind << 30))
    open(out, 'wb').write(bytes(blob))


SELF_TEST = r"""
    .syntax unified
    .arm
    .text
    .global good
    .type good, %function
good:
    push {r4, lr}
    bl other
    ldr r0, =0x12345678
    ldr r1, [pc, #4]
    add r2, pc, #0
    pop {r4, pc}
    .word 7
    .ltorg
    .size good, .-good

    .global far_load
    .type far_load, %function
far_load:
    ldr r0, word_elsewhere
    bx lr
    nop
    .size far_load, .-far_load

    .global quiet_branch
    .type quiet_branch, %function
quiet_branch:
    b 1f
    nop
    .size quiet_branch, .-quiet_branch
1:
    bx lr

    .global relative_word
    .type relative_word, %function
relative_word:
    bx lr
    nop
    .word other - .
    .size relative_word, .-relative_word
word_elsewhere:
    .word 5

    @ (Each function in a section of its own, as -ffunction-sections
    @ makes them: a difference to another one is then a relocation.)
    .section .text.other, "ax"
    .global other
    .type other, %function
other:
    bx lr
    .size other, .-other
"""


def self_test():
    """A made-up object: one function that may be moved, three that may not."""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        src, obj = os.path.join(d, 't.s'), os.path.join(d, 't.o')
        open(src, 'w').write(SELF_TEST)
        subprocess.run([TOOL + 'as', '-mcpu=arm946e-s', src, '-o', obj], check=True)
        failed = []
        table, problems = make(obj, ['good'])
        if problems or len(table) != 1:
            failed.append('good: refused: %s' % problems)
        for name, why in (('far_load', 'a load by the pc'), ('quiet_branch', 'with no relocation'),
                          ('relative_word', 'R_ARM_REL32')):
            _, problems = make(obj, [name])
            if not any(why in p for p in problems):
                failed.append('%s: not refused for %r (%s)' % (name, why, problems))
        return failed


def main(argv):
    if argv[1:] == ['--self-test']:
        failed = self_test()
        for f in failed:
            print('dsl_kernels: ' + f)
        print('dsl_kernels: self-test %s' % ('FAILED' if failed else 'passed'))
        return 1 if failed else 0
    if len(argv) == 3 and argv[1] == '--check':
        table, problems = make(argv[2])
    elif len(argv) == 3:
        table, problems = make(argv[1])
    else:
        print(__doc__)
        return 2
    for p in problems:
        print('dsl_kernels: ' + p)
    if problems:
        print('dsl_kernels: refused')
        return 1
    for start, size, total, thumb, name, words in table:
        print('dsl_kernels: %s: %d bytes at %#x, %d words to aim or keep' % (name.split('(')[0], size, start, len(words)))
    if argv[1] != '--check':
        write(table, argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
