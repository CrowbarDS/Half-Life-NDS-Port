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
# The BlocksDS the game is built with is pinned (docker/versions.env,
# tools/blocksds_pin.py): the first of these that is that version. A newer
# libnds elsewhere -- BLOCKSDS from ~/.bashrc -- is passed over, not used.
BLOCKSDS_CANDIDATES=("${HLDS_BLOCKSDS:-}" "${BLOCKSDS:-}"
                     "${WONDERFUL_TOOLCHAIN}/thirdparty/blocksds/core" /opt/blocksds/core)
if pinned="$(python "${SCRIPT_DIR}/tools/blocksds_pin.py" --pick "${BLOCKSDS_CANDIDATES[@]}")"; then
    export BLOCKSDS="${pinned}"
elif [ -z "${HLDS_BLOCKSDS_ANY:-}" ]; then
    echo "ERROR: no BlocksDS $(python "${SCRIPT_DIR}/tools/blocksds_pin.py") found (looked in:" \
         "${BLOCKSDS_CANDIDATES[*]})." >&2
    echo "Build it from docker/versions.env's BLOCKSDS_COMMIT and set HLDS_BLOCKSDS," \
         "or HLDS_BLOCKSDS_ANY=1." >&2
    exit 1
else
    export BLOCKSDS="${HLDS_BLOCKSDS:-${BLOCKSDS:-${WONDERFUL_TOOLCHAIN}/thirdparty/blocksds/core}}"
fi
export BLOCKSDSEXT="${BLOCKSDSEXT:-${WONDERFUL_TOOLCHAIN}/thirdparty/blocksds/external}"
export PATH="${WONDERFUL_TOOLCHAIN}/bin:${PATH}"

cd "${SCRIPT_DIR}"

# The DSP program (DSi audio), if the Teak toolchain is there to build it:
# made into a C array the ARM9 links (source/audio/DspProgram.gen.cpp). With
# no toolchain the array is empty and the game does without the DSP.
python tools/dspsim/embed.py
# The build's name (source/BuildId.gen.cpp), shown on the crash screen.
BUILD_ID="$(python tools/buildid.py)"
# The UI text (source/ui/UiText.gen.*: tools/uitext.py, from text/ui/), and
# its rule held: what uiText gives is never kept (tools/uitext_check.py).
python tools/uitext.py
python tools/uitext_check.py

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
# Its ARM9 image without the overlays (tools/ovl.py): ndstool left them out.
# And the ROM's own overlay table and files are the program's (HLDS_OVL_PACK:
# there is none, the overlays being packed in NitroFS).
if [ -f half-life-nds-port.nds ]; then
    python tools/ovl.py romcheck --rom half-life-nds-port.nds --elf build/arm9/arm9.elf \
        --manifest build/ld/overlays.json --mode "$([ -n "${HLDS_OVL_PACK:-}" ] && echo pack || echo rom)"
fi
# The ELFs of every build, by its name, for looking up a crash screen's
# addresses afterwards (arm-none-eabi-addr2line -e build/elf/<name>/arm9.elf);
# the last twelve kept. With the families' (build/<family>/<family>.elf): an
# address in one is its ELF's at the offset from where it was loaded.
if [ -f build/arm9/arm9.elf ]; then
    mkdir -p "build/elf/${BUILD_ID}"
    cp build/arm9/arm9.elf "build/elf/${BUILD_ID}/"
    for family in boss military net; do
        if [ -f "build/${family}/${family}.elf" ]; then
            cp "build/${family}/${family}.elf" "build/elf/${BUILD_ID}/"
        fi
    done
    ls -1t build/elf | tail -n +13 | while read -r old; do
        rm -rf "build/elf/${old:?}"
    done
fi
# What the fast memories hold and who still calls the library's arithmetic,
# in a line (tools/arm9-report.sh for all of it).
if [ -f build/arm9/arm9.elf ]; then
    tools/arm9-report.sh --summary || true
fi
# The ARM7 program loaded where it runs, every section (tools/arm7_ld.py).
python tools/arm7_ld.py check build/arm7/arm7.elf
# No interrupt handler may reach the divider or the square-root unit (AGENTS.md).
python tools/irq-divider.py build/arm9/arm9.elf child/build/arm9/arm9.elf
