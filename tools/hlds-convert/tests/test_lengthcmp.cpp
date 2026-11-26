// Comparisons of a length made without its root (HlFixed.hpp's
// fxLengthBelow, fxLengthAtMost, fxLengthSquared), each form as a site of the
// game's has it, against the form it replaced: the same answer, every time.
//
// fxLength(v) is isqrt64(S), S its square (Q24), rounded down. So, for an r:
//   L <  r  <=>  S < r^2           (fxLengthBelow)
//   L >= r  <=>  S >= r^2          (!fxLengthBelow)
//   L <= r  <=>  S < (r + 1)^2     (fxLengthAtMost)
//   L >  r  <=>  S >= (r + 1)^2    (!fxLengthAtMost)
// with r <= 0 kept right too. Each site's form is held to its old one over
// the squares at the edges (r^2 - 1, r^2, r^2 + 1, (r + 1)^2 - 1, (r + 1)^2)
// and random ones, for r across the range the game uses and beyond (Q12
// lengths up to 2^24, a 4096-unit vector), negative r included.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "HlFixed.hpp"

using namespace hldata;

namespace
{
    int g_failures = 0;
    long g_checked = 0;

    std::uint64_t g_seed = 0x9E3779B97F4A7C15ull;
    std::uint64_t next()
    {
        g_seed ^= g_seed << 13;
        g_seed ^= g_seed >> 7;
        g_seed ^= g_seed << 17;
        return g_seed;
    }

    /// fxLength of a vector whose square is S: its root, rounded down.
    /// (Not every S is a vector's square of a chosen shape: the forms are
    /// held to each other at the level of S, which is all they read; the
    /// helpers themselves, on vectors, further down.)
    fx lengthOf(std::int64_t s) { return static_cast<fx>(isqrt64(static_cast<std::uint64_t>(s))); }

    /// One site's two forms, by the square S and the r of its comparison.
    struct Form
    {
        const char *site;
        std::function<bool(std::int64_t s, fx r)> old;
        std::function<bool(std::int64_t s, fx r)> made;
    };

    // The new forms read a vector; at the S level, a vector of that square.
    // (fxLengthBelow and fxLengthAtMost read only S: fxLengthSquared.)
    bool below(std::int64_t s, fx r) { return r > 0 && s < static_cast<std::int64_t>(r) * r; }
    bool atMost(std::int64_t s, fx r)
    {
        return r >= 0 && s < (static_cast<std::int64_t>(r) + 1) * (static_cast<std::int64_t>(r) + 1);
    }

    void check(bool ok, const char *site, std::int64_t s, fx r)
    {
        ++g_checked;
        if (!ok && ++g_failures <= 10)
            std::printf("FAIL: %s at S %lld, r %ld\n", site, static_cast<long long>(s), static_cast<long>(r));
    }
} // namespace

int main()
{
    // Each form as the sites write it (the r: the site's own constant or
    // expression; here every r of the range).
    const std::vector<Form> forms = {
        {"L < r (Effects 388, AiTalk 774, Walk 1106, PlayerMove 1171, Aliens 1479/1920/3835/4083, Sound 1072)",
         [](std::int64_t s, fx r) { return lengthOf(s) < r; }, below},
        {"L >= r (Aliens 1149, Monsters 5533, Logic 4291/4578)",
         [](std::int64_t s, fx r) { return lengthOf(s) >= r; }, [](std::int64_t s, fx r) { return !below(s, r); }},
        {"L <= r (Monsters 2628)", [](std::int64_t s, fx r) { return lengthOf(s) <= r; }, atMost},
        {"L > r (AiTalk 896, Walk 1080, PlayerMove 1285, Actors 1553/1567, Aliens 1784/2095/3031, People 47, "
         "Monsters 2599/2860/5487, Logic 1918/3242)",
         [](std::int64_t s, fx r) { return lengthOf(s) > r; }, [](std::int64_t s, fx r) { return !atMost(s, r); }},
        {"clear + 48 >= L (Walk 833: r = clear + 48)", [](std::int64_t s, fx r) { return r >= lengthOf(s); }, atMost},
        {"L * 4 >= step (Walk 1342: r = step)",
         [](std::int64_t s, fx r) { return lengthOf(s) * 4 >= r; },
         [](std::int64_t s, fx r) { return !below(s, (r + 3) >> 2); }},
        {"L > 0 (PlayerMove 940)", [](std::int64_t s, fx) { return lengthOf(s) > 0; },
         [](std::int64_t s, fx) { return s > 0; }},
    };

    for (const Form &form : forms)
    {
        auto both = [&](std::int64_t s, fx r)
        {
            if (s < 0)
                return;
            check(form.old(s, r) == form.made(s, r), form.site, s, r);
        };
        // Every r from -64 to 4096 (Q12: up to a unit), and then r across the
        // range in random steps; the squares at each one's edges.
        auto edges = [&](fx r)
        {
            const std::int64_t r2 = static_cast<std::int64_t>(r) * r;
            const std::int64_t r12 = (static_cast<std::int64_t>(r) + 1) * (static_cast<std::int64_t>(r) + 1);
            for (const std::int64_t s : {r2 - 1, r2, r2 + 1, r12 - 1, r12, r12 + 1, std::int64_t(0), std::int64_t(1)})
                both(s, r);
            for (int k = 0; k < 4; ++k)
                both(static_cast<std::int64_t>(next() >> (16 + next() % 32)), r);
        };
        for (fx r = -64; r <= 4096; ++r)
            edges(r);
        for (int k = 0; k < 400000; ++k)
        {
            const int bits = 1 + static_cast<int>(next() % 25); // (r below 2^25: lengths to 8192 units)
            const fx r = static_cast<fx>(next() & ((1u << bits) - 1)) * ((next() & 7) == 0 ? -1 : 1);
            edges(r);
        }
        // The game's own constants, Q12 (fxFromInt of the sites' units).
        for (const int units : {1, 4, 8, 30, 32, 40, 48, 60, 64, 96, 128, 256, 400, 512, 750, 1024, 2000})
            edges(fxFromInt(units));
    }

    // The vector level: fxLengthSquared is the square fxLength takes, and the
    // helpers on a vector answer as its fxLength does.
    for (int k = 0; k < 2000000; ++k)
    {
        const int bits = 1 + static_cast<int>(next() % 26);
        fx v[3];
        for (fx &c : v)
            c = static_cast<fx>(static_cast<std::int32_t>(next() & ((1u << bits) - 1)) - (1 << (bits - 1)));
        const std::int64_t s = static_cast<std::int64_t>(v[0]) * v[0] + static_cast<std::int64_t>(v[1]) * v[1] +
                               static_cast<std::int64_t>(v[2]) * v[2];
        const fx length = fxLength(v);
        const fx r = (next() & 1) ? length + static_cast<fx>(next() % 5) - 2 : static_cast<fx>(next() & ((1u << bits) - 1));
        check(fxLengthSquared(v) == s, "fxLengthSquared is fxLength's square", s, r);
        check(fxLengthBelow(v, r) == (length < r), "fxLengthBelow on a vector", s, r);
        check(fxLengthAtMost(v, r) == (length <= r), "fxLengthAtMost on a vector", s, r);
    }

    std::printf("test_lengthcmp: %ld comparisons, every form against the one it replaced\n", g_checked);
    if (g_failures == 0)
        std::printf("test_lengthcmp: ok\n");
    return g_failures == 0 ? 0 : 1;
}
