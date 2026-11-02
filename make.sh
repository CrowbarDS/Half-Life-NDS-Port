#!/usr/bin/env bash
# Build the NDS ROM by running build.py inside the project's Python venv.
# Forwards any extra arguments to build.py (e.g. ./make.sh --ninja, --graph).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# (HLDS_VENV: a venv somewhere else -- the build container has its own.)
VENV_DIR="${HLDS_VENV:-${SCRIPT_DIR}/env}"

if [ ! -f "${VENV_DIR}/bin/activate" ]; then
    echo "ERROR: Python venv not found at ${VENV_DIR}." >&2
    echo "Run ./setup-env.sh first." >&2
    exit 1
fi

# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"

# BlocksDS lives under the Wonderful toolchain. Export the toolchain env here
# (with fallbacks) so the build works even from shells that never sourced
# ~/.bashrc -- e.g. IDE terminals / task runners. Without BLOCKSDS, architectds
# silently defaults to /opt/blocksds/core and the ARM7 core ELF
# (arm7_dswifi_maxmod.elf) can't be found. ${VAR:-default} keeps any value an
# already-configured shell provides.
export WONDERFUL_TOOLCHAIN="${WONDERFUL_TOOLCHAIN:-/opt/wonderful}"
export BLOCKSDS="${BLOCKSDS:-${WONDERFUL_TOOLCHAIN}/thirdparty/blocksds/core}"
export BLOCKSDSEXT="${BLOCKSDSEXT:-${WONDERFUL_TOOLCHAIN}/thirdparty/blocksds/external}"
export PATH="${WONDERFUL_TOOLCHAIN}/bin:${PATH}"

cd "${SCRIPT_DIR}"

# The DSP program (DSi audio), if the Teak toolchain is there to build it:
# made into a C array the ARM9 links (source/audio/DspProgram.gen.cpp). With
# no toolchain the array is empty and the game does without the DSP.
python tools/dspsim/embed.py
# The build's name (source/BuildId.gen.cpp), shown on the crash screen.
BUILD_ID="$(python tools/buildid.py)"

python build.py "$@"
# The Download Play child (child/build.py): the same code built again for a
# console that has no card. After the parent, whose ARM7 program it uses.
(cd child && python build.py "$@")
python tools/child_size.py
# ... made into what can be sent: DSi mode where the console allows it, a plain
# DS program where it does not (tools/dlp_twl_child.py).
PATH="${WONDERFUL_TOOLCHAIN}/toolchain/gcc-arm-none-eabi/bin:${PATH}" python tools/dlp_twl_child.py child/out/hl-child.nds child/out/hl-child-twl.nds
# ... and the parent again, now carrying the child to send (dlplay/child.nds).
python build.py "$@"
# The ELFs of every build, by its name, for looking up a crash screen's
# addresses afterwards (arm-none-eabi-addr2line -e build/elf/<name>/arm9.elf);
# the last twelve kept.
if [ -f build/arm9/arm9.elf ] && [ -f build/hl/hl.elf ]; then
    mkdir -p "build/elf/${BUILD_ID}"
    cp build/arm9/arm9.elf build/hl/hl.elf "build/elf/${BUILD_ID}/"
    ls -1t build/elf | tail -n +13 | while read -r old; do
        rm -rf "build/elf/${old:?}"
    done
fi
# What the fast memories hold and who still calls the library's arithmetic,
# in a line (tools/arm9-report.sh for all of it).
if [ -f build/arm9/arm9.elf ]; then
    tools/arm9-report.sh --summary || true
fi
# No interrupt handler may reach the divider or the square-root unit (AGENTS.md).
python tools/irq-divider.py build/arm9/arm9.elf child/build/arm9/arm9.elf
