#!/usr/bin/env python3
"""Take apart and put back together a 3DS TwlBg module, on a PC.

TwlBg is the 3DS-side program of DSi mode: it scales and shows the DS screens.
Our 3DS patcher (tools/3ds/twlbg-patcher) changes its code on the console; this
is the same unpacking on a PC, to study a dump and to check the packer.

The dump is the owner's own, never committed (tools/3ds/dumps/ is ignored):
GodMode9, CTRNAND title/00040138/00000102 (Old 3DS) or 20000102 (New 3DS), the
lowest-numbered .app, "NCCH image options" -> "Mount image to drive", then copy
exefs/.firm (TWL_FIRM). This takes that FIRM, the ExeFS holding it, or the
TwlBg NCCH itself.

    twlbg.py info    DUMP                 what it is: names, sizes, code layout
    twlbg.py unpack  DUMP OUTDIR          code.bin (unpacked), text/ro/data parts
    twlbg.py check   DUMP                 unpack, pack again, unpack: same code?
    twlbg.py disasm  DUMP OUT.txt [--arm] the text part as Thumb (or ARM)
    twlbg.py places  DUMP                 where our patch would go
    twlbg.py build   DUMP PAYLOAD OUT.cxi our patched TwlBg (for testing only:
                                          it holds Nintendo's code, never share)
    twlbg.py selftest                     the packer on made-up data

The code's packing is the 3DS's backward LZ: the file is unpacked from its end
towards its start, in place, so a packer must never let the writing catch up
with the reading. That rule is checked here on every pack.
"""

import os
import random
import struct
import subprocess
import sys

MEDIA = 0x200


# --- Backward LZ ------------------------------------------------------------

def lz_unpack(packed):
    """The unpacked bytes of PACKED (a whole .code as stored)."""
    n = len(packed)
    if n < 8:
        raise ValueError("too short for the footer")
    top_bottom, extra = struct.unpack_from("<II", packed, n - 8)
    footer = top_bottom >> 24
    region = top_bottom & 0xFFFFFF
    if footer < 8 or region > n or footer > region:
        raise ValueError("bad footer (%08X %08X)" % (top_bottom, extra))
    size = n + extra
    out = bytearray(size)
    out[:n] = packed
    src = n - footer          # the next packed byte is out[src - 1]
    stop = n - region         # bytes below this were stored as they are
    dst = size
    while src > stop:
        src -= 1
        flags = out[src]
        for _ in range(8):
            if src <= stop:
                break
            if flags & 0x80:
                src -= 2
                pair = out[src] | out[src + 1] << 8
                length = (pair >> 12) + 3
                back = (pair & 0xFFF) + 3
                for _ in range(length):
                    dst -= 1
                    if dst + back >= size:
                        raise ValueError("copy from beyond the end")
                    out[dst] = out[dst + back]
            else:
                src -= 1
                dst -= 1
                out[dst] = out[src]
            if dst < src:
                raise ValueError("writing caught up with reading")
            flags = (flags << 1) & 0xFF
    if dst != stop:
        raise ValueError("ended at %X, not %X" % (dst, stop))
    return bytes(out)


def _longest(data, lo, max_chain):
    """For each top end t in (lo, n]: the longest copy (length, back) that
    can write data[t-length:t] from bytes above, or (0, 0)."""
    n = len(data)
    best = [(0, 0)] * (n + 1)
    heads = {}
    added = n - 2            # 3-byte runs starting at q >= added are indexed
    for t in range(n, lo + 2, -1):
        # A copy's top three bytes data[t-3:t] come from data[q:q+3], q >= t.
        while added > t:
            added -= 1
            heads.setdefault(data[added:added + 3], []).append(added)
        cands = heads.get(data[t - 3:t])
        if not cands:
            continue
        limit = min(18, t - lo)
        blen, bback = 0, 0
        tried = 0
        for i in range(len(cands) - 1, -1, -1):     # nearest first
            back = cands[i] - (t - 3)
            if back > 0xFFF + 3:
                break
            length = 3
            while length < limit and data[t - 1 - length] == data[t - 1 - length + back]:
                length += 1
            if length > blen:
                blen, bback = length, back
                if length == limit:
                    break
            tried += 1
            if tried >= max_chain:
                break
        best[t] = (blen, bback)
    return best


def _tokens_backward(data, lo, max_chain=256):
    """The cheapest tokens for data[lo:], top token first.

    Each token is a literal byte or (length, back): it writes length bytes
    downwards, each from back bytes above, as the unpacker does. Cost in
    bits: a literal 9, a copy 17 (a flag bit each). Any length from 3 up to
    the longest found at a top end is allowed, with the same back."""
    n = len(data)
    longest = _longest(data, lo, max_chain)
    inf = 1 << 62
    cost = [inf] * (n + 1)
    step = [None] * (n + 1)   # step[p]: the token covering p upwards, (top, back)
    cost[n] = 0
    for t in range(n, lo, -1):
        c = cost[t]
        if c == inf:
            continue
        if c + 9 < cost[t - 1]:
            cost[t - 1] = c + 9
            step[t - 1] = (t, 0)
        length, back = longest[t]
        for ln in range(3, length + 1):
            if c + 17 < cost[t - ln]:
                cost[t - ln] = c + 17
                step[t - ln] = (t, back)
    toks = []
    p = lo
    while p < n:
        top, back = step[p]
        toks.append(data[p] if back == 0 else (top - p, back))
        p = top
    toks.reverse()
    return toks


def _stream(toks):
    """The packed bytes of TOKS, lowest address first, plus per-token
    (bytes written so far, packed bytes read so far) after each token."""
    rev = bytearray()            # packed bytes in reading order (top down)
    marks = []
    written = 0
    for g in range(0, len(toks), 8):
        group = toks[g:g + 8]
        flags = 0
        body = bytearray()
        for i, t in enumerate(group):
            if isinstance(t, tuple):
                length, back = t
                flags |= 0x80 >> i
                pair = (length - 3) << 12 | (back - 3)
                body += bytes((pair >> 8, pair & 0xFF))   # read high byte first
            else:
                body.append(t)
        rev.append(flags)
        base = len(rev)
        # Marks within the group: read count after each token.
        off = 0
        for t in group:
            if isinstance(t, tuple):
                off += 2
                written += t[0]
            else:
                off += 1
                written += 1
            marks.append((written, base + off))
        rev += body
    return bytes(reversed(rev)), marks


def lz_pack(data, size=None):
    """DATA packed so that the 3DS unpacks it in place, or None when packing
    gains nothing (the module then stores its code as it is). Whatever cannot
    be packed safely at the start is stored as it is, below the packed part.

    With SIZE, the result is exactly SIZE bytes (as TwlBg's own code fills its
    room in the file): more of the start is stored as it is, and what is left
    over goes in the footer, or None if it cannot be done."""
    n = len(data)
    lo = 0
    while True:
        toks = _tokens_backward(data, lo)
        stream, marks = _stream(toks)
        coded = n - lo
        # In place, after each token: the write position n - written must stay
        # at or above the read position lo + len(stream) - read.
        worst, worst_at = 0, 0
        for written, read in marks:
            over = (len(stream) - read) - (coded - written)
            if over > worst:
                worst, worst_at = over, written
        if worst == 0:
            break
        # Everything still unwritten at the worst point is stored as it is:
        # that lowers every earlier excess by the worst one, so this is safe.
        lo = n - worst_at
    if size is not None:
        return _pack_to(data, toks, marks, size)
    pad = (-(lo + len(stream))) & 3
    footer = 8 + pad
    total = lo + len(stream) + footer
    if not stream or total >= n:
        return None
    region = len(stream) + footer
    return data[:lo] + stream + b"\xFF" * pad + struct.pack("<II", footer << 24 | region, n - total)


def _pack_to(data, toks, marks, size):
    """The top K tokens of a safe parse, the rest stored as it is, padded in
    the footer to SIZE. Cutting after token K stays safe in place when the
    bytes read minus the bytes written there are no more than at any earlier
    token (or before the first)."""
    n = len(data)
    lowest = 0
    best = None
    for k, (written, read) in enumerate(marks, 1):
        if read - written > lowest:
            continue
        lowest = read - written
        footer = size - (n - written) - read
        if 8 <= footer <= 0xFF:
            best = (k, written, read, footer)   # the last fit packs the most
    if best is None:
        return None
    k, written, read, footer = best
    stream, _ = _stream(toks[:k])
    assert len(stream) == read
    lo = n - written
    region = read + footer
    out = data[:lo] + stream + b"\xFF" * (footer - 8) + struct.pack("<II", footer << 24 | region, n - size)
    assert len(out) == size
    return out


# --- NCCH / ExeFS -------------------------------------------------------------

class Module:
    def __init__(self, blob, base):
        self.blob = blob
        self.base = base
        h = blob[base:base + 0x200]
        if h[0x100:0x104] != b"NCCH":
            raise ValueError("no NCCH at %X" % base)
        self.size = struct.unpack_from("<I", h, 0x104)[0] * MEDIA
        self.exh_size = struct.unpack_from("<I", h, 0x180)[0]
        self.flags = h[0x188:0x190]
        self.exefs_off = struct.unpack_from("<I", h, 0x1A0)[0] * MEDIA
        self.exefs_size = struct.unpack_from("<I", h, 0x1A4)[0] * MEDIA
        if not (self.flags[7] & 4):
            raise ValueError("NCCH is encrypted; dump it decrypted")
        exh = blob[base + 0x200:base + 0x200 + 0x400]
        self.name = exh[0:8].rstrip(b"\0").decode("ascii", "replace")
        self.code_packed_flag = bool(exh[0xD] & 1)
        self.text = struct.unpack_from("<III", exh, 0x10)   # addr, pages, size
        self.ro = struct.unpack_from("<III", exh, 0x20)
        self.data = struct.unpack_from("<III", exh, 0x30)
        self.bss = struct.unpack_from("<I", exh, 0x3C)[0]
        self.files = []
        eb = base + self.exefs_off
        for i in range(10):
            name, off, size = struct.unpack_from("<8sII", blob, eb + i * 16)
            name = name.rstrip(b"\0").decode("ascii", "replace")
            if name:
                self.files.append((name, eb + 0x200 + off, size, i))

    def file(self, name):
        for f in self.files:
            if f[0] == name:
                return f
        raise KeyError(name)

    def code(self):
        _, off, size, _ = self.file(".code")
        raw = self.blob[off:off + size]
        return lz_unpack(raw) if self.code_packed_flag else raw


def _firm_module(blob, at):
    # Section table at 0x40, four entries of 0x30: offset, address, size.
    for i in range(4):
        off, addr, size = struct.unpack_from("<III", blob, at + 0x40 + i * 0x30)
        if size == 0:
            continue
        p = at + off
        while p < at + off + size:
            if blob[p + 0x100:p + 0x104] == b"NCCH":
                m = Module(blob, p)
                if m.name in ("TwlBg", "AgbBg"):
                    return m
                p += max(m.size, MEDIA)
            else:
                p += MEDIA
    return None


def find_module(blob):
    """The TwlBg NCCH in BLOB: the NCCH itself, a TWL_FIRM, the ExeFS that
    holds the FIRM, or the whole content (.app) when it is not encrypted."""
    for p in range(0, len(blob) - 0x200, MEDIA):
        if blob[p:p + 4] == b"FIRM":
            m = _firm_module(blob, p)
            if m:
                return m
        elif blob[p + 0x100:p + 0x104] == b"NCCH":
            try:
                m = Module(blob, p)
            except ValueError:
                continue
            if m.name in ("TwlBg", "AgbBg"):
                return m
    raise ValueError("no TwlBg found (an encrypted .app? use the mounted image's .firm)")


def load(path):
    with open(path, "rb") as f:
        return find_module(f.read())


# --- Commands -----------------------------------------------------------------

def cmd_info(path):
    m = load(path)
    print("module   %s (NCCH at %X, %X bytes)" % (m.name, m.base, m.size))
    for name, (addr, pages, size) in (("text", m.text), ("ro", m.ro), ("data", m.data)):
        print("%-8s %08X  %6X bytes  %3d pages" % (name, addr, size, pages))
    print("bss      %X bytes" % m.bss)
    print("code     %s" % ("packed" if m.code_packed_flag else "stored"))
    for name, off, size, i in m.files:
        print("exefs[%d] %-8s at %X, %X bytes" % (i, name, off, size))
    code = m.code()
    print("unpacked code %X bytes" % len(code))


def parts(m, code):
    """text, ro and data as (address, bytes). In a FIRM module they follow
    each other with no padding (TwlBg); in others each starts on a page."""
    segs = (("text", m.text), ("ro", m.ro), ("data", m.data))
    tight = sum(size for _, (_, _, size) in segs) == len(code)
    out = {}
    p = 0
    for name, (addr, pages, size) in segs:
        out[name] = (addr, code[p:p + size])
        p += size if tight else pages * 0x1000
    return out


def cmd_unpack(path, outdir):
    m = load(path)
    code = m.code()
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "code.bin"), "wb") as f:
        f.write(code)
    for name, (addr, b) in parts(m, code).items():
        with open(os.path.join(outdir, "%s_%08X.bin" % (name, addr)), "wb") as f:
            f.write(b)
    print("wrote %s" % outdir)


def cmd_check(path):
    m = load(path)
    _, off, size, _ = m.file(".code")
    code = m.code()
    packed = lz_pack(code)
    if packed is None:
        print("our packer gains nothing on this code")
        return 1
    again = lz_unpack(packed)
    same = again == code
    print("unpacked %X, stored %X, ours %X (%+d), same after a round trip: %s"
          % (len(code), size, len(packed), len(packed) - size, "yes" if same else "NO"))
    room = (m.exefs_size - 0x200) - (off - (m.base + m.exefs_off + 0x200))
    print("room for .code before the ExeFS ends: %X" % room)
    return 0 if same and len(packed) <= room else 1


def cmd_disasm(path, out, arm):
    m = load(path)
    code = m.code()
    addr, text = parts(m, code)["text"]
    tmp = out + ".bin"
    with open(tmp, "wb") as f:
        f.write(text)
    objdump = os.environ.get("OBJDUMP", "arm-none-eabi-objdump")
    args = [objdump, "-D", "-b", "binary", "-m", "arm", "--adjust-vma=0x%X" % addr]
    if not arm:
        args += ["-M", "force-thumb"]
    with open(out, "w") as f:
        subprocess.run(args + [tmp], stdout=f, check=True)
    os.remove(tmp)
    print("wrote %s" % out)


def cmd_selftest():
    rng = random.Random(1)
    cases = [b"", b"a", bytes(100), bytes(range(256)) * 40,
             bytes(rng.getrandbits(8) for _ in range(5000))]
    # Code-like: repeated words with variation, random head (incompressible).
    words = [rng.getrandbits(32) for _ in range(300)]
    body = b"".join(struct.pack("<I", words[rng.randrange(300)]) for _ in range(30000))
    cases.append(bytes(rng.getrandbits(8) for _ in range(3000)) + body)
    cases.append(body + bytes(rng.getrandbits(8) for _ in range(3000)))
    bad = 0
    for i, c in enumerate(cases):
        p = lz_pack(c)
        if p is None:
            print("case %d: %d bytes, stored (nothing to gain)" % (i, len(c)))
            continue
        ok = lz_unpack(p) == c
        bad += not ok
        print("case %d: %d -> %d bytes, %s" % (i, len(c), len(p), "ok" if ok else "WRONG"))
    return 1 if bad else 0


# --- Our patch -----------------------------------------------------------------
#
# Every place is found by its bytes and checked, never taken as a fixed
# address, so a TwlBg we do not know is refused instead of broken.

import hashlib
import re

TEXT = 0x300000          # where the code image starts
STATE = 0x201000         # RAM in the area TwlBg clears and leaves alone
HEAD_SIZE = 0x14         # feed.c's struct Head


def _find(code, pattern):
    """Offsets of PATTERN ("01 21 ?? 48 ...") in CODE."""
    rx = re.compile(b"".join(b"." if h == "??" else re.escape(bytes([int(h, 16)]))
                             for h in pattern.split()), re.S)
    return [m.start() for m in rx.finditer(code)]


def _one(code, pattern, what):
    hits = _find(code, pattern)
    if len(hits) != 1:
        raise ValueError("%s: found %d times, not once" % (what, len(hits)))
    return hits[0]


def _word(code, off):
    return struct.unpack_from("<I", code, off)[0]


def _low_block(code, text_size):
    """TwlBg copies the end of its text to 0x100000 at start: (start, size).
    Found as its literals: start, size, then 0x100000 + size (its bss)."""
    for off in range(0, text_size - 12, 4):
        src, size, bss = struct.unpack_from("<III", code, off)
        if bss == 0x100000 + size and TEXT < src and src + size == TEXT + text_size:
            return src, size
    raise ValueError("the copy to 0x100000 was not found")


class Places:
    """Where our patch goes in one TwlBg."""

    def __init__(self, m, code):
        text_size = m.text[2]
        low, low_size = _low_block(code, text_size)
        self.low, self.low_size = low, low_size

        def run(file_va):     # file address -> run-time address
            return file_va - (low - 0x100000) if file_va >= low else file_va

        # The frame's call to the input routine: a helper that loads a table
        # from its literal and calls the table's second word.
        h = _one(code, "01 21 04 48 89 02 10 B5 ?? ?? ?? ?? 02 48 40 68 80 47 10 BD",
                 "frame helper")
        self.hook = h + 0x18                      # its table literal
        self.table = _word(code, self.hook)
        if not 0x100000 <= self.table < 0x100000 + low_size + 0x10000:
            raise ValueError("frame helper's table %X is not in the low block" % self.table)

        # The input routine splits the circle pad's word into X and Y, kept
        # at +12 and +14 of a block it loads into r5 just before.
        p = _one(code, "61 68 08 05 00 0D A8 81 08 02", "circle pad split")
        self.pad = None
        for back in range(2, 0x80, 2):
            insn = struct.unpack_from("<H", code, p - back)[0]
            if insn >> 8 == 0x4D:                 # ldr r5, [pc, #imm]
                lit = ((TEXT + p - back + 4) & ~3) + (insn & 0xFF) * 4
                self.pad = _word(code, lit - TEXT) + 12
                break
        if self.pad is None:
            raise ValueError("circle pad block not found")

        # Code TwlBg never runs: an unused driver, nothing calls into it.
        d = _one(code, "00 23 49 01 F0 B5 16 01", "unused driver")
        e = _one(code, "30 B5 14 24 61 43 00 24 08 18 30 30", "function after it")
        self.room = TEXT + ((d + 3) & ~3)
        self.room_end = TEXT + e
        if not 0 < self.room_end - self.room < 0x1000:
            raise ValueError("unused driver's room looks wrong")

    def show(self):
        print("low block    %X, %X bytes, runs at 100000" % (self.low, self.low_size))
        print("frame hook   literal at %X (table %X)" % (TEXT + self.hook, self.table))
        print("circle pad   %X" % self.pad)
        print("room         %X..%X (%d bytes)" % (self.room, self.room_end, self.room_end - self.room))


def _rebuild(m, packed):
    """The TwlBg NCCH with .code replaced by PACKED, hashes made again."""
    ncch = bytearray(m.blob[m.base:m.base + m.size])
    name, off, size, index = m.file(".code")
    off -= m.base
    exefs = m.exefs_off
    # Room up to the next file (or the end of the ExeFS).
    ends = [o - m.base for n, o, s, i in m.files if o - m.base > off]
    limit = min(ends) if ends else exefs + m.exefs_size
    if off + len(packed) > limit:
        raise ValueError("packed code %X does not fit its room %X" % (len(packed), limit - off))
    ncch[off:limit] = packed + bytes(limit - off - len(packed))
    struct.pack_into("<I", ncch, exefs + index * 16 + 12, len(packed))
    struct.pack_into("32s", ncch, exefs + 0x1E0 - 0x20 * index, hashlib.sha256(packed).digest())
    region = struct.unpack_from("<I", ncch, 0x1A8)[0] * MEDIA
    if region:
        struct.pack_into("32s", ncch, 0x1C0, hashlib.sha256(bytes(ncch[exefs:exefs + region])).digest())
    return bytes(ncch)


def cmd_repack(path, out):
    """TwlBg packed again by us, nothing changed: tells our packing and file
    layout apart from our patch when a patched file does not start."""
    m = load(path)
    code = m.code()
    print("packing (about a minute)...")
    packed = lz_pack(code, m.file(".code")[2])
    if packed is None or lz_unpack(packed) != code:
        raise ValueError("packer round trip failed")
    ncch = _rebuild(m, packed)
    with open(out, "wb") as f:
        f.write(ncch)
    if find_module(ncch).code() != code:
        raise ValueError("written file does not unpack to the same code")
    print("wrote %s (code %X packed, was %X)" % (out, len(packed), m.file(".code")[2]))
    return 0


def cmd_build(path, payload_path, out):
    m = load(path)
    code = bytearray(m.code())
    places = Places(m, bytes(code))
    places.show()
    payload = bytearray(open(payload_path, "rb").read())
    if len(payload) > places.room_end - places.room:
        raise ValueError("payload %d bytes, room %d" % (len(payload), places.room_end - places.room))
    if places.room != 0x3018EC:
        raise ValueError("payload is linked at 3018EC; this TwlBg's room is at %X" % places.room)
    entry = _word(payload, 4)
    if not places.room < entry < places.room + len(payload) or not entry & 1:
        raise ValueError("payload head has no Thumb entry")
    struct.pack_into("<III", payload, 8, places.table, places.pad, STATE)
    at = places.room - TEXT
    code[at:at + len(payload)] = payload
    struct.pack_into("<I", code, places.hook, places.room)
    print("packing (about a minute)...")
    # Exactly the size of TwlBg's own packed code: the loader then finds
    # everything where it found it before.
    packed = lz_pack(bytes(code), m.file(".code")[2])
    if packed is None or lz_unpack(packed) != bytes(code):
        raise ValueError("packer round trip failed")
    ncch = _rebuild(m, packed)
    with open(out, "wb") as f:
        f.write(ncch)
    print("wrote %s (%X bytes, code %X packed)" % (out, len(ncch), len(packed)))
    # Read it back as a module: same code?
    again = find_module(ncch)
    if again.code() != bytes(code):
        raise ValueError("written file does not unpack to the patched code")
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, rest = argv[1], argv[2:]
    if cmd == "info" and len(rest) == 1:
        cmd_info(rest[0])
        return 0
    if cmd == "unpack" and len(rest) == 2:
        cmd_unpack(*rest)
        return 0
    if cmd == "check" and len(rest) == 1:
        return cmd_check(rest[0])
    if cmd == "disasm" and len(rest) in (2, 3):
        cmd_disasm(rest[0], rest[1], "--arm" in rest[2:])
        return 0
    if cmd == "places" and len(rest) == 1:
        m = load(rest[0])
        Places(m, m.code()).show()
        return 0
    if cmd == "repack" and len(rest) == 2:
        return cmd_repack(*rest)
    if cmd == "build" and len(rest) == 3:
        return cmd_build(*rest)
    if cmd == "selftest" and not rest:
        return cmd_selftest()
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
