#include "hlds/TitlesFile.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>
#include <cctype>
#include <sstream>

namespace hlds
{
    namespace
    {
        std::string upper(std::string text)
        {
            for (char &c : text)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return text;
        }

        std::string trimmed(const std::string &line)
        {
            std::size_t from = 0, to = line.size();
            while (from < to && std::isspace(static_cast<unsigned char>(line[from])))
                ++from;
            while (to > from && std::isspace(static_cast<unsigned char>(line[to - 1])))
                --to;
            return line.substr(from, to - from);
        }
    } // namespace

    const Title *TitlesBook::find(const std::string &name) const
    {
        const auto found = titles.find(upper(name));
        return found != titles.end() ? &found->second : nullptr;
    }

    TitlesBook parseTitles(const std::string &text)
    {
        TitlesBook book;
        Title now; // the settings as they stand
        std::string name;
        bool inside = false;
        std::string body;
        std::istringstream in(text);
        std::string raw;
        while (std::getline(in, raw))
        {
            if (!raw.empty() && raw.back() == '\r')
                raw.pop_back();
            const std::string line = trimmed(raw);
            if (inside)
            {
                if (line == "}")
                {
                    // (The line before the brace ends it: no empty last line.)
                    while (!body.empty() && body.back() == '\n')
                        body.pop_back();
                    Title title = now;
                    title.text = body;
                    book.titles[upper(name)] = title;
                    inside = false;
                    name.clear();
                    body.clear();
                }
                else
                {
                    // (Text is as it is written, blank lines and all.)
                    body += raw;
                    body += '\n';
                }
                continue;
            }
            if (line.empty() || line.compare(0, 2, "//") == 0)
                continue;
            if (line[0] == '$')
            {
                std::istringstream words(line.substr(1));
                std::string key;
                words >> key;
                double v[3] = {0, 0, 0};
                int given = 0;
                while (given < 3 && (words >> v[given]))
                    ++given;
                if (key == "position" && given >= 2)
                {
                    now.x = v[0];
                    now.y = v[1];
                }
                else if (key == "effect" && given >= 1)
                    now.effect = static_cast<int>(v[0]);
                else if ((key == "color" || key == "color2") && given >= 3)
                {
                    for (int i = 0; i < 3; ++i)
                        (key == "color" ? now.colour : now.colour2)[i] = std::clamp(static_cast<int>(v[i]), 0, 255);
                }
                else if (key == "fadein" && given >= 1)
                    now.fadeIn = v[0];
                else if (key == "fadeout" && given >= 1)
                    now.fadeOut = v[0];
                else if (key == "holdtime" && given >= 1)
                    now.hold = v[0];
                else if (key == "fxtime" && given >= 1)
                    now.fxTime = v[0];
                continue;
            }
            if (line == "{")
            {
                inside = !name.empty();
                continue;
            }
            name = line;
        }
        return book;
    }

    std::string toCp1252(const std::string &bytes)
    {
        std::string text = bytes;
        if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
            text.erase(0, 3);
        // Is it UTF-8, with at least one letter beyond ASCII? (Windows-1252
        // text with an accent is almost never also UTF-8.)
        std::vector<std::uint32_t> points;
        bool wide = false;
        for (std::size_t i = 0; i < text.size();)
        {
            const auto c = static_cast<unsigned char>(text[i]);
            int more = 0;
            std::uint32_t point = c;
            if (c < 0x80)
                more = 0;
            else if ((c & 0xE0) == 0xC0 && c >= 0xC2)
                more = 1, point = c & 0x1Fu;
            else if ((c & 0xF0) == 0xE0)
                more = 2, point = c & 0x0Fu;
            else if ((c & 0xF8) == 0xF0 && c <= 0xF4)
                more = 3, point = c & 0x07u;
            else
                return text;
            if (more > 0 && i + static_cast<std::size_t>(more) >= text.size())
                return text;
            for (int k = 1; k <= more; ++k)
            {
                const auto next = static_cast<unsigned char>(text[i + k]);
                if ((next & 0xC0) != 0x80)
                    return text;
                point = point << 6 | (next & 0x3Fu);
            }
            // (Too long a way of writing a small one, or a surrogate: not UTF-8.)
            if ((more == 2 && (point < 0x800 || (point >= 0xD800 && point < 0xE000))) ||
                (more == 3 && (point < 0x10000 || point > 0x10FFFF)))
                return text;
            wide = wide || more > 0;
            points.push_back(point);
            i += static_cast<std::size_t>(more) + 1;
        }
        if (!wide)
            return text;
        // Windows-1252's 128..159, which are not Latin-1's.
        static const std::uint16_t kHigh[32] = {
            0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
            0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
        std::string out;
        out.reserve(points.size());
        for (std::uint32_t point : points)
        {
            char c = '?';
            if (point < 0x80 || (point >= 0xA0 && point <= 0xFF))
                c = static_cast<char>(point);
            for (int k = 0; k < 32 && c == '?' && point >= 0x100; ++k)
            {
                if (kHigh[k] == point)
                    c = static_cast<char>(0x80 + k);
            }
            out.push_back(c);
        }
        return out;
    }
} // namespace hlds
