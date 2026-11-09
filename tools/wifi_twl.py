#!/usr/bin/env python3
"""
wifi_twl.py - the wireless library with its Internet half for a DSi only.

Online play is for a DSi (source/net/NetRelay.cpp), which needs the dswifi
build with the IP stack (lwIP: about 90 KB of code). Linked as it is, all of
that would be in the ARM9 program a DS loads too, where the heap has not that
to give. So this makes a copy of the library in which every object that only
the IP build has -- lwIP, and dswifi's own glue to it -- is renamed
`<name>.twl.o`: the BlocksDS linker script puts objects named `*.twl*` in the
DSi-only part of the program (arm9i), which a DS never loads. What the two
builds have in common stays where it was.

An object is the IP build's alone when the build without IP has none of that
name, or has it empty (compiled out), or it is the second of two of one name
(lwIP's own sys.c and netif.c beside dswifi's).

Nothing in the DSi-only part may be reached on a DS: the library is only
started with its IP stack (without WIFI_LOCAL_ONLY) on a DSi.

Usage: wifi_twl.py <dswifi_dl lib dir> <out dir>   (writes <out>/lib/libdswifi_dl9_twl.a)
"""

import os
import shutil
import subprocess
import sys
import tempfile


# The library's variables that code every console runs reads, and that the
# program defines itself (source/net/NetLink.cpp), in memory a DS has.
PROGRAM_GIVES = {'wifi_lwip_enabled'}


def tool(name):
    base = os.environ.get('WONDERFUL_TOOLCHAIN', '/opt/wonderful')
    path = os.path.join(base, 'toolchain', 'gcc-arm-none-eabi', 'bin', 'arm-none-eabi-' + name)
    return path if os.path.exists(path) else 'arm-none-eabi-' + name


def members(archive):
    return subprocess.run([tool('ar'), 't', archive], check=True, capture_output=True,
                          text=True).stdout.split()


def empty_members(archive):
    """The members of `archive` with no code or data at all."""
    out = subprocess.run([tool('size'), archive], check=True, capture_output=True, text=True).stdout
    empty = set()
    for line in out.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 6 and parts[0] == '0' and parts[1] == '0' and parts[2] == '0':
            empty.add(parts[5].split('(')[-1].rstrip(')'))
    return empty


def symbols(obj):
    """(name, kind) for each symbol of an object file, as nm gives them."""
    out = subprocess.run([tool('nm'), obj], check=True, capture_output=True, text=True).stdout
    found = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            found.append((parts[-1], parts[-2]))
    return found


def make(libdir, outdir):
    full = os.path.join(libdir, 'lib', 'libdswifi_dl9.a')
    noip = os.path.join(libdir, 'lib', 'libdswifi_dl9_noip.a')
    out = os.path.join(outdir, 'lib', 'libdswifi_dl9_twl.a')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    # (Made again only when the library is newer, or this script.)
    if os.path.exists(out) and os.path.getmtime(out) >= max(os.path.getmtime(full), os.path.getmtime(noip),
                                                             os.path.getmtime(__file__)):
        return out
    shared = set(members(noip)) - empty_members(noip)
    seen = {}
    names = []
    work = tempfile.mkdtemp(prefix='wifi_twl_')
    try:
        for name in members(full):
            seen[name] = seen.get(name, 0) + 1
            n = seen[name]
            subprocess.run([tool('ar'), 'xN', str(n), os.path.abspath(full), name], check=True, cwd=work)
            stem = name[:-2] if name.endswith('.o') else name
            ip_only = n > 1 or name not in shared
            renamed = f'{stem}{"." + str(n) if n > 1 else ""}{".twl" if ip_only else ""}.o'
            os.replace(os.path.join(work, name), os.path.join(work, renamed))
            names.append(renamed)
        # A variable the rest reads must be where a DS has it too: what is in
        # the DSi-only part is not there on a DS, and reading it there reads
        # whatever is at that address. Each such is made weak here, and the
        # program gives the one that counts (source/net/NetLink.cpp), in the
        # memory every console has. (wifi_lwip_enabled: what everything tests
        # before it goes near the IP stack.)
        needed = set()
        for renamed in names:
            if not renamed.endswith('.twl.o'):
                needed |= {s for s, kind in symbols(os.path.join(work, renamed)) if kind == 'U'}
        for renamed in names:
            if not renamed.endswith('.twl.o'):
                continue
            obj = os.path.join(work, renamed)
            shared_data = {s for s, kind in symbols(obj) if kind in 'BDCGS'} & needed
            unknown = shared_data - PROGRAM_GIVES
            if unknown:
                sys.exit(f'wifi_twl: {renamed} has {", ".join(sorted(unknown))}, which code every console runs '
                         'reads: define it in source/net/NetLink.cpp and add it to PROGRAM_GIVES')
            for name in sorted(shared_data):
                subprocess.run([tool('objcopy'), '--weaken-symbol=' + name, obj], check=True)
                print(f'wifi_twl: {name} weak in {renamed} (the program gives it)')
        tmp = out + '.tmp'
        if os.path.exists(tmp):
            os.remove(tmp)
        subprocess.run([tool('ar'), 'rcs', os.path.abspath(tmp)] + names, check=True, cwd=work)
        os.replace(tmp, out)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    twl = sum(1 for n in names if n.endswith('.twl.o'))
    print(f'wifi_twl: {twl} of {len(names)} wireless objects DSi-only -> {out}')
    return out


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    make(sys.argv[1], sys.argv[2])
