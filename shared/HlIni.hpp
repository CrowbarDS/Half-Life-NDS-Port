#pragma once

// A text file of `key = value` lines, as a person writes one (server.ini):
// what one key says. Lines starting with `#` or `;` are comments, `[...]`
// lines are headings and are passed over, spaces around the key and the
// value do not count, and a key is matched whatever its case. The first
// line with the key is the one taken.
//
// std-only, no heap: the console reads its server.ini with it, and the
// converter's tests run it.

#include <cstddef>
#include <cstring>

// Where these are put: the program's menu (ONLINE SETTINGS, SERVER
// SETTINGS), the only one of it that reads or writes these files, has them in
// the front end's overlay (source/ui/Menu.cpp defines this before it takes
// this header); and how they are linked: a library linked against the
// program's ELF (dll/online/) has copies of its own, which do not answer to
// the program's names (theirs are in that overlay, which a library may not
// reach). Elsewhere, where they fall.
#ifndef HL_INI_CODE
#define HL_INI_CODE
#endif
#ifndef HL_INI_LINKAGE
#define HL_INI_LINKAGE inline
#endif

namespace hldata
{
    namespace ini_detail
    {
        inline bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
        inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
    } // namespace ini_detail

    /// The value of `key` in `text` (`size` bytes) into `out` (`room` bytes,
    /// cut short to fit, always ended). False if no line has the key.
    HL_INI_LINKAGE HL_INI_CODE bool iniValue(const char *text, std::size_t size, const char *key, char *out,
                                             std::size_t room)
    {
        using namespace ini_detail;
        if (room == 0)
            return false;
        out[0] = 0;
        std::size_t at = 0;
        while (at < size)
        {
            std::size_t end = at;
            while (end < size && text[end] != '\n')
                ++end;
            std::size_t a = at;
            while (a < end && space(text[a]))
                ++a;
            std::size_t eq = a;
            while (eq < end && text[eq] != '=')
                ++eq;
            if (a < end && text[a] != '#' && text[a] != ';' && text[a] != '[' && eq < end)
            {
                std::size_t keyEnd = eq;
                while (keyEnd > a && space(text[keyEnd - 1]))
                    --keyEnd;
                std::size_t k = 0;
                bool same = true;
                for (std::size_t i = a; i < keyEnd && same; ++i, ++k)
                    same = key[k] != 0 && lower(text[i]) == lower(key[k]);
                if (same && key[k] == 0 && keyEnd > a)
                {
                    std::size_t v = eq + 1;
                    while (v < end && space(text[v]))
                        ++v;
                    std::size_t vEnd = end;
                    while (vEnd > v && space(text[vEnd - 1]))
                        --vEnd;
                    std::size_t n = 0;
                    for (; v < vEnd && n + 1 < room; ++v)
                        out[n++] = text[v];
                    out[n] = 0;
                    return true;
                }
            }
            at = end + 1;
        }
        return false;
    }

    /// ... as a number from `low` to `high`; `fallback` if it is not there or
    /// not one of them.
    HL_INI_LINKAGE HL_INI_CODE int iniNumber(const char *text, std::size_t size, const char *key, int fallback, int low,
                                             int high)
    {
        char value[16];
        if (!iniValue(text, size, key, value, sizeof(value)) || value[0] == 0)
            return fallback;
        long n = 0;
        for (const char *c = value; *c != 0; ++c)
        {
            if (*c < '0' || *c > '9' || n > high)
                return fallback;
            n = n * 10 + (*c - '0');
        }
        return n < low || n > high ? fallback : static_cast<int>(n);
    }

    /// `text` (`size` bytes) with `key` given `value`, into `out` (`room`
    /// bytes, ended): the first line with the key (as iniValue finds it) made
    /// `key = value`, every other line as it was -- comments, headings,
    /// other keys -- and the line added at the end if no line has it. Its
    /// length, or -1 if it would not fit.
    HL_INI_LINKAGE HL_INI_CODE int iniSet(const char *text, std::size_t size, const char *key, const char *value,
                                          char *out, std::size_t room)
    {
        using namespace ini_detail;
        std::size_t n = 0;
        bool fits = true, done = false;
        auto put = [&](const char *from, std::size_t count) {
            if (!fits || n + count >= room)
            {
                fits = false;
                return;
            }
            std::memcpy(out + n, from, count);
            n += count;
        };
        std::size_t at = 0;
        while (at < size)
        {
            std::size_t end = at;
            while (end < size && text[end] != '\n')
                ++end;
            // (This line's key, as iniValue reads one.)
            bool same = false;
            std::size_t a = at;
            while (a < end && space(text[a]))
                ++a;
            std::size_t eq = a;
            while (eq < end && text[eq] != '=')
                ++eq;
            if (!done && a < end && text[a] != '#' && text[a] != ';' && text[a] != '[' && eq < end)
            {
                std::size_t keyEnd = eq;
                while (keyEnd > a && space(text[keyEnd - 1]))
                    --keyEnd;
                std::size_t k = 0;
                same = keyEnd > a;
                for (std::size_t i = a; i < keyEnd && same; ++i, ++k)
                    same = key[k] != 0 && lower(text[i]) == lower(key[k]);
                same = same && key[k] == 0;
            }
            if (same)
            {
                put(key, std::strlen(key));
                put(" = ", 3);
                put(value, std::strlen(value));
                put("\n", 1);
                done = true;
            }
            else
            {
                put(text + at, end - at);
                if (end < size)
                    put("\n", 1);
            }
            at = end + 1;
        }
        if (!done)
        {
            if (n > 0 && out[n - 1] != '\n')
                put("\n", 1);
            put(key, std::strlen(key));
            put(" = ", 3);
            put(value, std::strlen(value));
            put("\n", 1);
        }
        if (!fits || room == 0)
            return -1;
        out[n] = 0;
        return static_cast<int>(n);
    }
} // namespace hldata
