// The game's rooms, heard (shared/HlRoomDsp.hpp): room 0 leaves the sound as
// it is; an echo comes back as far off as the room says; the left side is
// late by its lateness; water dulls; every room dies away once the sound
// stops, at both of the console's rates; and going from one room to another
// never clicks.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "HlRoomDsp.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    RoomDsp room; // (25 KB: not on the stack)

    struct Rate
    {
        int rate, factor;
        const char *name;
    };
    const Rate kRates[] = {{32728, 3, "32 kHz"}, {47605, 4, "47 kHz"}};

    /// The room set up and settled (the change to it over), at a rate.
    void settle(const Rate &at, int which)
    {
        room.init(at.rate, at.factor);
        room.setRoom(which);
        std::vector<std::int16_t> quiet(at.rate / 10, 0);
        room.process(quiet.data(), quiet.data(), quiet.data(), quiet.data(), static_cast<int>(quiet.size()));
    }

    void run(std::vector<std::int16_t> &l, std::vector<std::int16_t> &r)
    {
        // (In the ARM7's 64-sample blocks, as it will be run.)
        for (std::size_t at = 0; at < l.size(); at += 64)
        {
            const int n = static_cast<int>(l.size() - at < 64 ? l.size() - at : 64);
            room.process(l.data() + at, r.data() + at, l.data() + at, r.data() + at, n);
        }
    }

    double energy(const std::vector<std::int16_t> &s, std::size_t from, std::size_t to)
    {
        double sum = 0;
        for (std::size_t i = from; i < to && i < s.size(); ++i)
            sum += static_cast<double>(s[i]) * s[i];
        return sum;
    }

    /// Where the loudest sample is between `from` and `to`.
    std::size_t loudest(const std::vector<std::int16_t> &s, std::size_t from, std::size_t to)
    {
        std::size_t best = from;
        for (std::size_t i = from; i < to && i < s.size(); ++i)
        {
            if (std::abs(s[i]) > std::abs(s[best]))
                best = i;
        }
        return best;
    }
} // namespace

int main()
{
    for (const Rate &at : kRates)
    {
        const std::size_t second = static_cast<std::size_t>(at.rate);
        auto ms = [&](double t) { return static_cast<std::size_t>(t * at.rate / 1000.0); };

        // Room 0: the sound as it is.
        {
            settle(at, 0);
            std::vector<std::int16_t> l(second / 4), r(second / 4), was;
            std::uint32_t seed = 7;
            for (std::size_t i = 0; i < l.size(); ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                l[i] = static_cast<std::int16_t>(static_cast<int>(seed >> 16) - 32768);
                r[i] = static_cast<std::int16_t>(-l[i] / 2);
            }
            was = l;
            const std::vector<std::int16_t> wasR = r;
            run(l, r);
            check(l == was && r == wasR, "room 0 leaves the sound as it is");
        }

        // Room 1, generic: an echo 65 ms on, the left side 10 ms late.
        {
            settle(at, 1);
            std::vector<std::int16_t> l(second / 2, 0), r(second / 2, 0);
            l[0] = r[0] = 20000;
            run(l, r);
            const std::size_t echo = loudest(r, ms(30), ms(200));
            check(echo >= ms(63) && echo <= ms(70) && std::abs(r[echo]) > 500,
                  "generic: an echo comes back 65 ms on");
            check(r[0] == 20000 && l[0] == 0 && std::abs(l[ms(10)]) > 15000,
                  "generic: the left side 10 ms late, the right as it is");
        }

        // A chamber: a reverb, ringing a long time (its feedback of 0.9 every
        // 50 ms is 60 dB down in about 3 s, as the game's chambers ring),
        // steadily quieter.
        {
            settle(at, 9);
            std::vector<std::int16_t> l(3 * second, 0), r(3 * second, 0);
            l[0] = r[0] = 20000;
            run(l, r);
            const double early = energy(r, ms(5), ms(300)), mid = energy(r, ms(700), ms(1000)),
                         late = energy(r, ms(2000), ms(2300));
            check(early > 1e6 && mid < early / 10 && late < mid / 10, "chamber: a reverb, dying away steadily");
            if (!(early > 1e6 && mid < early / 10 && late < mid / 10))
                std::printf("test_roomdsp: %s chamber: %.3g %.3g %.3g\n", at.name, early, mid, late);
        }

        // Water: what is high is taken away, what is low passes.
        {
            settle(at, 14);
            std::vector<std::int16_t> l(second / 4), r(second / 4);
            for (std::size_t i = 0; i < l.size(); ++i)
                l[i] = r[i] = static_cast<std::int16_t>((i & 1) ? 8000 : -8000);
            run(l, r);
            check(energy(r, ms(50), ms(200)) < energy(std::vector<std::int16_t>(ms(150), 8000), 0, ms(150)) / 50,
                  "water: a high sound dulled");
            std::vector<std::int16_t> l2(second / 4, 6000), r2(second / 4, 6000);
            settle(at, 14);
            run(l2, r2);
            check(r2[ms(100)] > 8000 && r2[ms(100)] < 10000, "water: a low one passes (half as loud again)");
        }

        // Every room: loud noise for a second, then quiet -- nothing runs
        // away, and it dies away.
        bool allDie = true, noneStuck = true;
        for (int which = 0; which < kRoomCount; ++which)
        {
            settle(at, which);
            std::vector<std::int16_t> l(5 * second, 0), r(5 * second, 0);
            std::uint32_t seed = 99u + which;
            for (std::size_t i = 0; i < second; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                l[i] = static_cast<std::int16_t>((static_cast<int>(seed >> 16) - 32768) / 2);
                seed = seed * 1664525u + 1013904223u;
                r[i] = static_cast<std::int16_t>((static_cast<int>(seed >> 16) - 32768) / 2);
            }
            run(l, r);
            const double loud = energy(r, 0, second), tail = energy(r, 4 * second, 5 * second);
            if (tail > loud / 200)
            {
                allDie = false;
                std::printf("test_roomdsp: %s room %d: tail %.3g of %.3g\n", at.name, which, tail, loud);
            }
            std::size_t pinned = 0;
            for (std::size_t i = 0; i < second; ++i)
                pinned += (r[i] == 32767 || r[i] == -32768) ? 1 : 0;
            if (pinned > second / 100)
            {
                noneStuck = false;
                std::printf("test_roomdsp: %s room %d: %zu samples at the limit\n", at.name, which, pinned);
            }
        }
        check(allDie, "every room dies away once the sound stops");
        check(noneStuck, "no room drives half-loud noise to the limit");

        // From one room to another, with a tone playing: no click.
        {
            settle(at, 7);
            std::vector<std::int16_t> l(second, 0), r(second, 0);
            for (std::size_t i = 0; i < l.size(); ++i)
                l[i] = r[i] = static_cast<std::int16_t>(6000.0 * std::sin(2 * M_PI * 200.0 * i / at.rate));
            const std::size_t change = (second / 2) & ~std::size_t(63);
            for (std::size_t at64 = 0; at64 < l.size(); at64 += 64)
            {
                if (at64 == change)
                    room.setRoom(22);
                const int n = static_cast<int>(l.size() - at64 < 64 ? l.size() - at64 : 64);
                room.process(l.data() + at64, r.data() + at64, l.data() + at64, r.data() + at64, n);
            }
            int jump = 0;
            for (std::size_t i = change - 64; i < change + ms(60); ++i)
            {
                jump = std::max(jump, std::abs(l[i] - l[i - 1]));
                jump = std::max(jump, std::abs(r[i] - r[i - 1]));
            }
            check(room.room() == 22, "the new room is taken");
            check(jump < 2500, "going from a tunnel to outside does not click");
            if (jump >= 2500)
                std::printf("test_roomdsp: %s: a jump of %d\n", at.name, jump);
        }

        // Weirdo 1: the loudness wanders.
        {
            settle(at, 26);
            std::vector<std::int16_t> l(2 * second, 5000), r(2 * second, 5000);
            run(l, r);
            int least = 32767, most = 0;
            for (std::size_t i = ms(200); i < l.size(); i += ms(5))
            {
                least = std::min<int>(least, r[i]);
                most = std::max<int>(most, r[i]);
            }
            check(most - least > 2000, "weirdo 1: the loudness wanders");
        }
    }

    if (g_failures == 0)
        std::printf("test_roomdsp: ok\n");
    return g_failures == 0 ? 0 : 1;
}
