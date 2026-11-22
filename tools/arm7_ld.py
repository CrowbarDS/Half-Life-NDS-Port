#!/usr/bin/env python3
"""The ARM7 program's linker script, and a check of what it made.

    arm7_ld.py write --sdk BLOCKSDS --out-dir D   D/hl_arm7.ld and D/hl_arm7.specs
    arm7_ld.py check ELF                          exit 1 if the image would be laid wrong

BlocksDS's ds_arm7.ld runs the program in the ARM7's own memory and loads it
in main RAM (`>iwram AT>ewram`): its start copies one block, from the load
address to where it runs. That holds only while every section is the same
distance from where it is loaded as the first. GNU ld aligns a section's run
address to what its input asks, and its load address only when told to
(ALIGN_WITH_INPUT), which the script never does: a section that starts on a
stricter boundary than the last one ended on is then copied a few bytes short
of where its code looks for it. libnds's threads (2026-11-20) brought the
ARM7 sections named for the ARM9's ITCM, which the script does not place:
the read-only data came out 4 bytes low, every switch's table a word off --
the sound's commands each run as the next one (a Chain as a Shutdown) -- and
the initialised data in no loaded segment at all.

So, here: the same script, each section loaded where it runs being told to
keep its load address in step (ALIGN_WITH_INPUT), and the ITCM-named code
with the rest of the ARM7's (it has no ITCM). And the check, after the link:
one distance for everything the start copies, and that all of it is in a
segment the ROM carries.
"""
import os
import re
import subprocess
import sys

OBJDUMP = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-objdump'
READELF = '/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-readelf'


def patch_script(text):
    """ds_arm7.ld with ALIGN_WITH_INPUT on each section `AT>ewram`, and the
    .itcm sections in .text."""
    # The .itcm input sections, with the program's code.
    # (The line of .text's own, not the .twl section's *.twl*(...) one.)
    anchor = re.compile(r'^(\s*)\*\(\.text \.stub \.text\.\* \.gnu\.linkonce\.t\.\*\)$', re.M)
    if len(anchor.findall(text)) != 1:
        raise SystemExit('arm7_ld: ds_arm7.ld has not one line "*(.text .stub ...)": look at it again')
    text = anchor.sub(lambda m: m.group(0) + '\n' + m.group(1) + '*(.itcm .itcm.*) /* (arm7_ld.py: the ARM7 has no ITCM) */',
                      text)
    # Each output section, by its header, to its closing brace.
    out = []
    pos = 0
    header = re.compile(r'^(\s*\.[A-Za-z0-9_.]+(?:\s+ALIGN\(\d+\))?(?:\s*\([A-Z]+\))?\s*):', re.M)
    patched = 0
    for m in header.finditer(text):
        start = m.end()
        brace = text.find('{', start)
        if brace < 0:
            continue
        depth, k = 0, brace
        while k < len(text):
            if text[k] == '{':
                depth += 1
            elif text[k] == '}':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        tail = text[k:text.find('\n', k)]
        if 'AT>ewram' not in tail.replace(' ', ''):
            continue
        out.append(text[pos:start])
        out.append(' ALIGN_WITH_INPUT')
        pos = start
        patched += 1
    out.append(text[pos:])
    if patched < 5:
        raise SystemExit('arm7_ld: only %d sections loaded AT>ewram found in ds_arm7.ld: look at it again' % patched)
    return ''.join(out)


def write(sdk, out_dir):
    crts = os.path.join(sdk, 'sys', 'crts')
    with open(os.path.join(crts, 'ds_arm7.ld'), encoding='utf-8') as f:
        script = patch_script(f.read())
    with open(os.path.join(crts, 'ds_arm7.specs'), encoding='utf-8') as f:
        specs = f.read()
    os.makedirs(out_dir, exist_ok=True)
    ld_path = os.path.abspath(os.path.join(out_dir, 'hl_arm7.ld'))
    new, n = re.subn(r'-T %:getenv\(BLOCKSDS /sys/crts/ds_arm7\.ld\)', '-T ' + ld_path, specs)
    if n != 1:
        raise SystemExit('arm7_ld: ds_arm7.specs does not name ds_arm7.ld once')
    for name, body in (('hl_arm7.ld', script), ('hl_arm7.specs', new)):
        path = os.path.join(out_dir, name)
        try:
            with open(path, encoding='utf-8') as f:
                if f.read() == body:
                    continue
        except OSError:
            pass
        with open(path, 'w', encoding='utf-8') as f:
            f.write(body)


def check(elf):
    """1 if a section the ARM7's start copies is not where it runs."""
    out = subprocess.run([OBJDUMP, '-h', '-w', elf], capture_output=True, text=True, check=True).stdout
    sections = []
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 7 and p[0].isdigit():
            name, size, vma, lma = p[1], int(p[2], 16), int(p[3], 16), int(p[4], 16)
            flags = ' '.join(p[7:]) if len(p) > 7 else ''
            sections.append((name, size, vma, lma, flags))
    syms = subprocess.run([READELF, '-s', '-W', elf], capture_output=True, text=True, check=True).stdout
    marks = {}
    for line in syms.splitlines():
        p = line.split()
        if len(p) >= 8 and p[7] in ('__arm7_start__', '__arm7_end__'):
            marks[p[7]] = int(p[1], 16)
    start, end = marks.get('__arm7_start__'), marks.get('__arm7_end__')
    if start is None or end is None:
        print('arm7_ld: no __arm7_start__/__arm7_end__ in %s' % elf)
        return 1
    copied = [s for s in sections if s[1] > 0 and start <= s[2] < end and 'LOAD' in s[4]]
    failed = []
    distance = None
    for name, size, vma, lma, _flags in copied:
        d = vma - lma
        if distance is None:
            distance = d
        elif d != distance:
            failed.append('%s runs at %08x, loaded at %08x: %+d bytes from the rest' % (name, vma, lma, d - distance))
        if lma == vma:
            failed.append('%s is loaded where it runs (%08x): not in the image the start copies' % (name, vma))
    # And all of it in a segment the ROM carries.
    heads = subprocess.run([READELF, '-l', '-W', elf], capture_output=True, text=True, check=True).stdout
    loads = []
    for line in heads.splitlines():
        p = line.split()
        if p and p[0] == 'LOAD':
            loads.append((int(p[2], 16), int(p[4], 16)))  # (vaddr, filesz)
    for name, size, vma, lma, flags in copied:
        if 'CONTENTS' in flags and not any(v <= vma and vma + size <= v + n for v, n in loads):
            failed.append('%s (%08x, %d bytes) is in no loaded segment' % (name, vma, size))
    for f in failed:
        print('arm7_ld: ' + f)
    if not failed:
        print('arm7_ld: %s: %d sections, one distance (%#x)' % (elf, len(copied), distance or 0))
    return 1 if failed else 0


def main(argv):
    if len(argv) >= 2 and argv[1] == 'write':
        args = dict(zip(argv[2::2], argv[3::2]))
        write(args['--sdk'], args['--out-dir'])
        return 0
    if len(argv) == 3 and argv[1] == 'check':
        return check(argv[2])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
