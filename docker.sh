#!/usr/bin/env bash
# Run a command in the build container (docker/Dockerfile), in this folder.
#
#   ./docker.sh ./make.sh                 # the ROM
#   ./docker.sh                           # a shell
#   WITH_TEAK=1 ./docker.sh ./make.sh     # ... with the DSP toolchain built in
#   WITH_QT=1 ./docker.sh ...             # ... with Qt 6.8 (the converter's window)
#
# HLDS_VALVE_DIR=<your Half-Life "valve" folder> makes it readable inside, as
# /valve: for converting the game and for the tests that play its maps.
#
# The image is built the first time, and again when something in docker/ has
# changed.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

export HLDS_UID="${HLDS_UID:-$(id -u)}" HLDS_GID="${HLDS_GID:-$(id -g)}"
export WITH_TEAK="${WITH_TEAK:-0}"
export WITH_QT="${WITH_QT:-0}"

docker compose build dev

extra=()
if [ -n "${HLDS_VALVE_DIR:-}" ]; then
    extra+=(-v "${HLDS_VALVE_DIR}:/valve:ro" -e HLDS_VALVE_DIR=/valve)
fi
if [ $# -eq 0 ]; then
    set -- bash
fi
exec docker compose run --rm ${extra[@]+"${extra[@]}"} dev "$@"
