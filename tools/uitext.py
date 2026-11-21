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
                             program, always there), or an overlay's
                             (source/Overlay.hpp: front) -- read through a
                             gate that loads it; only what is never asked
                             for with something else in its region
  @unique                    no two lines of it alike (names of things)
  @enum shared/X.hpp Kind kKindCount
                             its first lines are named, in order, as the
                             enumerators of enum Kind in that header (the
                             last, a count, left out): line(k) is k's
  Resume = RESUME            Name = the text: a C identifier, then the text
  SavedAs = Saved as %s\\n    as it is to be shown, C's escapes (\\n \\t \\\\
                             \\" \\x41) and printf's formats as they are;
                             never empty

Each group becomes one table, its strings one after another, each with its
terminating NUL (a text two lines share is kept once), and a descriptor the
program refers to, `kText<Group>`. A reference to one line is
`txt::<group>::<Name>` (a TextRef: the group's descriptor and where the line
begins in its text, so that no table of where is needed), and only the groups
whose text is used are linked: the Download Play child takes only its own.
Where a line is chosen by a number (an achievement's), `txt::<group>::
line(n)` is the group's nth line, through a table of where each begins that
is linked only where it is used.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXT_DIR = os.path.join(ROOT, 'text', 'ui')
OUT_HPP = os.path.join(ROOT, 'source', 'ui', 'UiText.gen.hpp')
OUT_CPP = os.path.join(ROOT, 'source', 'ui', 'UiText.gen.cpp')
HOMES = ('resident', 'front')
RESERVED = ('kLines', 'kCount', 'line')


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


def read_root(path):
    with open(os.path.join(ROOT, path), encoding='latin-1') as f:
        return f.read()


def enumerators(header, enum, where):
    """The names of `enum` in a C++ header, in order (none given a value)."""
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', header, flags=re.S)
    m = re.search(r'\benum\s+(?:class\s+)?%s\b[^{;]*\{(.*?)\}' % re.escape(enum), text, re.S)
    if not m:
        raise TextError('%s: no enum %s there' % (where, enum))
    names = []
    for item in m.group(1).split(','):
        item = item.strip()
        if not item:
            continue
        if not re.fullmatch(r'[A-Za-z_]\w*', item):
            raise TextError('%s: enum %s: %r (a value given: not followed)' % (where, enum, item))
        names.append(item)
    return names


def parse(name, source, read=read_root):
    """(home, [(Name, text)]) of one group's file."""
    if not re.fullmatch(r'[a-z][a-z0-9_]*', name):
        raise TextError('group %r: lower case letters, digits, _' % name)
    home, lines, seen = None, [], set()
    unique, named = False, None
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
        if line.strip() == '@unique':
            unique = True
            continue
        if line.startswith('@enum '):
            parts = line.split()
            if len(parts) not in (3, 4) or named is not None:
                raise TextError('%s: @enum <header> <enum> [<count left out>], once' % where)
            try:
                header = read(parts[1])
            except OSError:
                raise TextError('%s: no header %s' % (where, parts[1]))
            named = enumerators(header, parts[2], where)
            if len(parts) == 4:
                if not named or named[-1] != parts[3]:
                    raise TextError('%s: %s is not the last of enum %s' % (where, parts[3], parts[2]))
                named.pop()
            continue
        m = re.match(r'([A-Za-z_][A-Za-z0-9_]*) = (.*)$', line)
        if not m:
            raise TextError('%s: not "Name = text": %r' % (where, line))
        key, text = m.group(1), unescape(m.group(2), where)
        if key in seen:
            raise TextError('%s: %s twice' % (where, key))
        if '\0' in text:
            raise TextError('%s: a NUL in the text' % where)
        if not text:
            raise TextError('%s: %s is empty' % (where, key))
        if key in RESERVED:
            raise TextError('%s: %s is a name the group already has' % (where, key))
        seen.add(key)
        lines.append((key, text))
    if home is None:
        raise TextError('%s.txt: no @home' % name)
    if not lines:
        raise TextError('%s.txt: no lines' % name)
    if len(lines) > 0xFFFF:
        raise TextError('%s.txt: too many lines' % name)
    if unique:
        texts = {}
        for key, text in lines:
            if text in texts:
                raise TextError('%s.txt: %s and %s say the same' % (name, texts[text], key))
            texts[text] = key
    if named is not None:
        keys = [key for key, _ in lines[:len(named)]]
        if keys != named:
            wrong = next((i for i, (a, b) in enumerate(zip(keys, named)) if a != b), len(keys))
            raise TextError('%s.txt: line %d of the enum\'s should be %s' % (
                name, wrong + 1, named[wrong] if wrong < len(named) else '(none)'))
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
           '#include "Overlay.hpp"', '', 'namespace hlrt::ui', '{']
    for name in names:
        home, lines = groups[name]
        group = camel(name)
        blob, at, seen = [], [], {}
        offset = 0
        for _key, text in lines:
            if text in seen:
                at.append(seen[text])
                continue
            seen[text] = offset
            at.append(offset)
            blob.append(text)
            offset += len(text.encode('latin-1')) + 1
        if offset > 0xFFFF:
            raise TextError('%s: more than 64 KB of text' % name)
        hpp.append('    extern const TextGroupInfo kText%s; ///< text/ui/%s.txt (%s)' % (group, name, home))
        hpp.append('    namespace txt::%s' % name)
        hpp.append('    {')
        for i, (key, _text) in enumerate(lines):
            hpp.append('        inline constexpr TextRef %s{&kText%s, %d};' % (key, group, at[i]))
        hpp.append('        inline constexpr unsigned kCount = %d;' % len(lines))
        hpp.append('        /// (Linked only where line() is: where each line begins.)')
        hpp.append('        inline constexpr std::uint16_t kLines[kCount] = {%s};' % ', '.join(str(a) for a in at))
        hpp.append('        /// The nth line, in the file\'s order.')
        hpp.append('        constexpr TextRef line(unsigned n) { return TextRef{&kText%s, kLines[n]}; }' % group)
        hpp.append('    }')
        cpp.append('    namespace')
        cpp.append('    {')
        if home == 'resident':
            cpp.append('        const char k%sChars[] =' % group)
        else:
            cpp.append('        HL_OVL_RODATA(%s) const char k%sChars[] =' % (home, group))
        for i, text in enumerate(blob):
            cpp.append('            %s%s' % (c_literal(text), ' "\\0"' if i + 1 < len(blob) else ''))
        cpp.append('            ;')
        if home != 'resident':
            # (Its way in: the overlay loaded, a halt naming it if it may not
            # be -- something else holding the region. A program without
            # overlays has the text as its own.)
            cpp.append('#if HL_OVL_COUNT > 0')
            cpp.append('        HL_OVL_GATED const char *bring%s()' % group)
            cpp.append('        {')
            cpp.append('            ::hlrt::ovl::load(::hlrt::ovl::Id::%s);' % camel(home))
            cpp.append('            HL_OVL_GATE(%s);' % camel(home))
            cpp.append('            return k%sChars;' % group)
            cpp.append('        }')
            cpp.append('#endif')
        cpp.append('    }')
        if home == 'resident':
            cpp.append('    const TextGroupInfo kText%s = {"%s", k%sChars, %d, nullptr};' % (group, name, group, offset))
        else:
            cpp.append('#if HL_OVL_COUNT > 0')
            cpp.append('    const TextGroupInfo kText%s = {"%s", nullptr, %d, &bring%s};' % (group, name, offset, group))
            cpp.append('#else')
            cpp.append('    const TextGroupInfo kText%s = {"%s", k%sChars, %d, nullptr};' % (group, name, group, offset))
            cpp.append('#endif')
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
    expect('inline constexpr TextRef Resume{&kTextMenu, 0};' in hpp and 'inline constexpr TextRef Quit{&kTextMenu, 7};' in hpp,
           'a reference per line, where it begins')
    expect('namespace txt::front_end' in hpp and 'kTextFrontEnd' in hpp, 'a group name made a C++ name')
    expect('"RESUME" "\\0"' in cpp and '"QUIT \\"now\\"\\n"' in cpp, 'the strings, NUL after each, escaped again')
    expect('{"menu", kMenuChars, 19, nullptr}' in cpp, 'the descriptor, the size with the last NUL')
    _hpp, cpp = make({'lobby': ('front', [('Host', 'HOST')])})
    expect('HL_OVL_RODATA(front) const char kLobbyChars[]' in cpp and 'HL_OVL_GATE(Front);' in cpp and
           '{"lobby", nullptr, 5, &bringLobby}' in cpp and '{"lobby", kLobbyChars, 5, nullptr}' in cpp,
           'a group in an overlay: its text there, read through a gate')
    _hpp, cpp = make({'opts': ('resident', [('On', 'on'), ('Off', 'off'), ('Again', 'on')])})
    expect('Again{&kTextOpts, 0}' in _hpp and 'Off{&kTextOpts, 3}' in _hpp and cpp.count('"on"') == 1,
           'a text two lines share kept once')
    expect('kLines[kCount] = {0, 3, 0}' in _hpp and 'kCount = 3;' in _hpp, 'the table of where, by number')
    fake = {'k.hpp': 'enum Kind : int // the kinds\n{\n    kA, ///< one\n    kB,\n    kKindCount\n};\n',
            'v.hpp': 'enum Kind { kA = 2, kB };'}

    def read(path):
        if path not in fake:
            raise OSError(path)
        return fake[path]
    _home, lines = parse('k', '@home resident\n@enum k.hpp Kind kKindCount\nkA = a\nkB = b\nMore = c\n', read)
    expect([k for k, _ in lines] == ['kA', 'kB', 'More'], 'an enum\'s lines, then others')
    for bad in ('@home resident\n@enum k.hpp Kind kKindCount\nkB = b\nkA = a\n',
                '@home resident\n@enum k.hpp Kind kKindCount\nkA = a\n',
                '@home resident\n@enum k.hpp Kind\nkA = a\nkB = b\n',
                '@home resident\n@enum k.hpp Other\nkA = a\n',
                '@home resident\n@enum v.hpp Kind\nkA = a\n',
                '@home resident\n@enum none.hpp Kind\nkA = a\n',
                '@home resident\n@unique\nA = same\nB = same\n',
                '@home resident\nA = \n', '@home resident\nkLines = x\n'):
        try:
            parse('k', bad, read)
            expect(False, 'refused: %r' % bad)
        except TextError:
            pass
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
