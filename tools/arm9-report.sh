#!/usr/bin/env bash
# What the built ARM9 program is made of, for whoever is making it faster:
#
#   - what is in the fast memories (ITCM 32 KB of code, DTCM 16 KB of data
#     and stack), symbol by symbol;
#   - which of the game's functions call the compiler's arithmetic helpers
#     (a divide, a 64-bit multiply done by a routine instead of an
#     instruction), and which of those helpers are the hardware's (HwMath.s);
#   - which of the game's functions are Thumb.
#
# And the game's families, build/<family>/<family>.elf (dsl/<family>.dsl in
# the ROM: see source/game/FamilyApi.hpp), the same way, with what a family
# must not have: Thumb code (an ARM-to-Thumb jump in it goes through a linker
# stub the loader cannot move), and a copy of anything the program has --
# a function it should have called, or worse, data it should have shared.
# And the program must have none of the families' code.
#
# And the Download Play child (child/build/arm9/arm9.elf), which is the engine
# and the game linked into one program: the same, but the game is meant to be
# in it there.
#
# usage: tools/arm9-report.sh [--check] [--summary] [arm9.elf]
#   --check    fail if a function of the hot list below calls a helper it
#              should not, the ITCM is over its budget, the library breaks
#              one of its rules, or the program has grown past what
#              kDsMapHeap's margin allows (shared/HlDsRoom.hpp)
#   --summary  the totals only (make.sh prints this)
#
# It reads the ELF and changes nothing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${WONDERFUL_TOOLCHAIN:-/opt/wonderful}/toolchain/gcc-arm-none-eabi/bin"
MODE=full
ELF="$ROOT/build/arm9/arm9.elf"
for arg in "$@"; do
    case "$arg" in
        --check) MODE=check ;;
        --summary) MODE=summary ;;
        *) ELF="$arg" ;;
    esac
done
FAMILIES="boss military net"
[ -f "$ELF" ] || { echo "arm9-report: no $ELF (build first)" >&2; exit 2; }

# One ELF: the program (KIND=main), the child (KIND=child) or a family
# (KIND=dsl). (-r: in a family a branch to the program is a relocation, not
# an address.)
report() {
"$BIN/arm-none-eabi-objdump" -dr -C "$3" | MODE="$1" KIND="$2" ROOT="$ROOT" python3 -c '
import os, re, subprocess, sys

mode = os.environ["MODE"]
kind_of_elf = os.environ["KIND"]
elf, tools = sys.argv[1], sys.argv[2]

# Code the ITCM may hold before the build is told to think again. (The link
# fails by itself at 32768.)
ITCM_BUDGET = 32384  # (2026-11-18: the instruction cache profiler, round 1; 384 B kept for libnds to grow)

# The helpers. "soft" ones are the library routines; the others are ours,
# on the divider and the square-root unit (source/HwMath.s).
HELPERS = ["__wrap___aeabi_ldivmod", "__wrap___aeabi_uldivmod", "__aeabi_ldivmod", "__aeabi_uldivmod", "hlHwSqrt64", "__aeabi_idiv", "__aeabi_uidiv",
           "__aeabi_idivmod", "__aeabi_uidivmod", "__divsi3", "__udivsi3", "__aeabi_lmul", "__muldi3",
           "__aeabi_llsl", "__aeabi_llsr", "__aeabi_lasr", "__udivmoddi4", "__divmoddi4",
           "__clzsi2", "__clzdi2", "__aeabi_f2d", "__aeabi_d2f", "__aeabi_dmul", "__aeabi_fmul"]
SHORT = {"__wrap___aeabi_ldivmod": "div64", "__wrap___aeabi_uldivmod": "udiv64",
         "__aeabi_ldivmod": "soft-div64", "__aeabi_uldivmod": "soft-udiv64", "hlHwSqrt64": "sqrt",
         "__aeabi_idiv": "div", "__divsi3": "div", "__aeabi_uidiv": "udiv", "__udivsi3": "udiv",
         "__aeabi_idivmod": "divmod", "__aeabi_uidivmod": "udivmod", "__aeabi_lmul": "lmul",
         "__muldi3": "lmul"}

# The hot list: functions run many times a frame, and what none of them may
# call. (A 64-bit multiply by a routine means the function is Thumb; a
# 64-by-64 divide means a divide that was not looked at.)
ALL = ["lmul", "div64", "udiv64", "div", "udiv", "divmod", "udivmod"]
SOFT = ["lmul", "div", "udiv", "divmod", "udivmod"]
HOT = {
    # (The trace divides once per wall crossed, on the hardware: that one is meant.)
    "hldata::detail::hullCheck": SOFT,
    "hldata::traceHull": SOFT,
    "hldata::traceAll": SOFT,
    "hldata::poseModel": SOFT,
    "hlrt::WorldDraw::walk": ALL,
    "hlrt::WorldDraw::emit": ALL,
    "hlrt::WorldDraw::emitList": ALL,
    "hlrt::EffectDraw::gather": ["lmul"],
    "hlrt::EffectDraw::drawRecord": ["lmul"],
    "hlrt::EffectDraw::drawDecals": ["lmul"],
    "hlrt::HudDraw::draw": SOFT,
    # The minds of the monsters (header-only: ARM as long as the ARM objects come
    # first on the link line -- architectds.py). Each of these runs for every
    # monster every think or frame.
    "hldata::aiTick": ["lmul"],
    "hldata::aiMonsterThink": ["lmul"],
    "hldata::aiLook": ["lmul"],
    "hldata::aiTargetOf": ["lmul"],
    "hldata::aiVisible": ["lmul"],
    "hldata::aiMoveTick": ["lmul"],
    "hldata::aiAnimEvents": ["lmul"],
    "hldata::lineClear": ["lmul"],
    "hldata::monstersTick": ["lmul"],
    "hldata::logicTick": ["lmul"],
}
# What has to be in the ITCM. (The game'"'"'s core is linked into the program:
# its shot traces with the rest.)
HOT_ITCM = ["hldata::detail::hullCheck", "hldata::traceHull", "hldata::traceAll", "hldata::poseModel",
            "hldata::detail::sampleTrack", "hldata::walkStep", "hldata::walkTick",
            "hlrt::ModelDraw::drawActor", "hlrt::ModelDraw::draw", "hlrt::WorldDraw::walk",
            "hldata::traceShot", "hldata::pointContents"]

def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout

# Sections.
sizes = {}
for line in run(tools + "/arm-none-eabi-size", "-A", elf).splitlines():
    part = line.split()
    if len(part) == 3 and part[0].startswith("."):
        sizes[part[0]] = (int(part[1]), int(part[2]))

# Symbols: address, size, name; Thumb ones have the low bit set. (A family
# is linked against the program: the symbols of the program are in its ELF
# too, at the addresses of the program. Its own are from 0, below them.)
PROGRAM_LOW = 0x01000000
all_symbols = []
for line in run(tools + "/arm-none-eabi-readelf", "-sW", elf).splitlines():
    part = line.split()
    if len(part) >= 8 and part[3] in ("FUNC", "OBJECT") and part[6] != "UND":
        try:
            all_symbols.append((int(part[1], 16), int(part[2], 0), part[3], part[7]))
        except ValueError:
            pass
symbols = [s for s in all_symbols if kind_of_elf != "dsl" or s[0] < PROGRAM_LOW]
programs = [s for s in all_symbols if kind_of_elf == "dsl" and s[0] >= PROGRAM_LOW]
# The marks the linker sets (no type, no size).
marks = {}
for line in run(tools + "/arm-none-eabi-readelf", "-sW", elf).splitlines():
    part = line.split()
    if len(part) >= 8 and part[3] == "NOTYPE" and part[7] in (
            "__dtcm_start", "__sp_usr", "__sbss_end", "__dtcm_data_top", "__itcm_start", "__itcm_end", "__end__"):
        marks[part[7]] = int(part[1], 16)
demangle = dict(zip([s[3] for s in symbols],
                    subprocess.run([tools + "/arm-none-eabi-c++filt"], input="\n".join(s[3] for s in symbols),
                                   capture_output=True, text=True).stdout.splitlines()))
address = {}
for at, size, kind, name in all_symbols:
    if name in HELPERS:
        address[at & ~1] = name

def game(name):
    return "hlrt::" in name or "hldata::" in name

def bare(name):
    name = re.sub(r"\[clone[^\]]*\]", "", name).strip()
    name = re.sub(r"\(.*$", "", name)
    return name

# The disassembly: per function, the helpers it calls.
calls = {}      # function -> {helper: count}
where = {}      # function -> address
current = None
head = re.compile(r"^([0-9a-f]{8}) <(.*)>:$")
branch = re.compile(r"\tblx?\t[0-9a-f]+ <([^>+]+)>")
relocated = re.compile(r": R_ARM_(?:CALL|JUMP24)\t(\S+)")
word = re.compile(r"\t\.word\t0x([0-9a-f]{8})")
for line in sys.stdin:
    line = line.rstrip("\n")
    m = head.match(line)
    if m:
        current = m.group(2)
        where.setdefault(current, int(m.group(1), 16))
        continue
    if current is None:
        continue
    m = branch.search(line) or relocated.search(line)
    helper = None
    if m and m.group(1) in HELPERS:
        helper = m.group(1)
    else:
        m = word.search(line)
        if m:
            helper = address.get(int(m.group(1), 16) & ~1)
    if helper:
        calls.setdefault(current, {})
        calls[current][helper] = calls[current].get(helper, 0) + 1

itcm_at, itcm_size = sizes.get(".itcm", (0, 0))[1], sizes.get(".itcm", (0, 0))[0]
dtcm_size = sizes.get(".dtcm", (0, 0))[0] + sizes.get(".sbss", (0, 0))[0]
in_itcm = [(size, demangle.get(name, name)) for at, size, kind, name in symbols
           if itcm_at <= (at & ~1) < itcm_at + 32768 and size]
if kind_of_elf == "dsl":
    in_itcm = []  # (the library is linked at 0: nothing of it is in ITCM)
dtcm_at = sizes.get(".dtcm", (0, 0))[1]
in_dtcm = [(size, demangle.get(name, name)) for at, size, kind, name in symbols
           if kind == "OBJECT" and 0x02ff4000 <= at < 0x02ff8000 and size]
functions = [(at, size, demangle.get(name, name)) for at, size, kind, name in symbols if kind == "FUNC"]
thumb_game = sorted({(size, name) for at, size, name in functions if (at & 1) and game(name)}, reverse=True)
arm_game = {name for at, size, name in functions if not (at & 1) and game(name)}

soft = hard = 0
for name, by in calls.items():
    if not game(name):
        continue
    for helper, count in by.items():
        if helper in ("__wrap___aeabi_ldivmod", "__wrap___aeabi_uldivmod", "hlHwSqrt64"):
            hard += count
        else:
            soft += count

summary = ("%s.dsl: %d bytes; the game: %d ARM functions, %d Thumb; helper calls: %d on the hardware, %d in the library"
           % (os.path.splitext(os.path.basename(elf))[0], sizes.get(".progbits", (0, 0))[0] + sizes.get(".nobits", (0, 0))[0], len(arm_game), len(thumb_game),
              hard, soft)) if kind_of_elf == "dsl" else \
          ("%s: ITCM %d of 32768 bytes (heap %d), stack %d, DTCM data %d (heap %d); "
           "the game: %d ARM functions, %d Thumb; helper calls: %d on the hardware, %d in the library"
           % ("child" if kind_of_elf == "child" else "arm9", itcm_size,
              marks.get("__itcm_start", 0) + 32768 - marks.get("__itcm_end", 0),
              marks.get("__sp_usr", 0) - marks.get("__dtcm_start", 0), dtcm_size,
              marks.get("__dtcm_data_top", 0) - marks.get("__sbss_end", 0),
              len(arm_game), len(thumb_game), hard, soft))

failed = []
# The program has grown since kDsMapHeap was measured (shared/HlDsRoom.hpp):
# each byte of it a byte less of a map'"'"'s heap, out of the margin kept
# there. (Code and static data only: room.sh measures the rest.)
if kind_of_elf == "main" and "__end__" in marks:
    room = open(os.path.join(os.environ["ROOT"], "shared", "HlDsRoom.hpp")).read()
    heap = re.search(r"kDsMapHeap = (\d+) - (\d+) \* 1024;", room)
    end = re.search(r"kDsMapHeapProgramEnd = (0x[0-9a-fA-F]+);", room)
    if heap and end:
        margin = int(heap.group(2)) * 1024 - (marks["__end__"] - int(end.group(1), 16))
        summary += "; map heap margin %d KB" % (margin // 1024)
        if margin < 16 * 1024:
            failed.append("the program has grown %d bytes since kDsMapHeap was measured: %d KB of its margin left, "
                          "under 16 (tools/emu/room.sh ds, then shared/HlDsRoom.hpp)"
                          % (marks["__end__"] - int(end.group(1), 16), margin // 1024))
for name, banned in HOT.items():
    found = False
    for function, by in list(calls.items()) + [(f, {}) for f in where if f not in calls]:
        if bare(function) != name:
            continue
        found = True
        for helper, count in by.items():
            if SHORT.get(helper, helper) in banned:
                failed.append("%s calls %s (%d)" % (function, helper, count))
    if not found and mode == "check":
        pass  # inlined away, or not in this build: nothing to hold it to
for name in HOT_ITCM if kind_of_elf in ("main", "child") else []:
    for function, at in where.items():
        if bare(function) == name and not (itcm_at <= at < itcm_at + 32768):
            failed.append("%s is not in the ITCM (it is at %08x)" % (function, at))
# ... and what has to be ARM (the minds of the monsters, above).
HOT_ARM = [name for name, banned in HOT.items() if name.startswith("hldata::ai") or name in
           ("hldata::lineClear", "hldata::monstersTick", "hldata::logicTick")]
for name in HOT_ARM:
    for at, size, function in functions:
        if bare(function) == name and (at & 1):
            failed.append("%s is Thumb" % function)
if kind_of_elf == "dsl":
    # The library: ARM throughout, and no linker stub (see the top).
    for size, name in thumb_game:
        failed.append("%s is Thumb, in the library" % name)
    for at, size, kind, name in symbols:
        if name.endswith("_from_arm") or name.endswith("_from_thumb"):
            failed.append("a linker stub in the library: %s" % name)
    # A copy of what the program has: data of its own where the program'"'"'s
    # is meant (two of one thing, each half of the game seeing its own), or
    # a function the link should have taken from the program. (A clone the
    # compiler made of one -- .isra, .constprop, .part -- is local to each
    # side and holds nothing: it only costs its bytes.)
    theirs = {name for at, size, kind, name in programs}
    for at, size, kind, name in symbols:
        if name in theirs and "." not in name:
            failed.append("the family has its own %s %s, which the program has too"
                          % ("data" if kind == "OBJECT" else "copy of", demangle.get(name, name)))
    # What libnds'"'"'s loader (dlfcn.c) does not do, and would get wrong
    # without a word: put a section on more than eight bytes (the block is
    # malloc'"'"'s), apply more than the first relocation section. (A word
    # pointing into the program keeps what the link wrote:
    # tools/dsl_family.py.)
    rel_sections = 0
    for line in run(tools + "/arm-none-eabi-readelf", "-SW", elf).splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\S+\s+(\S*)\s+\d+\s+\d+\s+(\d+)", line)
        if not m:
            continue
        name, kind, flags, align = m.group(1), m.group(2), m.group(6), int(m.group(7))
        if "A" in flags and align > 8:
            failed.append("family section %s wants %d-byte alignment (the loader gives 8)" % (name, align))
        if kind == "REL":
            rel_sections += 1
    if rel_sections > 1:
        failed.append("the family has %d relocation sections (the loader applies the first)" % rel_sections)
else:
    # The guarded heap is linked in (build.py: --wrap).
    if kind_of_elf in ("main", "child"):
        # None of the families'"'"' code: they are loaded with the map that
        # needs them (HL_FAMILIES_APART, HL_NET_APART). (The child has the
        # match'"'"'s rules linked in: every guest needs them.)
        family_code = re.compile(r"hldata::(garg|babycrab|icky|tentacle|momma|nihilanth|grunt|assassin|osprey|"
                                 r"apache|boss|military)::|hldata::detail::(stompTick|mommaSpitTick|repelTick)\(" +
                                 (r"|hldata::net(Tick|Start|Read|Lobby|Used)\(|hldata::detail::netApply\("
                                  if kind_of_elf == "main" else ""))
        for at, size, kind, name in symbols:
            if family_code.search(demangle.get(name, name)):
                failed.append("the program has a family'"'"'s %s" % demangle.get(name, name))
    if not any(name == "__wrap_malloc" for at, size, kind, name in symbols):
        failed.append("the program has no guarded heap (__wrap_malloc): see --wrap in build.py")
if itcm_size > ITCM_BUDGET:
    failed.append("the ITCM holds %d bytes, over its budget of %d" % (itcm_size, ITCM_BUDGET))

if mode == "summary":
    print(summary)
    for line in failed:
        print("arm9: HOT LIST: " + line)
    sys.exit(0)
if mode == "check":
    print(summary)
    for line in failed:
        print("arm9-report: " + line, file=sys.stderr)
    sys.exit(1 if failed else 0)

print(summary)
print()
print("ITCM, the largest (bytes):")
for size, name in sorted(in_itcm, reverse=True)[:40]:
    print("  %6d  %s" % (size, name))
print()
print("DTCM data:")
for size, name in sorted(in_dtcm, reverse=True)[:40]:
    print("  %6d  %s" % (size, name))
if not in_dtcm:
    print("  (none)")
print()
print("The game'"'"'s functions that call a helper (the most first):")
rows = []
for name, by in calls.items():
    if game(name):
        mode_of = "ITCM " if itcm_at <= where[name] < itcm_at + 32768 else "     "
        rows.append((sum(by.values()), mode_of, name, by))
thumb_names = {name for size, name in thumb_game}
for total, place, name, by in sorted(rows, key=lambda r: -r[0]):
    what = ", ".join("%s %d" % (SHORT.get(h, h), c) for h, c in sorted(by.items(), key=lambda x: -x[1]))
    print("  %3d %s%s %s: %s" % (total, place, "T" if name in thumb_names else "A", name[:90], what))
print()
print("The game'"'"'s Thumb functions, the largest (bytes):")
for size, name in thumb_game[:60]:
    print("  %6d  %s" % (size, name[:100]))
if failed:
    print()
    for line in failed:
        print("HOT LIST: " + line)
' "$3" "$BIN"
}

# (The child is told by where it was built.)
KIND=main
case "$(realpath "$ELF")" in */child/build/*) KIND=child ;; esac

status=0
report "$MODE" "$KIND" "$ELF" || status=$?
[ "$KIND" = child ] && FAMILIES="boss military"
if [ "$KIND" = main ] || [ "$KIND" = child ]; then
    for family in $FAMILIES; do
        DSL="$(dirname "$ELF")/../$family/$family.elf"
        [ -f "$DSL" ] || continue
        [ "$MODE" = full ] && echo
        report "$MODE" dsl "$DSL" || status=$?
    done
fi
exit $status
