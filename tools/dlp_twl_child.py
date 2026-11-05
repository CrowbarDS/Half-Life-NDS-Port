#!/usr/bin/env python3
"""
dlp_twl_child.py - turn a BlocksDS hybrid (DSi-enhanced) .nds into a
DS Download Play child that can run in DSi mode.

Problem: Download Play only transfers the NTR ARM9/ARM7 binaries. The
arm9i/arm7i (.twl sections) and the TWL header at 0x02FFE000 never reach
the child, so a BlocksDS child that detects DSi mode copies garbage over
itself and crashes.

What this tool does:
  * Folds arm9i into the ARM9 binary at its VMA (__arm9i_start__ = __end__).
  * Carries arm7i as a blob inside the ARM9 binary.
  * Adds an ARM9 entry stub that, only if SCFG is unlocked (SCFG_EXT9 bit 31):
      - forces libnds' DSi detection (SCFG_A9ROM & 3 == 1) by patching the
        crt0 read and every IS_DSI site, or, from BlocksDS 1.25 on, the test
        in __libnds_mpu_setup (needed when the launcher gave the child the
        NTR BIOS, e.g. nds-bootstrap + dlplay forced DSi mode);
      - builds a minimal TWL header at 0x02FFE000 (NTR header copy,
        arm9i LMA == VMA so crt0 skips the copy, arm7i LMA -> blob,
        MBK values from the original header, appflags = 0: the loader leaves
        the DSi's codec in its DS mode (nds-bootstrap's NDSTouchscreenMode),
        and with that flag libnds reads the touch screen the DSi's way, where
        nothing answers: seen on a DSi, 2026-10-30).
  * That only if 16 MB of RAM is mapped too (SCFG_EXT9 bit 15): the ARM7's
    second program then moves to high memory, out of the heap's way; the NTR
    header is found wherever the loader left it, and the game told by a
    marker word (kTwlMarker) that all this was done.
  * Adds an ARM7 entry stub that, only if SCFG is unlocked, maps NWRAM
    for the ARM7 like the original TWL header asks (MBK6/7/8 ARM7), so
    arm7i can be copied to 0x03000000.
  * If SCFG is locked (real DS, DSi in NTR mode), both stubs jump straight
    to the original entry points: the ROM behaves as a plain DS-mode build.

Usage: dlp_twl_child.py in.nds out.nds
Requires arm-none-eabi-as / ld / objcopy in PATH. Python 3.8+.
"""
import struct, subprocess, sys, tempfile, os

# Where and what the marker is (source/Global.cpp reads it): just under the
# TWL header. And where the ARM7's second program waits to be copied, high in
# the 16 MB, not at the heap's start where it is sent.
MARKER_AT = 0x02FFDFF8
MARKER = 0x434C5754
BLOB_HIGH = 0x02F00000

def u32(b, o): return struct.unpack_from('<I', b, o)[0]

def crc16(d):
    c = 0xFFFF
    for x in d:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c

def assemble(src, base):
    with tempfile.TemporaryDirectory() as t:
        s, o, e, b = (os.path.join(t, n) for n in ('s.s', 's.o', 's.elf', 's.bin'))
        open(s, 'w').write(src)
        subprocess.run(['arm-none-eabi-as', '-mcpu=arm946e-s', '-o', o, s], check=True)
        subprocess.run(['arm-none-eabi-ld', '-Ttext=0x%x' % base, '-o', e, o], check=True)
        subprocess.run(['arm-none-eabi-objcopy', '-O', 'binary', e, b], check=True)
        return open(b, 'rb').read()

def ldr_literal(code, base, addr):
    """Value loaded by an ARM 'ldr rX, [pc, #imm]' at addr."""
    w = u32(code, addr - base)
    assert (w & 0x0F7F0000) == 0x051F0000, hex(w)
    imm = w & 0xFFF
    lit = addr + 8 + (imm if (w >> 23) & 1 else -imm)
    return u32(code, lit - base)

def find_isdsi_sites(code, base):
    """Return list of (addr, new_insn) turning SCFG_A9ROM reads into 'mov rX,#1'."""
    sites = []
    # crt0: ldrb r11, [r12, r12, lsr #12] ; and r11, r11, #3
    pat = struct.pack('<II', 0xE7DCB62C, 0xE20BB003)
    i = code.find(pat)
    while i >= 0:
        sites.append((base + i, 0xE3A0B001))          # mov r11, #1
        i = code.find(pat, i + 4)
    # IS_DSI macro: ldr rX,=0x04004000 ; ldrb rX,[rX] ; and rX,rX,#3 ; cmp rX,#1
    for off in range(0, len(code) - 16, 4):
        w = u32(code, off)
        if (w & 0x0F7F0000) != 0x051F0000:
            continue
        try:
            if ldr_literal(code, base, base + off) != 0x04004000:
                continue
        except Exception:
            continue
        rx = (w >> 12) & 0xF
        ldrb = 0xE5D00000 | (rx << 16) | (rx << 12)
        andi = 0xE2000003 | (rx << 16) | (rx << 12)
        if u32(code, off + 4) == ldrb and u32(code, off + 8) == andi:
            sites.append((base + off + 4, 0xE3A00001 | (rx << 12)))  # mov rX,#1
            continue
        # BlocksDS 1.25 (__libnds_mpu_setup): ldr rA,=0x04004000 ;
        # ldr rB,[rA,#8] ; ldr rA,[rA] ; lsr rB,rB,#31 ; orr rA,rA,rB ;
        # cmp rA,#1 -- SCFG_A9ROM or SCFG_EXT's top bit, then 1 for DSi mode.
        # The orr becomes 'mov rA,#1'.
        if off + 24 > len(code):
            continue
        ra = rx
        rb = (u32(code, off + 4) >> 12) & 0xF
        if (u32(code, off + 4) == (0xE5900008 | (ra << 16) | (rb << 12)) and
                u32(code, off + 8) == (0xE5900000 | (ra << 16) | (ra << 12)) and
                u32(code, off + 12) == (0xE1A00FA0 | (rb << 12) | rb) and
                u32(code, off + 16) == (0xE1800000 | (ra << 16) | (ra << 12) | rb) and
                u32(code, off + 20) == (0xE3500001 | (ra << 16))):
            sites.append((base + off + 16, 0xE3A00001 | (ra << 12)))  # mov rA,#1
    return sites

def find_after_literal(code, base, magic):
    """crt0: ldr r1,=magic ; ldr r1,[r1] ; ldr r2,=VMA ; ... -> VMA"""
    for off in range(0, len(code) - 12, 4):
        w = u32(code, off)
        if (w & 0x0F7F0000) == 0x051F0000:
            try:
                if ldr_literal(code, base, base + off) == magic:
                    return ldr_literal(code, base, base + off + 8)
            except Exception:
                pass
    raise SystemExit('pattern for 0x%08x not found' % magic)

def main(inp, outp):
    d = open(inp, 'rb').read()
    if not d[0x12] & 2:
        raise SystemExit('input is not a DSi-enhanced/hybrid ROM (unitcode %d)' % d[0x12])

    a9off, a9entry, a9load, a9size = struct.unpack_from('<4I', d, 0x20)
    a7off, a7entry, a7load, a7size = struct.unpack_from('<4I', d, 0x30)
    a9ioff, _, a9iload, a9isize = struct.unpack_from('<4I', d, 0x1C0)
    a7ioff, _, a7iload, a7isize = struct.unpack_from('<4I', d, 0x1D0)
    arm9, arm7 = d[a9off:a9off + a9size], d[a7off:a7off + a7size]
    arm9i, arm7i = d[a9ioff:a9ioff + a9isize], d[a7ioff:a7ioff + a7isize]

    arm9i_vma = find_after_literal(arm9, a9load, 0x02FFE1C8)
    print('arm9  %08x size %x  entry %08x' % (a9load, a9size, a9entry))
    print('arm9i VMA %08x size %x' % (arm9i_vma, a9isize))
    print('arm7  %08x size %x  entry %08x' % (a7load, a7size, a7entry))
    print('arm7i size %x (LMA rewritten to a blob in EWRAM)' % a7isize)
    assert arm9i_vma >= a9load + a9size

    sites = find_isdsi_sites(arm9, a9load)
    print('SCFG_A9ROM read sites patched at runtime:', ', '.join('%08x' % a for a, _ in sites))
    if not sites:
        raise SystemExit('no SCFG_A9ROM detection site found: not a BlocksDS ARM9?')

    # ---- build new ARM9 image: arm9 | pad | arm9i@VMA | arm7i blob | stub
    img = bytearray(arm9)
    img += b'\0' * (arm9i_vma - a9load - len(img))
    img += arm9i
    img += b'\0' * (-len(img) % 32)
    blob7 = a9load + len(img)
    img += arm7i
    img += b'\0' * (-len(img) % 32)
    stub9_addr = a9load + len(img)

    mbk = struct.unpack_from('<12I', d, 0x180)  # 0x180..0x1AF
    hdr_words = [(0x1C8, arm9i_vma), (0x1CC, a9isize),
                 (0x1D8, BLOB_HIGH), (0x1DC, a7isize)]
    # MBK1-5 + ARM9/ARM7 MBK6-8 + MBK9 copied for libnds/info purposes
    for k in range(12):
        hdr_words.append((0x180 + 4 * k, mbk[k]))

    patch_tab = ''.join('    .word 0x%08x, 0x%08x\n' % s for s in sites)
    hdr_tab = ''.join('    .word 0x%03x, 0x%08x\n' % s for s in hdr_words)
    stub9 = f"""
    .arm
    .global _start
_start:
    ldr  r0, =0x04004008        @ SCFG_EXT9
    ldr  r0, [r0]
    tst  r0, #0x80000000        @ SCFG unlocked ?
    beq  boot
    tst  r0, #0x8000            @ ... and the 16 MB mapped ?
    beq  boot
    adr  r1, patchtab           @ force DSi detection in libnds
1:  ldr  r2, [r1], #4
    cmp  r2, #0
    beq  2f
    ldr  r3, [r1], #4
    str  r3, [r2]
    b    1b
2:  ldr  r0, =0x02FFE000        @ minimal TWL header
    mov  r1, #0
    mov  r2, #0x1000
3:  subs r2, r2, #4
    str  r1, [r0, r2]
    bne  3b
    ldr  r5, =0x{stub9_addr:08x}    @ the NTR header: the one naming this stub
    ldr  r1, =0x02FFFE00
    ldr  r3, [r1, #0x24]
    cmp  r3, r5
    ldrne r1, =0x027FFE00
    ldrne r3, [r1, #0x24]
    cmpne r3, r5
    ldrne r1, =0x023FFE00
    ldr  r5, =0x02FFFE00
    mov  r2, #0
4:  ldr  r3, [r1, r2]
    str  r3, [r0, r2]           @ ... into the TWL header
    str  r3, [r5, r2]           @ ... and where libnds reads it
    add  r2, r2, #4
    cmp  r2, #0x160
    blt  4b
    ldr  r1, =0x{blob7:08x}     @ arm7i out of the heap's way
    ldr  r5, =0x{BLOB_HIGH:08x}
    ldr  r2, =0x{a7isize:08x}
6:  subs r2, r2, #4
    ldrpl r3, [r1, r2]
    strpl r3, [r5, r2]
    bpl  6b
    ldr  r1, =0x{MARKER_AT:08x}  @ the game is told
    ldr  r3, =0x{MARKER:08x}
    str  r3, [r1]
    adr  r1, hdrtab
5:  ldr  r2, [r1], #4
    cmn  r2, #1
    beq  boot
    ldr  r3, [r1], #4
    str  r3, [r0, r2]
    b    5b
boot:
    ldr  pc, =0x{a9entry:08x}
    .pool
patchtab:
{patch_tab}    .word 0
hdrtab:
{hdr_tab}    .word 0xFFFFFFFF
"""
    img += assemble(stub9, stub9_addr)
    if stub9_addr >= 0x02280000:
        raise SystemExit('ARM9 too big for dlplay (entry must be < 0x02280000)')

    # ---- ARM7: original + stub mapping NWRAM like the TWL header asks
    img7 = bytearray(arm7)
    img7 += b'\0' * (-len(img7) % 32)
    stub7_addr = a7load + len(img7)
    mbk6_7, mbk7_7, mbk8_7 = u32(d, 0x1A0), u32(d, 0x1A4), u32(d, 0x1A8)
    stub7 = f"""
    .arm
    .global _start
_start:
    ldr  r0, =0x04004008        @ SCFG_EXT7
    ldr  r0, [r0]
    tst  r0, #0x80000000
    beq  boot
    tst  r0, #0x8000
    beq  boot
    ldr  r0, =0x04004054        @ MBK6 (ARM7)
    ldr  r1, =0x{mbk6_7:08x}
    str  r1, [r0]
    ldr  r1, =0x{mbk7_7:08x}
    str  r1, [r0, #4]
    ldr  r1, =0x{mbk8_7:08x}
    str  r1, [r0, #8]
boot:
    ldr  pc, =0x{a7entry:08x}
    .pool
"""
    img7 += assemble(stub7, stub7_addr)

    # ---- NTR header
    h = bytearray(d[:0x200])
    h[0x12] = 0                       # unitcode: NTR
    h[0x1C] = 0                       # clear TWL flags
    h[0x1D] = 0
    h[0x1F] = 0
    h[0x160:0x200] = b'\0' * 0xA0
    A9OFF = 0x4000
    A7OFF = (A9OFF + len(img) + 0x1FF) & ~0x1FF
    BNOFF = (A7OFF + len(img7) + 0x1FF) & ~0x1FF
    struct.pack_into('<4I', h, 0x20, A9OFF, stub9_addr, a9load, len(img))
    struct.pack_into('<4I', h, 0x30, A7OFF, stub7_addr, a7load, len(img7))
    struct.pack_into('<4I', h, 0x40, 0, 0, 0, 0)      # no FNT/FAT
    struct.pack_into('<4I', h, 0x50, 0, 0, 0, 0)      # no overlays
    bn_src = u32(d, 0x68)
    banner = bytearray(d[bn_src:bn_src + 0x840]) if bn_src else bytearray()
    if banner:
        banner[0:2] = b'\x01\x00'                       # banner v1 (CRC of v1 area unchanged)
        struct.pack_into('<I', h, 0x68, BNOFF)
    else:
        struct.pack_into('<I', h, 0x68, 0)
    total = BNOFF + len(banner)
    struct.pack_into('<I', h, 0x80, total)
    struct.pack_into('<I', h, 0x84, 0x4000)
    struct.pack_into('<H', h, 0x15E, crc16(h[:0x15E]))

    rom = bytearray(total)
    rom[:0x200] = h
    rom[A9OFF:A9OFF + len(img)] = img
    rom[A7OFF:A7OFF + len(img7)] = img7
    rom[BNOFF:BNOFF + len(banner)] = banner
    open(outp, 'wb').write(rom)
    print('written %s (%d bytes)  ARM9 entry stub %08x  ARM7 entry stub %08x'
          % (outp, total, stub9_addr, stub7_addr))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(1)
    main(sys.argv[1], sys.argv[2])
