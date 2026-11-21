#!/usr/bin/env python3
"""The game's UI text, compiled from text/ui/<group>.txt into tables the
program reads by reference (source/ui/UiText.hpp: uiText). Run by make.sh
before the ARM build, as tools/buildid.py is.

  uitext.py                  write source/ui/UiText.gen.hpp and .gen.cpp
                             (only what changed: an unchanged tree is not
                             rebuilt)
  uitext.py --self-test      the parser and the writer on made-up groups

A GROUP is a file, text/ui/<group>.txt:

  # a comment
  @home resident             where the group lives: resident (in the
                             program, always there) -- or, later, an
                             overlay's name
  Resume = RESUME            Name = the text: a C identifier, then the text
  SavedAs = Saved as %s\\n    as it is to be shown, C's escapes (\\n \\t \\\\
                             \\" \\x41) and printf's formats as they are

Each group becomes one table: its strings one after another, each with its
terminating NUL, and where each begins (u16); and a descriptor the program
refers to, `kText<Group>`. A reference to one line is
`txt::<group>::<Name>` (a TextRef: the group's descriptor and the line's
number), so that only the groups whose text is used are linked: the
Download Play child takes only its own.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXT_DIR = os.path.join(ROOT, 'text', 'ui')
OUT_HPP = os.path.join(ROOT, 'source', 'ui', 'UiText.gen.hpp')
OUT_CPP = os.path.join(ROOT, 'source', 'ui', 'UiText.gen.cpp')
HOMES = ('resident',)


class TextError(ValueError):
    pass


def unescape(text, where):
    """C's escapes, as a C string literal would have them."""
    out, i = [], 0
    while i < len(text):
        c = text[i]
        if c != '\\':
            out.append(c)
            i += 1
            continue
        if i + 1 >= len(text):
            raise TextError('%s: a \\ at the end of the line' % where)
        n = text[i + 1]
        simple = {'n': '\n', 't': '\t', '\\': '\\', '"': '"', "'": "'", '0': '\0'}
        if n in simple and not (n == '0' and i + 2 < len(text) and text[i + 2] in '01234567'):
            out.append(simple[n])
            i += 2
        elif n == 'x':
            m = re.match(r'[0-9a-fA-F]{2}', text[i + 2:])
            if not m:
                raise TextError('%s: \\x wants two hex digits' % where)
            out.append(chr(int(m.group(0), 16)))
            i += 4
        else:
            raise TextError('%s: unknown escape \\%s' % (where, n))
    return ''.join(out)


def parse(name, source):
    """(home, [(Name, text)]) of one group's file."""
    if not re.fullmatch(r'[a-z][a-z0-9_]*', name):
        raise TextError('group %r: lower case letters, digits, _' % name)
    home, lines, seen = None, [], set()
    for number, raw in enumerate(source.split('\n'), 1):
        where = '%s.txt:%d' % (name, number)
        line = raw.rstrip('\r')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if line.startswith('@home '):
            if home is not None:
                raise TextError('%s: a second @home' % where)
            home = line[6:].strip()
            if home not in HOMES:
                raise TextError('%s: home %r (one of %s)' % (where, home, ', '.join(HOMES)))
            continue
        m = re.match(r'([A-Za-z_][A-Za-z0-9_]*) = (.*)$', line)
        if not m:
            raise TextError('%s: not "Name = text": %r' % (where, line))
        key, text = m.group(1), unescape(m.group(2), where)
        if key in seen:
            raise TextError('%s: %s twice' % (where, key))
        if '\0' in text:
            raise TextError('%s: a NUL in the text' % where)
        seen.add(key)
        lines.append((key, text))
    if home is None:
        raise TextError('%s.txt: no @home' % name)
    if not lines:
        raise TextError('%s.txt: no lines' % name)
    if len(lines) > 0xFFFF:
        raise TextError('%s.txt: too many lines' % name)
    return home, lines


def camel(name):
    return ''.join(part[:1].upper() + part[1:] for part in name.split('_'))


def c_literal(text):
    out = []
    for ch in text:
        code = ord(ch)
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\t':
            out.append('\\t')
        elif code < 32 or code >= 127:
            out.append('\\%03o' % code)
        else:
            out.append(ch)
    return '"' + ''.join(out) + '"'


def make(groups):
    """(header, source) for {name: (home, lines)}."""
    names = sorted(groups)
    hpp = ['// Made by tools/uitext.py from text/ui/: do not edit.', '#pragma once', '',
           '#include "ui/UiTextTypes.hpp"', '', 'namespace hlrt::ui', '{']
    cpp = ['// Made by tools/uitext.py from text/ui/: do not edit.', '#include "ui/UiText.gen.hpp"', '',
           'namespace hlrt::ui', '{']
    for name in names:
        home, lines = groups[name]
        group = camel(name)
        hpp.append('    extern const TextGroupInfo kText%s; ///< text/ui/%s.txt (%s)' % (group, name, home))
        hpp.append('    namespace txt::%s' % name)
        hpp.append('    {')
        for i, (key, _text) in enumerate(lines):
            hpp.append('        inline constexpr TextRef %s{&kText%s, %d};' % (key, group, i))
        hpp.append('    }')
        blob, at = [], []
        offset = 0
        for _key, text in lines:
            at.append(offset)
            blob.append(text)
            offset += len(text.encode('latin-1')) + 1
        if offset > 0xFFFF:
            raise TextError('%s: more than 64 KB of text' % name)
        cpp.append('    namespace')
        cpp.append('    {')
        cpp.append('        const char k%sChars[] =' % group)
        for i, text in enumerate(blob):
            cpp.append('            %s%s' % (c_literal(text), ' "\\0"' if i + 1 < len(blob) else ''))
        cpp.append('            ;')
        cpp.append('        const std::uint16_t k%sAt[] = {%s};' % (group, ', '.join(str(a) for a in at)))
        cpp.append('    }')
        cpp.append('    const TextGroupInfo kText%s = {"%s", k%sChars, k%sAt, %d};' % (group, name, group, group, len(lines)))
    hpp += ['} // namespace hlrt::ui', '']
    cpp += ['} // namespace hlrt::ui', '']
    return '\n'.join(hpp), '\n'.join(cpp)


def read_groups(folder):
    groups = {}
    for entry in sorted(os.listdir(folder)):
        if entry.endswith('.txt'):
            name = entry[:-4]
            with open(os.path.join(folder, entry), encoding='latin-1') as f:
                groups[name] = parse(name, f.read())
    return groups


def write_if_changed(path, text):
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open(path, 'w') as f:
        f.write(text)


def self_test():
    failures = []

    def expect(cond, what):
        if not cond:
            failures.append(what)

    home, lines = parse('menu', '# hello\n@home resident\nResume = RESUME\nSaved = Saved as %s\\n"x"\n\nTab = a\\tb\\\\c\n')
    expect(home == 'resident' and [k for k, _ in lines] == ['Resume', 'Saved', 'Tab'], 'the lines and their names')
    expect(lines[1][1] == 'Saved as %s\n"x"' and lines[2][1] == 'a\tb\\c', 'escapes, formats left as they are')
    for bad in ('Resume = A\n', '@home resident\nResume = A\nResume = B\n', '@home resident\nnot a line\n',
                '@home nowhere\nA = B\n', '@home resident\nA = \\q\n', '@home resident\n'):
        try:
            parse('menu', bad)
            expect(False, 'refused: %r' % bad)
        except TextError:
            pass
    hpp, cpp = make({'menu': ('resident', [('Resume', 'RESUME'), ('Quit', 'QUIT "now"\n')]),
                     'front_end': ('resident', [('Host', 'HOST')])})
    expect('inline constexpr TextRef Resume{&kTextMenu, 0};' in hpp and 'inline constexpr TextRef Quit{&kTextMenu, 1};' in hpp,
           'a reference per line, numbered in order')
    expect('namespace txt::front_end' in hpp and 'kTextFrontEnd' in hpp, 'a group name made a C++ name')
    expect('"RESUME" "\\0"' in cpp and '"QUIT \\"now\\"\\n"' in cpp, 'the strings, NUL after each, escaped again')
    expect('kMenuAt[] = {0, 7}' in cpp, 'where each line begins')
    expect('{"menu", kMenuChars, kMenuAt, 2}' in cpp, 'the descriptor')
    for f in failures:
        print('uitext.py self-test FAILED: ' + f, file=sys.stderr)
    print('uitext.py self-test: %s' % ('ok' if not failures else '%d failed' % len(failures)))
    return 1 if failures else 0


def main(argv):
    if argv[1:] == ['--self-test']:
        return self_test()
    if argv[1:]:
        print(__doc__)
        return 2
    try:
        hpp, cpp = make(read_groups(TEXT_DIR))
    except TextError as e:
        print('uitext.py: %s' % e, file=sys.stderr)
        return 1
    write_if_changed(OUT_HPP, hpp)
    write_if_changed(OUT_CPP, cpp)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
