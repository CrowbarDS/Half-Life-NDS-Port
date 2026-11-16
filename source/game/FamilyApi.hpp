#pragma once

// The line between the program and a family of the game: code a map has only
// when it needs it (the bosses, the soldiers, the match's rules), each a
// library of its own in the ROM's NitroFS, dsl/<family>.dsl (built from
// dll/family/). Families.cpp loads them.
//
// A family is linked against the program's own ELF: it calls the core's
// functions and reads its data at the program's addresses, written into it
// as numbers when it was built. There is no table of names between the two.
// That only holds for the very program it was linked against, so each
// program carries an ID (hlProgramId: tools/program_id.py, a hash of its ELF)
// and each family the ID of the program it was built for (hlFamilyProgramId,
// stamped by tools/dsl_post.py). The loader refuses a family whose ID is not
// the program's, before anything of it has run.

#include <cstdint>

#include "game/GameApi.hpp"

namespace hlrt
{
    /// Changed whenever FamilyFuncs changes shape.
    inline constexpr std::uint32_t kFamilyApiVersion = 1;

    /// The size of a program's ID (tools/program_id.py: SIZE).
    inline constexpr int kProgramIdBytes = 8;

    /// What a family gives the program.
    struct FamilyFuncs
    {
        /// The family set up in the core (`on`): its kinds, its schedules,
        /// what the core asks of it (HlAi.hpp's registry); or let go.
        void (*attach)(bool on);
        /// The match's rules: the net family's only (null in the others).
        /// GameFuncs' entries of the same names.
        decltype(GameFuncs::netStart) netStart;
        decltype(GameFuncs::netTick) netTick;
        decltype(GameFuncs::netRead) netRead;
        decltype(GameFuncs::netUsed) netUsed;
        decltype(GameFuncs::netLobby) netLobby;
    };

    /// A family's one function: false if it is not of this program's
    /// headers, or not for this program.
    using GetFamilyApiFn = bool (*)(std::uint32_t version, const GameLayout &layout, const EngineFuncs *engine,
                                    FamilyFuncs *out);
} // namespace hlrt

/// The program's ID (source/game/ProgramId.cpp), stamped after the link.
extern "C" const unsigned char hlProgramId[hlrt::kProgramIdBytes];
