# Everything needed to build the ROM, the converter and the tests, on any
# system that runs Docker: BlocksDS, the Nitro Engine Advanced fork this port
# is written on, the dswifi fork its wireless is, ptexconv, a host compiler
# and CMake.
#
#   docker compose build                       # the default image
#   docker compose build --build-arg WITH_TEAK=1
#       ... with the DSi's DSP toolchain as well (the teak-llvm and libteak
#       forks). That compiles clang: allow an hour and several GB of memory.
#       Without it the ROM builds all the same, with no DSP audio -- which is
#       an experiment, off by default in the game.
#   docker compose build --build-arg WITH_QT=1
#       ... with Qt 6.8 as well, to build the converter's window
#       (hlds-convert-gui) in the container: a check that it builds. The
#       program is linked to the container's Qt; to use it, build it where
#       it is to run (see the README).
#
# What it is made of is pinned: the BlocksDS image just below, the rest in
# versions.env. Only those two files and fetch.sh go into the image, so it
# holds the tools and nothing of the game or of this repository.

ARG BLOCKSDS_IMAGE=skylyrac/blocksds:slim-v1.25.0@sha256:c5f19b95d9c2f312e6a68ea244d2ed5120203b4b82af9785937a349096f8e5e4
ARG WITH_TEAK=0
ARG WITH_QT=0

# ---------------------------------------------------------------- the tools
FROM ${BLOCKSDS_IMAGE} AS base

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++ cmake ninja-build git ca-certificates \
        python3 python3-venv python3-pip && \
    rm -rf /var/lib/apt/lists/*

COPY versions.env fetch.sh /opt/hlds/

# ptexconv, which the converter runs for textures. (BlocksDS and its compiler
# are the image's own, at the versions it was made with.)
RUN . /opt/hlds/versions.env && \
    sh /opt/hlds/fetch.sh "$PTEXCONV_REPO" "$PTEXCONV_COMMIT" /tmp/ptexconv && \
    mkdir -p "${BLOCKSDSEXT}/ptexconv" && \
    gcc -O2 /tmp/ptexconv/src/*.c -lm -o "${BLOCKSDSEXT}/ptexconv/ptexconv" && \
    cp /tmp/ptexconv/LICENSE "${BLOCKSDSEXT}/ptexconv/" && \
    rm -rf /tmp/ptexconv

# ------------------------------------------- the DSP toolchain, if asked for
FROM base AS teak-0

FROM base AS teak-1
# (How many compilers at once: each can take a gigabyte and more. And this
# LLVM is older than the compiler here, which no longer brings in <cstdint>.)
ARG TEAK_JOBS=4
RUN . /opt/hlds/versions.env && \
    sh /opt/hlds/fetch.sh "$TEAK_LLVM_REPO" "$TEAK_LLVM_COMMIT" /tmp/teak-llvm && \
    cd /tmp/teak-llvm && \
    CXXFLAGS="-include cstdint" JOBS="${TEAK_JOBS}" ./build-teak.sh configure build install && \
    rm -rf /tmp/teak-llvm
RUN . /opt/hlds/versions.env && \
    sh /opt/hlds/fetch.sh "$LIBTEAK_REPO" "$LIBTEAK_COMMIT" /tmp/libteak && \
    make -C /tmp/libteak install && \
    rm -rf /tmp/libteak

# ---------------------------------------------------- Nitro Engine Advanced
# (With its sound: the game is built with NEA_MAXMOD.)
FROM teak-${WITH_TEAK} AS dev
ARG WITH_TEAK
ARG NEA_JOBS=4
RUN . /opt/hlds/versions.env && \
    sh /opt/hlds/fetch.sh "$NEA_REPO" "$NEA_COMMIT" /tmp/nitro-engine && \
    make -C /tmp/nitro-engine -f Makefile.blocksds -j"${NEA_JOBS}" install NEA_MAXMOD=1 NEA_TEAK="${WITH_TEAK}" && \
    rm -rf /tmp/nitro-engine

# The wireless: the dswifi fork that also does DS Download Play, installed as
# "dswifi_dl" beside the stock one. (After the engine: nothing of that
# depends on it, and it is the game that links it.)
RUN . /opt/hlds/versions.env && \
    sh /opt/hlds/fetch.sh "$DSWIFI_REPO" "$DSWIFI_COMMIT" /tmp/dswifi && \
    make -C /tmp/dswifi -j"${NEA_JOBS}" && make -C /tmp/dswifi install && \
    rm -rf /tmp/dswifi

# The ROM's build system is this repository's own copy of architectds (found
# through PYTHONPATH once the repository is mounted); what it needs besides
# is here, where any user of the container can read it.
RUN . /opt/hlds/versions.env && python3 -m venv /opt/hlds-venv && \
    /opt/hlds-venv/bin/pip install --no-cache-dir "ninja==${NINJA_VERSION}" "pillow==${PILLOW_VERSION}"
ENV HLDS_VENV=/opt/hlds-venv \
    PYTHONPATH=/work/architectds_nea_mod \
    HOME=/tmp

# (The repository is mounted here, whoever owns it.)
RUN git config --system --add safe.directory /work
WORKDIR /work
CMD ["bash"]

# ------------------------------------- Qt for the converter's window, if asked
# (aqtinstall: the Qt the window needs, 6.8, is newer than many systems'
# own. Widgets and what they stand on only.)
FROM dev AS qt-0

FROM dev AS qt-1
USER root
RUN . /opt/hlds/versions.env && \
    apt-get update && apt-get install -y --no-install-recommends \
        libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libfontconfig1-dev \
        libfreetype6-dev libdbus-1-3 libglib2.0-dev && \
    rm -rf /var/lib/apt/lists/* && \
    python3 -m venv /opt/aqt && \
    /opt/aqt/bin/pip install --no-cache-dir "aqtinstall==${AQT_VERSION}" && \
    /opt/aqt/bin/aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O /opt/qt && \
    rm -rf /opt/aqt && \
    ln -s "/opt/qt/${QT_VERSION}/gcc_64" /opt/qt/current
ENV HLDS_QT_DIR=/opt/qt/current

FROM qt-${WITH_QT} AS final
