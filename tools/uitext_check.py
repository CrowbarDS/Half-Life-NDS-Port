#!/usr/bin/env python3
"""The UI text's first rule, held by the build (make.sh): what uiText gives
(source/ui/UiText.hpp) is for the draw that asked for it, never kept. A line
of the text may later live in an overlay, gone once another group takes its
place: what is to be shown again keeps the TextRef (or a ui::Label) and asks
each draw.

  uitext_check.py             look through source/, dll/ and shared/
  uitext_check.py --self-test

It fails on a uiText result -- uiText(...) or a Label's .text() -- put in:
a member (a name ending in _), a field (x.y = or x->y =), an element
(x[i] =), or anything static. Kept as a local for the draw, or passed on
straight away, it is not looked at.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOLDERS = ('source', 'dll', 'shared')
RESULT = r'(?:\buiText\s*\(|\.label\.text\s*\(|\bLabel\b[^;]*\.text\s*\()'
KEPT = [
    (re.compile(r'\b\w+_\s*=(?!=)[^;]*' + RESULT), 'a member'),
    (re.compile(r'(?:\.|->)\w+\s*=(?!=)[^;]*' + RESULT), 'a field'),
    (re.compile(r'\]\s*=(?!=)[^;]*' + RESULT), 'an element'),
    (re.compile(r'\bstatic\b[^;=]*=[^;]*' + RESULT), 'a static'),
]


def problems(text, name):
    found = []
    for number, line in enumerate(text.split('\n'), 1):
        code = line.split('//', 1)[0]
        if 'uiText' not in code and '.text(' not in code:
            continue
        for pattern, what in KEPT:
            if pattern.search(code):
                found.append('%s:%d: a uiText result kept in %s: %s' % (name, number, what, line.strip()))
                break
    return found


def self_test():
    bad = ['reason_ = uiText(r);', 'shown.line = uiText(r);', 'p->text = row.label.text();', 'lines[i] = uiText(r);',
           'static const char *keep = uiText(r);']
    good = ['const char *t = uiText(r);', 'drawText(c, f, x, y, uiText(r), ink);', 'if (uiText(r) == nullptr) {}',
            'row.label = ui::txt::menu::Resume;', '// x_ = uiText(r);']
    failed = [b for b in bad if not problems(b, 't')] + [g for g in good if problems(g, 't')]
    for f in failed:
        print('uitext_check.py self-test FAILED on: ' + f, file=sys.stderr)
    print('uitext_check.py self-test: %s' % ('ok' if not failed else '%d failed' % len(failed)))
    return 1 if failed else 0


def main(argv):
    if argv[1:] == ['--self-test']:
        return self_test()
    found = []
    for folder in FOLDERS:
        for dirpath, _dirs, files in os.walk(os.path.join(ROOT, folder)):
            for f in files:
                if f.endswith(('.cpp', '.hpp')) and not f.endswith('.gen.cpp'):
                    path = os.path.join(dirpath, f)
                    with open(path, encoding='utf-8', errors='replace') as file:
                        found += problems(file.read(), os.path.relpath(path, ROOT))
    for f in found:
        print('uitext_check.py: ' + f, file=sys.stderr)
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
