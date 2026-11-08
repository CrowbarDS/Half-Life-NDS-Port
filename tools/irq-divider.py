#!/usr/bin/env python3
"""Whether an interrupt handler can reach the divider or the square-root unit.

The unit holds one sum at a time. Our routines (source/HwMath.s) hold interrupts
off while they use it, but libnds's and the engine's inline divides do not: a
handler that divided in the middle of one would leave its own answer there. So
no handler may use it. This walks the calls in a linked ELF from every handler:

    tools/irq-divider.py build/arm9/arm9.elf child/build/arm9/arm9.elf

Exit status 1 if one can, with the way there, or if something new installs a
handler. What a crash prints is allowed: it never returns to whatever was
interrupted.

What it can see: direct calls, the linker's Thumb veneers, and the registers
reached by a literal or a base and an offset. Not where a call through a pointer
goes: --calls lists those in what the handlers reach, for someone to read, and a
new handler goes in HANDLERS by hand. The wireless packet handlers are found by
name for that reason.

    tools/irq-divider.py --uses <elf>    every function that touches the units
    tools/irq-divider.py --calls <elf>   the calls through a pointer in handlers
"""
import bisect
import collections
import re
import subprocess
import sys

TOOL = "/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-"

# Given to irqSet, or called from libnds's dispatcher and FIFO interrupts.
HANDLERS = [
    "(anonymous namespace)::onVblank()",  # source/Global.cpp
    "NEA_HBLFunc",
    "ne_gxfifo_irq_handler",
    "fifoInternalRecvInterrupt",
    "fifoInternalSendInterrupt",
    "IPCSync",
    "systemValueHandler",
    "systemMsgHandler",
    "wifiValue32Handler",  # (without lwIP, Wifi_Update runs in the interrupt)
    "Wifi_DlPlayFromClientHandler",
]
# (What the radio's interrupt calls by a pointer, each of these: fromHost's sink
# is the child's sinkTake, which takes a map's pieces as they arrive.)
HANDLER_PATTERN = re.compile(r"netlink::.*(fromClient|fromHost)\(|guest::.*sinkTake\(")
# Handlers that only some programs have (the child sends nothing).
OPTIONAL = {"Wifi_DlPlayFromClientHandler"}
# Who may install one. Anyone new may be installing a handler this cannot see:
# it fails until that handler is in HANDLERS and its caller here.
INSTALLERS = {
    "irqSet": {"Global::Init(char const*)", "fifoInit", "ne_gxfifo_wait_room"},
    "irqSetAUX": set(),
    "timerStart": set(),
    "fifoSetValue32Handler": {"Wifi_Deinit", "Wifi_InitDefault", "initSystem"},
    "fifoSetDatamsgHandler": {"initSystem"},
    "fifoSetAddressHandler": set(),
    "Wifi_RawSetPacketHandler": set(),
    "Wifi_MultiplayerFromClientSetPacketHandler": {
        "Wifi_DlPlayStart", "Wifi_DlPlayStop", "hlrt::netlink::host(char const*, int)", "hlrt::netlink::shutdown()"},
    "Wifi_MultiplayerFromHostSetPacketHandler": {
        "hlrt::netlink::join(int)", "hlrt::netlink::joinDirect", "hlrt::netlink::scan()", "hlrt::netlink::shutdown()"},
    "hlrt::netlink::setSink(bool (*)(unsigned char const*, int))": {
        "hlrt::guest::(anonymous namespace)::demoWant", "hlrt::guest::(anonymous namespace)::ringOn",
        "hlrt::guest::(anonymous namespace)::demoGetTrack"},
}
UNITS = ("__wrap___aeabi_ldivmod", "__wrap___aeabi_uldivmod", "hlHwSqrt64")
CRASH = "__assert_func"
REGISTERS = range(0x04000280, 0x040002C0)
BRANCH = re.compile(r"^(bl|blx|b)(eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?(\.n|\.w)?$")
LINE = re.compile(r"\s*([0-9a-f]+):\s+([0-9a-f]+)(?: [0-9a-f]+)?\s+(\S+)\s*(.*)")
R = r"(r\d+|ip|lr|sl|fp)"
PC_LOAD = re.compile(R + r",\s*\[pc,.*@\s*\(?([0-9a-f]+)")
MOVE = re.compile(R + r",\s*#(-?\d+)")
ADD = re.compile(R + r",\s*" + R + r",\s*#(\d+)")
ADD_TO = re.compile(R + r",\s*#(\d+)")
OFFSET = re.compile(r"\[" + R + r",\s*#(-?\d+)")
BLOCK = re.compile(R + r"!?,")
REGISTER = re.compile(R + r"\b")


def run(*command):
    return subprocess.run(command, capture_output=True, text=True, check=True).stdout


def check(elf):
    symbols = set()
    for line in run(TOOL + "nm", "-n", "--defined-only", "-C", elf).splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[1] in "tTwW" and not parts[2].startswith("$"):
            symbols.add((int(parts[0], 16) & ~1, parts[2]))
    symbols = sorted(symbols)
    starts = [at for at, _ in symbols]
    names = {name for _, name in symbols}
    by_address = {at: name for at, name in symbols}

    def owner(at):
        i = bisect.bisect_right(starts, at & ~1) - 1
        return symbols[i][1] if i >= 0 else None

    # Words first: the literals an instruction loads are after it.
    listing = []
    words = {}
    for line in run(TOOL + "objdump", "-d", "-z", elf).splitlines():
        match = LINE.match(line)
        if match is None:
            continue
        at = int(match[1], 16)
        listing.append((at, owner(at), match[3], match[4]))
        if len(match[2]) == 8:
            words[at] = int(match[2], 16)

    calls = collections.defaultdict(set)
    uses = set()
    evidence = {}  # the first instruction that says so
    indirect = collections.defaultdict(set)
    known = {}  # what each register is known to hold, within a function
    last = None
    for at, where, op, rest in listing:
        if where != last:
            known = {}
            last = where
        if BRANCH.match(op):
            target = re.match(r"([0-9a-f]+)", rest)
            if target:
                to = owner(int(target[1], 16))
                if to != where:
                    calls[where].add(to)
            elif not rest.startswith("lr"):
                indirect[where].add(op + " " + rest)
            continue
        if (op == "bx" and rest.split()[0] != "lr") or (op.startswith("mov") and rest.startswith("pc,")):
            indirect[where].add(op + " " + rest)
        if op == ".word":
            # (A veneer goes on by a literal.)
            if "_from_thumb" in (where or "") and (words.get(at, 0) & ~1) in by_address:
                calls[where].add(by_address[words[at] & ~1])
            continue
        loaded = PC_LOAD.match(rest) if op.startswith("ldr") else None
        moved = MOVE.match(rest) if op.startswith("mov") else None
        added = ADD.match(rest) if op.startswith("add") else None
        added_to = ADD_TO.match(rest) if op.startswith("add") else None
        memory = OFFSET.search(rest) if op[:3] in ("ldr", "str") else None
        block = BLOCK.match(rest) if op[:3] in ("ldm", "stm") else None
        value = None
        written = None
        if loaded:
            written, value = loaded[1], words.get(int(loaded[2], 16))
        elif moved:
            written, value = moved[1], int(moved[2], 0)
        elif added and added[2] in known:
            written, value = added[1], known[added[2]] + int(added[3], 0)
        elif added_to and added_to[1] in known:
            written, value = added_to[1], known[added_to[1]] + int(added_to[2], 0)
        elif memory and memory[1] in known and known[memory[1]] + int(memory[2], 0) in REGISTERS:
            uses.add(where)
            evidence.setdefault(where, at)
        elif block and block[1] in known and known[block[1]] in REGISTERS:
            uses.add(where)
            evidence.setdefault(where, at)
        if value is not None and value in REGISTERS:
            uses.add(where)
            evidence.setdefault(where, at)
        # Anything else that writes a register: what it held is gone.
        if written is None and not op.startswith(("str", "cmp", "cmn", "tst", "teq", "push", "stm")):
            first = REGISTER.match(rest)
            if first:
                written = first[1]
        if written is not None:
            known.pop(written, None)
            if value is not None:
                known[written] = value

    roots = [name for name in HANDLERS if name in names]
    missing = [name for name in HANDLERS if name not in names]
    roots += [name for name in names if HANDLER_PATTERN.search(name)]
    came_from = {}
    stack = [(root, None) for root in roots]
    while stack:
        name, parent = stack.pop()
        if name in came_from or name == CRASH:
            continue
        came_from[name] = parent
        stack += [(callee, name) for callee in calls.get(name, ())]

    if "--uses" in FLAGS:
        for name in sorted(uses):
            print("  uses: %08x %s%s" % (evidence[name], name, "  (reached by a handler)" if name in came_from else ""))
    if "--calls" in FLAGS:
        for name in sorted(came_from):
            for call in sorted(indirect.get(name, ())):
                print("  through a pointer: %s: %s" % (name, call))
    bad = 0
    for name in missing:
        if name not in OPTIONAL:
            print("%s: the handler %s is not there: has it a new name?" % (elf, name))
            bad += 1
    callers = collections.defaultdict(set)
    for caller, callees in calls.items():
        for callee in callees:
            callers[callee].add(caller)

    def who_calls(name):
        found = set()
        for caller in callers.get(name, ()):
            found |= who_calls(caller) if "_from_thumb" in caller else {caller}
        return found

    for installer, allowed in INSTALLERS.items():
        for caller in sorted(who_calls(installer)):
            if not any(caller.startswith(known) for known in allowed):
                print("%s: %s calls %s: a new handler? Add it here" % (elf, caller, installer))
                bad += 1
    for name in sorted(came_from):
        if name not in uses and name not in UNITS:
            continue
        path = [name]
        while came_from[path[-1]] is not None:
            path.append(came_from[path[-1]])
        print("%s: %s" % (elf, " <- ".join(path)))
        bad += 1
    print("%s: %d handlers, %d functions they reach, %d problems%s" % (
        elf, len(roots), len(came_from), bad,
        "; not in this program: " + ", ".join(missing) if missing else ""))
    return bad == 0


FLAGS = {arg for arg in sys.argv[1:] if arg.startswith("--")}


def main():
    good = True
    for elf in (arg for arg in sys.argv[1:] if not arg.startswith("--")):
        good = check(elf) and good
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
