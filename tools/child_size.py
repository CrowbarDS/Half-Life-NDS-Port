#!/usr/bin/env python3
"""The Download Play child's size, checked (make.sh runs it after the child).

  child_size.py [child/build/arm9/arm9.elf]

Two limits:
  - its program and data must end below 0x022C0000, where Download Play puts
    the ARM7's program as it arrives;
  - its code and data must be no more than what the converter's account of a
    guest's memory takes them to be (shared/HlGuest.hpp, kGuestImageBytes),
    or that account says maps fit that do not.
A test build (HLDS_CHILD_TESTPACK, which links a map's data in) is reported
but not held to them. A development or measuring build (HLDS_DEV,
HLDS_PROFILE) is held to the first only: the converter's account is of the
release's child, so such a build's child, if sent, may find a map it is
offered too big for -- a test's risk, not a player's.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NM = os.path.join(os.environ.get('WONDERFUL_TOOLCHAIN', '/opt/wonderful'),
                  'toolchain', 'gcc-arm-none-eabi', 'bin', 'arm-none-eabi-nm')
DLPLAY_ARM7_AT = 0x022C0000


def image_limit():
    text = open(os.path.join(ROOT, 'shared', 'HlGuest.hpp')).read()
    m = re.search(r'kGuestImageBytes\s*=\s*0x([0-9A-Fa-f]+)\s*\+\s*(\d+)\s*\*\s*1024', text)
    return int(m.group(1), 16) + int(m.group(2)) * 1024 if m else None


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'child', 'build', 'arm9', 'arm9.elf')
    out = subprocess.run([NM, elf], capture_output=True, text=True).stdout
    end = next((int(line.split()[0], 16) for line in out.splitlines() if line.endswith(' __end__')), None)
    if end is None:
        sys.exit('child_size: no __end__ in %s' % elf)
    image = end - 0x02000000
    test = os.environ.get('HLDS_CHILD_TESTPACK', '').strip() != ''
    unsent = [name for name in ('HLDS_DEV', 'HLDS_PROFILE') if os.environ.get(name, '').strip() not in ('', '0')]
    limit = image_limit()
    print('child: program and data end at %#x (%d KB)%s' % (end, image // 1024, ' -- a test build' if test else ''))
    if test:
        return
    if end > DLPLAY_ARM7_AT:
        sys.exit('child_size: past %#x, where Download Play puts the ARM7\'s program' % DLPLAY_ARM7_AT)
    if unsent:
        if limit is not None and image > limit:
            print('child: %d KB, over what HlGuest.hpp counts (%d KB): allowed in a %s build; '
                  'a map offered to its guests may not fit' % (image // 1024, limit // 1024, ' and '.join(unsent)))
        return
    if limit is not None and image > limit:
        sys.exit('child_size: %d KB, more than HlGuest.hpp counts (%d KB): raise kGuestImageBytes'
                 % (image // 1024, limit // 1024))


if __name__ == '__main__':
    main()
