#pragma once
// What the game's code tells tools/hlds-convert/tests/diag_replay as it runs,
// for its frame-by-frame comparison with the real game (tools/ref/diag.sh):
// hooks it sets, each null until then, called through HL_DIAG.
//
// On a host only. The console's builds (__NDS__) have none of it: HL_DIAG is
// nothing there, and costs nothing; nor is what HL_DIAG_ONLY holds.

#include <cstdint>

#if !defined(__NDS__)
namespace hldata::diag
{
    /// A move on a ladder (playerMove's ladderMove), as the game's
    /// PM_LadderMove would have it: which ladder, whether its face was found
    /// (always, but for a shape the line to its middle missed), the normal
    /// found (unit, Q30), solid under the feet, and the climb speeds the keys
    /// asked for (Q12 units a second).
    struct Ladder
    {
        int index;
        bool hit;
        std::int32_t normal[3];
        bool onFloor;
        std::int32_t forward, right;
    };
    inline void (*ladder)(const Ladder &) = nullptr;

    /// One round of the player's gun, about to be fired (weaponsTick): from
    /// `from` along `dir` (unit, Q12, the spread in it), as far as `range`;
    /// `aim` the way it was aimed before the spread; `n` which of the shot's
    /// rounds (from 1); the spread drawn, -65536..65534 each way, and the
    /// gun's cone (4096ths: the way goes off by draw / 65536 * cone / 4096 of
    /// the view's right and up).
    struct Shot
    {
        const std::int32_t *from;
        const std::int32_t *aim;
        const std::int32_t *dir;
        int range;
        int n;
        int drawX, drawY;
        int cone;
    };
    inline void (*shot)(const Shot &) = nullptr;

    /// A blast (radiusDamage): at `at` (Q12), `damage` at its middle, as far
    /// as `radius`, of the kind `type` (kDmg*).
    struct Boom
    {
        const std::int32_t *at;
        int damage, radius;
        std::uint32_t type;
    };
    inline void (*boom)(const Boom &) = nullptr;

    /// The player hurt (playerTakeDamage): what was asked, from where (null:
    /// nowhere), and the player before and after -- velocity (Q12 units a
    /// second), health and armour.
    struct Hurt
    {
        std::int32_t amount;
        std::uint32_t type;
        bool known;
        std::int32_t from[3];
        std::int32_t velocity0[3], velocity[3];
        int health0, health, armor0, armor;
    };
    inline void (*hurt)(const Hurt &) = nullptr;
} // namespace hldata::diag

#define HL_DIAG(hook, ...)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::hldata::diag::hook != nullptr)                                                                           \
            ::hldata::diag::hook(__VA_ARGS__);                                                                         \
    } while (0)
#define HL_DIAG_ONLY(...) __VA_ARGS__
#else
#define HL_DIAG(hook, ...) ((void)0)
#define HL_DIAG_ONLY(...)
#endif
