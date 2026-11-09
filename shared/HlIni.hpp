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

namespace hldata
{
    namespace ini_detail
    {
        inline bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
        inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
    } // namespace ini_detail

    /// The value of `key` in `text` (`size` bytes) into `out` (`room` bytes,
    /// cut short to fit, always ended). False if no line has the key.
    inline bool iniValue(const char *text, std::size_t size, const char *key, char *out, std::size_t room)
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
    inline int iniNumber(const char *text, std::size_t size, const char *key, int fallback, int low, int high)
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
} // namespace hldata
