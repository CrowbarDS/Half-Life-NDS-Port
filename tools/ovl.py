#!/usr/bin/env python3
"""ARM9 overlays: code and data that share one region of main RAM, a phase at
a time, loaded from the ROM when the phase begins (source/Overlay.hpp).

BlocksDS has no overlays; this is ours. A REGION sits in the program, just
before __end__ (where a DS's heap begins), as large as its largest overlay:
what the program saves is the sum of a region's overlays less that one. An
overlay is code (and its constants) or zeroed data, by whole files or by
section attribute (HL_OVL_CODE / HL_OVL_BSS, source/Overlay.hpp).

  ovl.py ld --template ds_arm9.ld --specs ds_arm9.specs --manifest M --out-dir D
        the linker script (D/hl_arm9.ld): the installed BlocksDS one with the
        regions put in, the overlay files left out of the program's own
        sections, and a program header per overlay flagged 0x200007 -- which
        ndstool leaves out of the ARM9 image, as it does devkitPro's
        overlays; the specs that link with it (D/hl_arm9_link.specs); and the
        list for the loader (D/OverlayList.gen.hpp).
  ovl.py check --elf E --map MAP --manifest M
        what the link made of it, against the rules below; the link fails on
        any (the ELF must be linked with --emit-relocs).
  ovl.py pack --elf E --manifest M --out F
        the overlays' bytes, one file (F, put in NitroFS as ovl/arm9.ovl) with
        a table at its head (see PACK below).
  ovl.py romcheck --rom R --elf E
        ndstool left the overlays out: the ROM's ARM9 image ends before the
        first region.
  ovl.py --self-test

THE MANIFEST (JSON):
  {"regions": [{"name": "A", "overlays": [
       {"name": "front", "id": "Front", "mode": "code",
        "files": ["*/source/render/ProfileViewBody.cpp.o"], "callbacks": []},
       {"name": "map", "id": "Map", "mode": "bss", "sizer": true}]}],
   "gate": "_ZN4hlrt3ovl7missing",
   "load": ["_ZN4hlrt3ovl4load", "_ZN4hlrt3ovl7tryLoad"]}
  mode: "code" (the files' code and constants: their data and zeroed data
  stay in the program), "full" (all of the files), "bss" (zeroed data only,
  HL_OVL_BSS). sizer: the overlay the region is sized by -- it must be the
  largest (so that, say, the menus never decide a map's memory). gate, load:
  the start of the mangled names of hlrt::ovl::missing, and of
  hlrt::ovl::load and tryLoad.

THE RULES (check):
  R0  what an overlay is made of is in it: its files' sections in .ovl.*,
      none in the program's own; no static constructors or destructors, no
      exception tables, no atexit from an overlay's files.
  R1  the program refers into an overlay only from a gate: a function of
      its own section (HL_OVL_GATED: .text.ovlgate.*) that checks the
      overlay is loaded (HL_OVL_GATE: or a halt that names it); the
      program's data never points into one.
  R2  no overlay refers to another of its region.
  R3  an overlay function whose address is taken is a declared callback.
  R4  an overlay never calls ovl::load or tryLoad (it would replace itself).
  R5  each region ends at or before __end__, aligned to 32; its sizer is
      its largest overlay.

PACK (little-endian): "HLOV", u16 version (1), u16 count, 8 bytes program
ID, u32 0, pad to 32; then per overlay 48 bytes: name[16] (NUL padded),
u32 region, u32 address, u32 loaded bytes, u32 zeroed bytes, u32 offset in
the file (512-aligned), u32 sum of its loaded words, u32 0, u32 0; then the
overlays' bytes.
"""
import fnmatch
import json
import os
import re
import struct
import sys

PACK_VERSION = 1
PACK_HEAD = 32
PACK_ENTRY = 48
PACK_ALIGN = 512
OVERLAY_FLAGS = 0x200007     # ndstool leaves PT_LOAD segments with 0x200000 out
OVERLAY_LMA = 0xB0000000     # where the overlays are "stored" in the ELF only

# ------------------------------------------------------------------ the manifest


class Overlay:
    def __init__(self, region, index, d):
        self.region = region
        self.index = index
        self.name = d['name']
        self.id = d.get('id', self.name[:1].upper() + self.name[1:])
        self.mode = d.get('mode', 'code')
        self.files = list(d.get('files', []))
        self.callbacks = list(d.get('callbacks', []))
        self.sizer = bool(d.get('sizer', False))
        if not re.fullmatch(r'[a-z][a-z0-9_]*', self.name):
            raise ValueError('overlay name %r: lower case letters, digits, _' % self.name)
        if self.mode not in ('code', 'full', 'bss'):
            raise ValueError('overlay %s: mode %r' % (self.name, self.mode))
        if self.mode == 'bss' and self.files:
            raise ValueError('overlay %s: a bss overlay takes no files' % self.name)

    @property
    def loads(self):
        return self.mode != 'bss'


class Region:
    def __init__(self, index, d):
        self.index = index
        self.name = d['name']
        if not re.fullmatch(r'[A-Z][A-Za-z0-9]*', self.name):
            raise ValueError('region name %r' % self.name)
        self.overlays = [Overlay(self, i, o) for i, o in enumerate(d['overlays'])]
        sizers = [o for o in self.overlays if o.sizer]
        if len(sizers) != 1:
            raise ValueError('region %s: exactly one overlay is its sizer' % self.name)
        self.sizer = sizers[0]


class Manifest:
    def __init__(self, d):
        self.regions = [Region(i, r) for i, r in enumerate(d.get('regions', []))]
        self.gate = d.get('gate', '_ZN4hlrt3ovl7missing')
        load = d.get('load', ['_ZN4hlrt3ovl4load', '_ZN4hlrt3ovl7tryLoad'])
        self.load = [load] if isinstance(load, str) else list(load)
        names = [o.name for o in self.overlays]
        if len(names) != len(set(names)):
            raise ValueError('overlay names must differ')

    @property
    def overlays(self):
        return [o for r in self.regions for o in r.overlays]

    def files(self, modes):
        return [f for o in self.overlays if o.mode in modes for f in o.files]

    def overlay_of_file(self, path):
        for o in self.overlays:
            if any(fnmatch.fnmatch(path, f) for f in o.files):
                return o
        return None


def load_manifest(path):
    with open(path) as f:
        return Manifest(json.load(f))

# ------------------------------------------------------------------ ld: the linker script


def _exclude(text, section_names, old, add, expect_min):
    """In the output sections named `section_names`, add `add` to every
    EXCLUDE_FILE(`old`). At least `expect_min` must be found (else the
    installed script is not one this knows)."""
    if not add:
        return text
    out, current, done = [], None, 0
    for line in text.split('\n'):
        m = re.match(r'\s*(\.[A-Za-z_.]+)\b[^:;]*:', line)
        if m and not line.strip().startswith('*'):
            current = m.group(1)
        if current in section_names and ('EXCLUDE_FILE(%s)' % old) in line:
            line = line.replace('EXCLUDE_FILE(%s)' % old, 'EXCLUDE_FILE(%s %s)' % (old, ' '.join(add)))
            done += 1
        out.append(line)
    if done < expect_min:
        raise ValueError('linker script: %d of EXCLUDE_FILE(%s) in %s, %d expected at least'
                         % (done, old, '/'.join(section_names), expect_min))
    return '\n'.join(out)


def _once(text, anchor, insert, before=True):
    n = text.count(anchor)
    if n != 1:
        raise ValueError('linker script: %r found %d times, 1 expected' % (anchor, n))
    return text.replace(anchor, insert + anchor if before else anchor + insert)


def region_block(region):
    r = region.name
    lines = ['', '    /* ---- overlay region %s (tools/ovl.py) ---- */' % r,
             '    __ovl_%s_start = ALIGN(., 32);' % r]
    for o in region.overlays:
        n = o.name
        files = ' '.join(o.files)
        lines.append('    /* overlay %s (%s) */' % (n, o.mode))
        if o.loads:
            body = ['        __ovl_%s_start = ABSOLUTE(.);' % n,
                    '        *(.ovl.%s.text .ovl.%s.text.*)' % (n, n)]
            if files:
                body.append('        ' + ' '.join('%s(.text .stub .text.* .gnu.linkonce.t*)' % f for f in o.files))
            body.append('        *(.ovl.%s.rodata .ovl.%s.rodata.*)' % (n, n))
            if files:
                body.append('        ' + ' '.join('%s(.rodata .roda .rodata.* .gnu.linkonce.r*)' % f for f in o.files))
            body.append('        *(.ovl.%s.data .ovl.%s.data.*)' % (n, n))
            if o.mode == 'full' and files:
                body.append('        ' + ' '.join('%s(.data .data.* .gnu.linkonce.d*)' % f for f in o.files))
            body += ['        . = ALIGN(32);', '        __ovl_%s_load_end = ABSOLUTE(.);' % n]
            lines.append('    .ovl.%s __ovl_%s_start :' % (n, r))
            lines.append('    {')
            lines += body
            lines.append('    } AT>ovlmem :ovl_%s = 0xff' % n)
            bss_at = '__ovl_%s_load_end' % n
        else:
            lines.append('    __ovl_%s_start = __ovl_%s_start;' % (n, r))
            lines.append('    __ovl_%s_load_end = __ovl_%s_start;' % (n, r))
            bss_at = '__ovl_%s_start' % r
        bss = ['        *(.ovl.%s.bss .ovl.%s.bss.*)' % (n, n)]
        if o.mode == 'full' and files:
            bss.append('        ' + ' '.join('%s(.dynbss .bss .bss.* COMMON .gnu.linkonce.b*)' % f for f in o.files))
        lines.append('    .ovl.%s.bss %s (NOLOAD) :' % (n, bss_at))
        lines.append('    {')
        lines += bss
        lines += ['        . = ALIGN(32);', '        __ovl_%s_end = ABSOLUTE(.);' % n, '    } :NONE']
    sizes = ['(__ovl_%s_end - __ovl_%s_start)' % (o.name, r) for o in region.overlays]
    biggest = sizes[0]
    for s in sizes[1:]:
        biggest = 'MAX(%s, %s)' % (biggest, s)
    lines.append('    __ovl_%s_end = __ovl_%s_start + %s;' % (r, r, biggest))
    # The region, taken out of main RAM for good: what follows (and __end__)
    # comes after it.
    lines += ['    .ovlarea.%s __ovl_%s_start (NOLOAD) :' % (r, r), '    {',
              '        . = . + (__ovl_%s_end - __ovl_%s_start);' % (r, r), '    } >ewram :NONE']
    lines.append('')
    return '\n'.join(lines)


def make_ld(template, manifest):
    text = template
    code_files = manifest.files(('code', 'full'))
    full_files = manifest.files(('full',))
    # 1. Their sections out of the program's own (ld places an input section
    #    by the first statement that names it).
    text = _exclude(text, ('.text',), '*.itcm* *.twl*', code_files, 3 if code_files else 0)
    text = _exclude(text, ('.text', '.rodata'), '*.twl*', code_files, 4 if code_files else 0)
    text = _exclude(text, ('.data', '.bss', '.noinit'), '*.twl*', full_files, 3 if full_files else 0)
    if not manifest.regions:
        return text
    # 2. A program header per overlay that is loaded: ndstool leaves it out.
    phdrs = ''.join('    ovl_%s PT_LOAD FLAGS(0x%x);\n' % (o.name, OVERLAY_FLAGS)
                    for o in manifest.overlays if o.loads)
    m = re.search(r'PHDRS\s*\{[^}]*\n(\s*)\}', text)
    if not m:
        raise ValueError('linker script: no PHDRS block')
    text = text[:m.end() - len(m.group(1)) - 1] + phdrs + text[m.end() - len(m.group(1)) - 1:]
    # 3. Where they are "stored" in the ELF: apart from each other.
    text = _once(text, '\nSECTIONS\n', '\nMEMORY { ovlmem : ORIGIN = 0x%08X, LENGTH = 0x10000000 }\n' % OVERLAY_LMA)
    # 4. The regions, after the program's zeroed data, before __end__.
    blocks = ''.join(region_block(r) for r in manifest.regions)
    m = re.search(r'\n(\s*)/\*[^\n]*thread local storage[^\n]*\*/\s*\n\s*\.tls ALIGN\(4\) \(NOLOAD\) :', text)
    if m:
        text = text[:m.start()] + '\n' + blocks + text[m.start():]
    else:
        text = _once(text, '    .tls ALIGN(4) (NOLOAD) :', blocks)
    for r in manifest.regions:
        text += '\nASSERT(__ovl_%s_end <= __end__, "overlay region %s ends after __end__");\n' % (r.name, r.name)
        for o in r.overlays:
            if o is not r.sizer:
                text += ('ASSERT(__ovl_%s_end <= __ovl_%s_end, "overlay %s is larger than %s, which sizes the region");\n'
                         % (o.name, r.sizer.name, o.name, r.sizer.name))
    return text


def make_specs(specs_text, ld_path):
    """The installed specs' *link: with the script's path changed."""
    m = re.search(r'^\*link:\n(.*)$', specs_text, re.M)
    if not m:
        raise ValueError('specs: no *link:')
    line = m.group(1)
    new, n = re.subn(r'-T %:getenv\(BLOCKSDS /sys/crts/ds_arm9\.ld\)', '-T ' + ld_path, line)
    if n != 1:
        raise ValueError('specs: the *link: line does not name ds_arm9.ld once')
    return '*link:\n' + new + '\n'


def make_header(manifest, source):
    lines = ['// Made by tools/ovl.py from %s: do not edit.' % os.path.basename(source), '#pragma once', '',
             '// X(Region, index)', '#define HL_OVL_REGIONS(X) \\']
    lines += ['    X(%s, %d) \\' % (r.name, r.index) for r in manifest.regions]
    lines += ['', '// X(Id, name, Region, loads)', '#define HL_OVL_OVERLAYS(X) \\']
    lines += ['    X(%s, %s, %s, %d) \\' % (o.id, o.name, o.region.name, 1 if o.loads else 0) for o in manifest.overlays]
    lines += ['', '#define HL_OVL_COUNT %d' % len(manifest.overlays), '#define HL_OVL_REGION_COUNT %d' % len(manifest.regions), '']
    return '\n'.join(lines)


def cmd_ld(args):
    manifest = load_manifest(args['manifest'])
    out = args['out-dir']
    os.makedirs(out, exist_ok=True)
    with open(args['template']) as f:
        template = f.read()
    with open(args['specs']) as f:
        specs = f.read()
    ld_path = os.path.abspath(os.path.join(out, 'hl_arm9.ld'))
    _write_if_changed(ld_path, make_ld(template, manifest))
    _write_if_changed(os.path.join(out, 'hl_arm9_link.specs'), make_specs(specs, ld_path))
    _write_if_changed(os.path.join(out, 'OverlayList.gen.hpp'), make_header(manifest, args['manifest']))
    return 0


def _write_if_changed(path, text):
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open(path, 'w') as f:
        f.write(text)

# ------------------------------------------------------------------ reading an ELF


class Elf:
    def __init__(self, data):
        self.data = data
        if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
            raise ValueError('not a little-endian ELF32 file')
        (shoff,) = struct.unpack_from('<I', data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2E)
        raw = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize) for i in range(shnum)]
        names = raw[shstrndx]
        self.sections = []
        for i, s in enumerate(raw):
            self.sections.append({'index': i, 'name': self._str(names[4], s[0]), 'type': s[1], 'flags': s[2],
                                  'addr': s[3], 'offset': s[4], 'size': s[5], 'link': s[6], 'info': s[7]})
        self.symbols = []
        for s in self.sections:
            if s['type'] != 2:  # SHT_SYMTAB
                continue
            strtab = self.sections[s['link']]['offset']
            for at in range(s['offset'], s['offset'] + s['size'], 16):
                n, value, size, info, _other, shndx = struct.unpack_from('<IIIBBH', data, at)
                self.symbols.append({'name': self._str(strtab, n), 'value': value, 'size': size,
                                     'type': info & 15, 'bind': info >> 4, 'shndx': shndx})
        self.by_name = {}
        for s in self.symbols:
            self.by_name.setdefault(s['name'], s)

    def _str(self, base, at):
        end = self.data.index(b'\0', base + at)
        return self.data[base + at:end].decode('latin-1')

    def section(self, name):
        for s in self.sections:
            if s['name'] == name:
                return s
        return None

    def value(self, name):
        s = self.by_name.get(name)
        if s is None:
            raise ValueError('ELF: no symbol %s' % name)
        return s['value']

    def bytes_of(self, section):
        return self.data[section['offset']:section['offset'] + section['size']]

    def relocations(self):
        """(site section, r_offset, symbol, type) for every REL entry."""
        for s in self.sections:
            if s['type'] != 9:  # SHT_REL
                continue
            target = self.sections[s['info']]
            symtab = s['link']
            sym_base = [x for x in self.sections if x['index'] == symtab][0]
            for at in range(s['offset'], s['offset'] + s['size'], 8):
                r_offset, r_info = struct.unpack_from('<II', self.data, at)
                sym_index = r_info >> 8
                sym = self.symbols_of(sym_base)[sym_index] if sym_index else None
                yield target, r_offset, sym, r_info & 0xFF

    _symcache = None

    def symbols_of(self, symtab):
        if self._symcache is None:
            self._symcache = {}
        key = symtab['index']
        if key not in self._symcache:
            strtab = self.sections[symtab['link']]['offset']
            out = []
            for at in range(symtab['offset'], symtab['offset'] + symtab['size'], 16):
                n, value, size, info, _other, shndx = struct.unpack_from('<IIIBBH', self.data, at)
                out.append({'name': self._str(strtab, n), 'value': value, 'size': size,
                            'type': info & 15, 'bind': info >> 4, 'shndx': shndx})
            self._symcache[key] = out
        return self._symcache[key]

    def word_at(self, section, vma):
        off = section['offset'] + (vma - section['addr'])
        return struct.unpack_from('<I', self.data, off)[0]

# ------------------------------------------------------------------ branch targets


R_ARM_ABS32, R_ARM_REL32, R_ARM_THM_CALL, R_ARM_CALL, R_ARM_JUMP24, R_ARM_THM_JUMP24, R_ARM_TARGET1 = 2, 3, 10, 28, 29, 30, 38


def arm_branch_target(word, at):
    """Where an ARM B/BL/BLX at `at` goes."""
    imm = word & 0x00FFFFFF
    if imm & 0x00800000:
        imm -= 0x01000000
    target = at + 8 + (imm << 2)
    if (word >> 28) == 0xF:  # BLX: to Thumb, with the H bit
        target += ((word >> 24) & 1) << 1
    return target & 0xFFFFFFFF


def thumb_bl_target(pair, at):
    """Where a Thumb BL/BLX pair (ARMv5: two halfwords) at `at` goes."""
    hi, lo = pair & 0xFFFF, pair >> 16
    offset = ((hi & 0x7FF) << 12) | ((lo & 0x7FF) << 1)
    if offset & 0x400000:
        offset -= 0x800000
    target = (at + 4 + offset) & 0xFFFFFFFF
    if (lo >> 11) == 0x1D:  # BLX: to ARM, word aligned
        target &= ~3
    return target

# ------------------------------------------------------------------ check


def parse_map(text):
    """(output section, input section, object, address, size) for each input
    line of a GNU ld map file's memory map."""
    out, current, pending = [], None, None
    started = False
    for line in text.split('\n'):
        if line.startswith('Linker script and memory map'):
            started = True
            continue
        if not started:
            continue
        m = re.match(r'^(\.\S+|\S+)\s', line) or re.match(r'^(\.\S+)$', line)
        if m and not line.startswith(' '):
            current = m.group(1)
            continue
        m = re.match(r'^ (\.\S+|COMMON)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)', line)
        if m:
            out.append((current, m.group(1), m.group(4), int(m.group(2), 16), int(m.group(3), 16)))
            pending = None
            continue
        m = re.match(r'^ (\.\S+|COMMON)$', line)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)', line)
        if m and pending:
            out.append((current, pending, m.group(3), int(m.group(1), 16), int(m.group(2), 16)))
            pending = None
    return out


def _kind(input_section):
    s = input_section
    for k, prefixes in (('text', ('.text', '.stub', '.gnu.linkonce.t', '.glue_7', '.v4_bx', '.vfp11')),
                        ('rodata', ('.rodata', '.roda', '.gnu.linkonce.r')),
                        ('data', ('.data', '.gnu.linkonce.d')),
                        ('bss', ('.bss', '.dynbss', 'COMMON', '.gnu.linkonce.b', '.noinit')),
                        ('init', ('.init_array', '.ctors', '.fini_array', '.dtors', '.preinit_array')),
                        ('exidx', ('.ARM.exidx', '.ARM.extab'))):
        if s.startswith(prefixes):
            return k
    return None


def check(elf, map_text, manifest):
    errors = []
    overlays = {o.name: o for o in manifest.overlays}

    def overlay_of_section(name):
        m = re.match(r'^\.ovl\.([a-z][a-z0-9_]*)(\.bss)?$', name or '')
        return overlays.get(m.group(1)) if m else None

    # R0: placement, from the map. (And where the gates are: HL_OVL_GATED.)
    placed = parse_map(map_text)
    gates = [(addr, addr + size) for _o, in_sec, _obj, addr, size in placed if in_sec.startswith('.text.ovlgate')]
    for out_sec, in_sec, obj, _addr, _size in placed:
        o = manifest.overlay_of_file(obj)
        if o is None:
            continue
        kind = _kind(in_sec)
        if kind in ('init', 'exidx'):
            errors.append('R0 %s: %s in %s (no static constructors, destructors or exception tables in an overlay)'
                          % (o.name, in_sec, obj))
            continue
        wanted = ('text', 'rodata') + (('data', 'bss') if o.mode == 'full' else ())
        if kind in wanted and overlay_of_section(out_sec) is not o:
            errors.append('R0 %s: %s of %s landed in %s' % (o.name, in_sec, obj, out_sec))

    # Where each overlay is.
    ranges = []
    for o in manifest.overlays:
        start, end = elf.value('__ovl_%s_start' % o.name), elf.value('__ovl_%s_end' % o.name)
        ranges.append((start, end, o))
    region_ranges = [(elf.value('__ovl_%s_start' % r.name), elf.value('__ovl_%s_end' % r.name), r)
                     for r in manifest.regions]

    def in_region(addr):
        for start, end, r in region_ranges:
            if start <= addr < end:
                return r
        return None

    def func_at(section, vma):
        best = None
        for s in elf.symbols:
            if s['shndx'] == section['index'] and s['type'] == 2 and s['value'] & ~1 <= vma < (s['value'] & ~1) + max(s['size'], 1):
                best = s
        return best

    # Functions that call the gate, and what refers where.
    gated = set()
    refs = []
    for site_sec, at, sym, rtype in elf.relocations():
        if not (site_sec['flags'] & 2):  # SHF_ALLOC
            continue
        if sym and sym['name'].startswith(manifest.gate):
            f = func_at(site_sec, at)
            if f:
                gated.add((site_sec['index'], f['value']))
        refs.append((site_sec, at, sym, rtype))

    for site_sec, at, sym, rtype in refs:
        site_ovl = overlay_of_section(site_sec['name'])
        target_ovl = None
        if sym is not None and sym['shndx'] not in (0, 0xFFF1) and sym['shndx'] < len(elf.sections):
            target_ovl = overlay_of_section(elf.sections[sym['shndx']]['name'])
        # (A branch's own target, as linked: a veneer or anything the symbol
        # does not say.)
        decoded = None
        try:
            word = elf.word_at(site_sec, at)
            if rtype in (R_ARM_CALL, R_ARM_JUMP24):
                decoded = arm_branch_target(word, at)
            elif rtype == R_ARM_THM_CALL:
                decoded = thumb_bl_target(word, at)
        except (struct.error, IndexError):
            pass
        sym_name = sym['name'] if sym else '?'
        if site_ovl is not None:
            if sym and any(sym['name'].startswith(name) for name in manifest.load):
                errors.append('R4 %s: calls ovl::load at %08x' % (site_ovl.name, at))
            if sym and sym['name'] in ('__aeabi_atexit', '__cxa_atexit', 'atexit'):
                errors.append('R0 %s: registers an atexit destructor at %08x' % (site_ovl.name, at))
            if target_ovl is not None and target_ovl is not site_ovl and target_ovl.region is site_ovl.region:
                errors.append('R2 %s: refers to %s (%s) at %08x' % (site_ovl.name, target_ovl.name, sym_name, at))
            if (target_ovl is site_ovl and rtype in (R_ARM_ABS32, R_ARM_TARGET1) and sym and sym['type'] == 2
                    and not any(fnmatch.fnmatch(sym['name'], c) for c in site_ovl.callbacks)):
                errors.append('R3 %s: the address of %s is taken at %08x; declare it a callback'
                              % (site_ovl.name, sym['name'], at))
            continue
        # The program's own: into an overlay only through a gate.
        lands = target_ovl is not None or (decoded is not None and in_region(decoded) is not None)
        if not lands:
            continue
        what = target_ovl.name if target_ovl else 'region %s' % in_region(decoded).name
        if not (site_sec['flags'] & 4):  # SHF_EXECINSTR
            errors.append('R1 data at %08x (%s) points into %s (%s)' % (at, site_sec['name'], what, sym_name))
            continue
        f = func_at(site_sec, at)
        if not any(lo <= at < hi for lo, hi in gates):
            errors.append('R1 %s refers into %s (%s), and is not a gate (HL_OVL_GATED)'
                          % (f['name'] if f else '%08x' % at, what, sym_name))
        elif f is None or (site_sec['index'], f['value']) not in gated:
            errors.append('R1 %s refers into %s (%s) without checking it is loaded (HL_OVL_GATE)'
                          % (f['name'] if f else '%08x' % at, what, sym_name))

    # R5: sizes.
    end = elf.value('__end__')
    for r in manifest.regions:
        start, rend = elf.value('__ovl_%s_start' % r.name), elf.value('__ovl_%s_end' % r.name)
        if start & 31:
            errors.append('R5 region %s starts at %08x, not 32-aligned' % (r.name, start))
        if rend > end:
            errors.append('R5 region %s ends at %08x, after __end__ %08x' % (r.name, rend, end))
        sizer = elf.value('__ovl_%s_end' % r.sizer.name)
        for o in r.overlays:
            if elf.value('__ovl_%s_end' % o.name) > sizer:
                errors.append('R5 overlay %s is larger than %s, which sizes the region' % (o.name, r.sizer.name))
    return errors


def overlay_sizes(elf, manifest):
    out = []
    for o in manifest.overlays:
        start = elf.value('__ovl_%s_start' % o.name)
        load_end = elf.value('__ovl_%s_load_end' % o.name)
        end = elf.value('__ovl_%s_end' % o.name)
        out.append((o, start, load_end - start, end - load_end))
    return out


def cmd_check(args):
    manifest = load_manifest(args['manifest'])
    with open(args['elf'], 'rb') as f:
        elf = Elf(f.read())
    with open(args['map']) as f:
        map_text = f.read()
    if not any(s['type'] == 9 for s in elf.sections):
        print('ovl.py check: %s has no relocations: link it with -Wl,--emit-relocs' % args['elf'], file=sys.stderr)
        return 1
    errors = check(elf, map_text, manifest)
    for e in errors:
        print('ovl.py check: ' + e, file=sys.stderr)
    saved = 0
    for r in manifest.regions:
        size = elf.value('__ovl_%s_end' % r.name) - elf.value('__ovl_%s_start' % r.name)
        total = sum(load + bss for o, _s, load, bss in overlay_sizes(elf, manifest) if o.region is r)
        saved += total - size
    print('ovl.py check: %d region(s), %d overlay(s), %d bytes saved%s'
          % (len(manifest.regions), len(manifest.overlays), saved, '' if not errors else '; %d problem(s)' % len(errors)))
    return 1 if errors else 0

# ------------------------------------------------------------------ pack


def word_sum(data):
    total = 0
    for i in range(0, len(data) - len(data) % 4, 4):
        total = (total + struct.unpack_from('<I', data, i)[0]) & 0xFFFFFFFF
    return total


def program_id(elf):
    s = elf.by_name.get('hlProgramId')
    if s is None or s['shndx'] in (0, 0xFFF1) or s['shndx'] >= len(elf.sections):
        return b'\0' * 8
    sec = elf.sections[s['shndx']]
    if sec['type'] == 8:
        return b'\0' * 8
    off = sec['offset'] + s['value'] - sec['addr']
    return elf.data[off:off + 8]


def make_pack(elf, manifest):
    entries, blobs = [], []
    offset = PACK_HEAD + PACK_ENTRY * len(manifest.overlays)
    offset = (offset + PACK_ALIGN - 1) // PACK_ALIGN * PACK_ALIGN
    for o, start, load, bss in overlay_sizes(elf, manifest):
        data = b''
        if o.loads and load:
            sec = elf.section('.ovl.%s' % o.name)
            if sec is None or sec['addr'] != start:
                raise ValueError('pack: no section .ovl.%s at %08x' % (o.name, start))
            data = elf.bytes_of(sec)[:load]
            if len(data) != load:
                raise ValueError('pack: .ovl.%s holds %d bytes, %d expected' % (o.name, len(data), load))
        at = offset if data else 0
        entries.append(struct.pack('<16sIIIIIIII', o.name.encode(), o.region.index, start, len(data), bss, at,
                                   word_sum(data), 0, 0))
        if data:
            blobs.append((at, data))
            offset = (at + len(data) + PACK_ALIGN - 1) // PACK_ALIGN * PACK_ALIGN
    head = struct.pack('<4sHH8sI', b'HLOV', PACK_VERSION, len(entries), program_id(elf), 0)
    out = bytearray(head + b'\0' * (PACK_HEAD - len(head)) + b''.join(entries))
    for at, data in blobs:
        out += b'\0' * (at - len(out))
        out += data
    return bytes(out)


def read_pack(data):
    magic, version, count, pid, _z = struct.unpack_from('<4sHH8sI', data, 0)
    if magic != b'HLOV' or version != PACK_VERSION:
        raise ValueError('not an overlay pack of version %d' % PACK_VERSION)
    out = []
    for i in range(count):
        name, region, addr, load, bss, at, total, _a, _b = struct.unpack_from('<16sIIIIIIII', data, PACK_HEAD + i * PACK_ENTRY)
        out.append({'name': name.rstrip(b'\0').decode(), 'region': region, 'addr': addr, 'load': load, 'bss': bss,
                    'offset': at, 'sum': total, 'data': data[at:at + load]})
    return pid, out


def cmd_pack(args):
    manifest = load_manifest(args['manifest'])
    with open(args['elf'], 'rb') as f:
        elf = Elf(f.read())
    data = make_pack(elf, manifest)
    os.makedirs(os.path.dirname(os.path.abspath(args['out'])), exist_ok=True)
    try:
        with open(args['out'], 'rb') as f:
            if f.read() == data:
                return 0
    except OSError:
        pass
    with open(args['out'], 'wb') as f:
        f.write(data)
    for o, start, load, bss in overlay_sizes(elf, manifest):
        print('ovl.py pack: %-12s region %s at %08x: %6d bytes loaded, %6d zeroed' % (o.name, o.region.name, start, load, bss))
    return 0

# ------------------------------------------------------------------ romcheck


def cmd_romcheck(args):
    with open(args['rom'], 'rb') as f:
        head = f.read(0x40)
    with open(args['elf'], 'rb') as f:
        elf = Elf(f.read())
    ram, size = struct.unpack_from('<II', head, 0x28)
    end = ram + size
    bad = [r for r in elf.symbols if re.fullmatch(r'__ovl_[A-Z][A-Za-z0-9]*_start', r['name'])
           and end > r['value'] and r['value'] >= ram]
    # (The ARM9 image is the program up to its data, and the ITCM's bytes
    # after them: it may reach past a region's start only by the ITCM's.)
    itcm = elf.value('__itcm_end') - elf.value('__itcm_start') if '__itcm_end' in elf.by_name else 0
    bad = [r for r in bad if end - itcm > r['value']]
    if bad:
        print('ovl.py romcheck: the ROM\'s ARM9 image (%08x..%08x) reaches into %s: ndstool kept the overlays'
              % (ram, end, ', '.join(r['name'] for r in bad)), file=sys.stderr)
        return 1
    return 0

# ------------------------------------------------------------------ self test


_TEMPLATE = """PHDRS {
    main    PT_LOAD FLAGS(7);
    twl     PT_LOAD FLAGS(0x100007);
}

SECTIONS
{
    .text :
    {
        *(EXCLUDE_FILE(*.itcm* *.twl*) .text)
        *(EXCLUDE_FILE(*.itcm* *.twl*) .stub)
        *(EXCLUDE_FILE(*.itcm* *.twl*) .text.*)
        *(EXCLUDE_FILE(*.twl*) .gnu.warning)
        *(EXCLUDE_FILE(*.twl*) .gnu.linkonce.t*)
    } >ewram :main = 0xff
    .rodata :
    {
        *(EXCLUDE_FILE(*.twl*) .rodata)
        *(EXCLUDE_FILE(*.twl*) .roda)
        *(EXCLUDE_FILE(*.twl*) .rodata.*)
    } >ewram :main = 0xff
    .data ALIGN(4) :
    {
        *(EXCLUDE_FILE(*.twl*) .data)
        *(EXCLUDE_FILE(*.twl*) .data.*)
    } >ewram :main = 0xff
    .bss __bss_vma (NOLOAD):
    {
        *(EXCLUDE_FILE(*.twl*) .bss*)
    } >ewram :NONE
    .noinit (NOLOAD):
    {
        *(EXCLUDE_FILE(*.twl*) .noinit)
    } >ewram :NONE

    /* Space reserved for the thread local storage of main() */
    .tls ALIGN(4) (NOLOAD) :
    {
        . = . + 4;
    } >ewram :NONE
}
"""

_SPECS = """*link:
%(blocksds_link) -T %:getenv(BLOCKSDS /sys/crts/ds_arm9.mem) -T %:getenv(BLOCKSDS /sys/crts/ds_arm9.ld) --gc-sections
"""


def self_test():
    failures = []

    def expect(cond, what):
        if not cond:
            failures.append(what)

    m = Manifest({'regions': [{'name': 'A', 'overlays': [
        {'name': 'one', 'mode': 'code', 'files': ['*/one.cpp.o']},
        {'name': 'two', 'mode': 'full', 'files': ['*/two.cpp.o']},
        {'name': 'buf', 'mode': 'bss', 'sizer': True}]}]})
    ld = make_ld(_TEMPLATE, m)
    expect(ld.count('*/one.cpp.o */two.cpp.o') >= 7, 'the overlay files are left out of .text and .rodata')
    expect('EXCLUDE_FILE(*.twl* */two.cpp.o) .data' in ld, 'a full overlay\'s files are left out of .data')
    expect('ovl_one PT_LOAD FLAGS(0x200007)' in ld and 'ovl_buf PT_LOAD' not in ld, 'a header per loaded overlay')
    expect(ld.index('.ovl.one ') < ld.index('.tls ALIGN(4)'), 'the region comes before .tls (and __end__)')
    expect('ASSERT(__ovl_one_end <= __ovl_buf_end' in ld, 'the sizer is checked by the link')
    expect('ASSERT(__ovl_A_end <= __end__' in ld, 'the region is checked against __end__')
    try:
        make_ld(_TEMPLATE.replace('EXCLUDE_FILE(*.itcm* *.twl*)', 'EXCLUDE_FILE(*.itcm*)'), m)
        expect(False, 'a script it does not know is refused')
    except ValueError:
        pass
    expect(make_ld(_TEMPLATE, Manifest({})) == _TEMPLATE, 'no overlays: the script unchanged')
    specs = make_specs(_SPECS, '/x/hl_arm9.ld')
    expect('-T /x/hl_arm9.ld' in specs and 'ds_arm9.mem' in specs and 'ds_arm9.ld' not in specs,
           'the specs name the new script, and the memory map still')
    hdr = make_header(m, 'm.json')
    expect('X(One, one, A, 1)' in hdr and 'X(Buf, buf, A, 0)' in hdr and 'HL_OVL_COUNT 3' in hdr, 'the list header')
    for bad in ({'regions': [{'name': 'A', 'overlays': [{'name': 'x'}]}]},
                {'regions': [{'name': 'A', 'overlays': [{'name': 'x', 'mode': 'bss', 'files': ['a'], 'sizer': True}]}]}):
        try:
            Manifest(bad)
            expect(False, 'a bad manifest is refused: %r' % bad)
        except ValueError:
            pass

    # Branches.
    expect(arm_branch_target(0xEB000000, 0x1000) == 0x1008, 'ARM BL +0')
    expect(arm_branch_target(0xEBFFFFFE, 0x1000) == 0x1000, 'ARM BL to itself')
    expect(arm_branch_target(0xFA000000, 0x1000) == 0x1008, 'ARM BLX')
    expect(arm_branch_target(0xFB000000, 0x1000) == 0x100A, 'ARM BLX, H set')
    # Thumb BL +0x100: hi = F000, lo = F880 (offset 0x100 >> 1 = 0x80).
    expect(thumb_bl_target(0xF880F000, 0x2000) == 0x2104, 'Thumb BL forwards')
    # Thumb BL -4: offset -4 -> hi 0x7FF, lo 0x7FE.
    expect(thumb_bl_target(0xFFFEF7FF, 0x2000) == 0x2000, 'Thumb BL back to itself')
    expect(thumb_bl_target(0xE880F000, 0x2002) == 0x2104, 'Thumb BLX: word aligned')

    # The map.
    mp = ('Linker script and memory map\n\n.text           0x02000000      0x100\n'
          ' .text.f        0x02000000       0x20 build/a/one.cpp.o\n'
          ' .text.longname_of_a_function\n                0x02000020       0x20 build/a/main.cpp.o\n'
          '.ovl.one        0x02100000       0x40 load address 0xb0000000\n'
          ' .text.g        0x02100000       0x20 build/a/one.cpp.o\n')
    parsed = parse_map(mp)
    expect(('.text', '.text.f', 'build/a/one.cpp.o', 0x02000000, 0x20) in parsed
           and ('.ovl.one', '.text.g', 'build/a/one.cpp.o', 0x02100000, 0x20) in parsed
           and ('.text', '.text.longname_of_a_function', 'build/a/main.cpp.o', 0x02000020, 0x20) in parsed,
           'the map is read, with where and how large')

    # Pack, and back.
    class FakeElf:
        def __init__(self):
            self.data = bytes(range(64)) * 2
            self.by_name = {}
            self.sections = []

        def value(self, name):
            return {'__ovl_one_start': 0x2100000, '__ovl_one_load_end': 0x2100040, '__ovl_one_end': 0x2100060,
                    '__ovl_two_start': 0x2100000, '__ovl_two_load_end': 0x2100020, '__ovl_two_end': 0x2100020,
                    '__ovl_buf_start': 0x2100000, '__ovl_buf_load_end': 0x2100000, '__ovl_buf_end': 0x2100080}[name]

        def section(self, name):
            return {'.ovl.one': {'addr': 0x2100000, 'offset': 0, 'size': 64},
                    '.ovl.two': {'addr': 0x2100000, 'offset': 64, 'size': 32}}.get(name)

        def bytes_of(self, s):
            return self.data[s['offset']:s['offset'] + s['size']]

    pack = make_pack(FakeElf(), m)
    _pid, entries = read_pack(pack)
    expect([e['name'] for e in entries] == ['one', 'two', 'buf'], 'the pack lists every overlay')
    expect(entries[0]['data'] == bytes(range(64)) and entries[0]['offset'] % PACK_ALIGN == 0, 'an overlay\'s bytes, aligned')
    expect(entries[0]['sum'] == word_sum(bytes(range(64))) and entries[2]['load'] == 0 and entries[2]['bss'] == 0x80,
           'its sum; a bss overlay loads nothing')

    for f in failures:
        print('ovl.py self-test FAILED: ' + f, file=sys.stderr)
    print('ovl.py self-test: %s' % ('ok' if not failures else '%d failed' % len(failures)))
    return 1 if failures else 0

# ------------------------------------------------------------------ main


def parse_args(argv, wanted):
    out, i = {}, 0
    while i < len(argv):
        a = argv[i]
        if not a.startswith('--') or a[2:] not in wanted or i + 1 >= len(argv):
            raise SystemExit('ovl.py: unexpected %r (wants %s)' % (a, ', '.join('--' + w for w in wanted)))
        out[a[2:]] = argv[i + 1]
        i += 2
    missing = [w for w in wanted if w not in out]
    if missing:
        raise SystemExit('ovl.py: missing %s' % ', '.join('--' + w for w in missing))
    return out


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    cmd, rest = argv[0], argv[1:]
    try:
        if cmd == '--self-test':
            return self_test()
        if cmd == 'ld':
            return cmd_ld(parse_args(rest, ('template', 'specs', 'manifest', 'out-dir')))
        if cmd == 'check':
            return cmd_check(parse_args(rest, ('elf', 'map', 'manifest')))
        if cmd == 'pack':
            return cmd_pack(parse_args(rest, ('elf', 'manifest', 'out')))
        if cmd == 'romcheck':
            return cmd_romcheck(parse_args(rest, ('rom', 'elf')))
    except ValueError as e:
        print('ovl.py %s: %s' % (cmd, e), file=sys.stderr)
        return 1
    print('ovl.py: unknown command %r' % cmd, file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
