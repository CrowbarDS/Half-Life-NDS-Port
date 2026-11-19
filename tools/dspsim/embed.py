#!/usr/bin/env python3
"""Builds the DSP program (teak/) and writes it out as a C++ array.

Run by make.sh before the ARM build. Writes source/audio/DspProgram.gen.cpp:
the bytes of build/teak.tlf, or nothing at all if the Teak toolchain or
libteak is not installed (the game then runs without DSP audio, as it does on
a DS).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXT = os.environ.get('BLOCKSDSEXT', '/opt/wonderful/thirdparty/blocksds/external')
OUT = os.path.join(ROOT, 'source', 'audio', 'DspProgram.gen.cpp')
TLF = os.path.join(ROOT, 'build', 'teak.tlf')


def main():
    data = b''
    have = os.path.exists(os.path.join(EXT, 'libteak', 'mk', 'Makefile.teak')) and \
        os.path.exists(os.path.join(EXT, 'llvm-teak', 'bin', 'clang'))
    if have:
        made = subprocess.run(['make', '-s', '-f', 'Makefile.teak'], cwd=ROOT,
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if made.returncode != 0:
            sys.stderr.write(made.stderr)
            sys.exit('the DSP program did not build')
        with open(TLF, 'rb') as f:
            data = f.read()
    else:
        print('[*] no Teak toolchain in %s: building without DSP audio' % EXT)

    lines = ['// Made by tools/dspsim/embed.py from build/teak.tlf: do not edit.',
             '#include <cstdint>', '#include <nds/ndstypes.h>', '',
             '// (Four-byte aligned: the loader reads its headers as words. In the',
             '// DSi\'s memory alone, TWL_DATA: a DS has no DSP to run it. Its size',
             '// in the program\'s own, which says whether there is one, on either.)',
             'extern const std::uint32_t hlDspProgramBytes = %d;' % len(data),
             'alignas(4) extern const std::uint8_t hlDspProgram[] TWL_DATA = {']
    for at in range(0, len(data), 16):
        lines.append('    ' + ' '.join('0x%02x,' % b for b in data[at:at + 16]))
    if not data:
        lines.append('    0,')
    lines.append('};')
    text = '\n'.join(lines) + '\n'
    old = None
    if os.path.exists(OUT):
        with open(OUT) as f:
            old = f.read()
    if old != text:
        with open(OUT, 'w') as f:
            f.write(text)


if __name__ == '__main__':
    main()
