#pragma once

// What the game's logic spends its time on, part by part, in a build that
// measures it (HLDS_PROFILE): the game sets the clock, and reads (and
// clears) what each part took each frame, as it does the monsters' thinking
// (HlAi.hpp's aiClock). In any other build HL_LOGIC_PART is nothing.

#include <cstdint>

namespace hldata
{
    enum class LogicPart : std::uint8_t
    {
        // logicTick:
        Fires,  ///< what has come due to fire
        Marks,  ///< the map's own marks looked at (every half second)
        Links,  ///< the ways between the nodes looked at (every quarter second)
        Clips,  ///< the actors' clips (actorsTick)
        Movers, ///< each record's waiting and moving: doors, trains, lifts, water
        Lights, ///< switched lights, brushes' textures and conveyors
        // monstersTick, but for the monsters' own thinking (aiTick):
        Hazards, ///< the suit, pushes, places that hurt, crushers, Xen, bodies fading
        AiWorld, ///< aiWorldTick
        Pieces,  ///< what is in flight, and the marks
        Makers,  ///< makers, chargers, guns, mortars, ropes
        Actors,  ///< each actor: items, turrets, mines, people on what moves, and aiTick
        Shots,   ///< projectiles and effects
        Count
    };
    inline constexpr int kLogicParts = static_cast<int>(LogicPart::Count);

    inline std::uint32_t (*partClock)() = nullptr;
    inline std::uint32_t partSpent[kLogicParts] = {};
    /// The movers' part by the kind of each record (LogicKind), and the
    /// one record that took longest in the frame: read and cleared by the
    /// game each frame, as partSpent is.
    inline constexpr int kRecordKinds = 128;
    inline std::uint32_t kindSpent[kRecordKinds] = {};
    inline std::uint32_t recordWorst = 0;
    inline int recordWorstIndex = -1;
    inline int recordWorstKind = 0;

    struct LogicPartScope
    {
        int part;
        std::uint32_t start;
        explicit LogicPartScope(LogicPart p)
            : part(static_cast<int>(p)), start(partClock != nullptr ? partClock() : 0)
        {
        }
        ~LogicPartScope()
        {
            if (partClock != nullptr)
                partSpent[part] += partClock() - start;
        }
        LogicPartScope(const LogicPartScope &) = delete;
        LogicPartScope &operator=(const LogicPartScope &) = delete;
    };
    struct LogicRecordScope
    {
        int index;
        int kind;
        std::uint32_t start;
        LogicRecordScope(int i, int k) : index(i), kind(k), start(partClock != nullptr ? partClock() : 0) {}
        ~LogicRecordScope()
        {
            if (partClock == nullptr)
                return;
            const std::uint32_t spent = partClock() - start;
            kindSpent[kind & (kRecordKinds - 1)] += spent;
            if (spent > recordWorst)
            {
                recordWorst = spent;
                recordWorstIndex = index;
                recordWorstKind = kind;
            }
        }
        LogicRecordScope(const LogicRecordScope &) = delete;
        LogicRecordScope &operator=(const LogicRecordScope &) = delete;
    };
} // namespace hldata

#define HL_LOGIC_PART_NAME2(line) hlLogicPart##line
#define HL_LOGIC_PART_NAME(line) HL_LOGIC_PART_NAME2(line)
#ifdef HLDS_PROFILE
/// Times the rest of the enclosing block as part `p` (hldata::LogicPart).
#define HL_LOGIC_PART(p) const ::hldata::LogicPartScope HL_LOGIC_PART_NAME(__LINE__)(::hldata::LogicPart::p)
/// Times the rest of the enclosing block as record `i`'s, of kind `k`.
#define HL_LOGIC_RECORD(i, k) const ::hldata::LogicRecordScope HL_LOGIC_PART_NAME(__LINE__)((i), (k))
#else
#define HL_LOGIC_PART(p) ((void)0)
#define HL_LOGIC_RECORD(i, k) ((void)0)
#endif
