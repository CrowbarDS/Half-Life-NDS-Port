#include "game/FamilyApi.hpp"
#include "game/GameLayout.hpp"

#include "HlMonsters.hpp"
#include "HlNetGame.hpp"

// A family of the game (source/game/FamilyApi.hpp), built once for each:
// HL_FAMILY_BOSS, HL_FAMILY_MILITARY or HL_FAMILY_NET says which (build.py).
// Its code is what the core's headers have of it; all else it calls is the
// program's, at the addresses the program has it (the link takes the
// program's ELF first: build.py).

#if defined(HL_FAMILY_BOSS) + defined(HL_FAMILY_MILITARY) + defined(HL_FAMILY_NET) != 1
#error "build one family at a time"
#endif

/// The ID of the program this family is for: tools/dsl_post.py writes it
/// over this placeholder (tools/program_id.py's).
extern "C" __attribute__((visibility("default"), used)) const unsigned char hlFamilyProgramId[hlrt::kProgramIdBytes] = {
    'H', 'L', 'P', 'R', 'O', 'G', 'I', 'D'};

namespace
{
    using namespace hlrt;

    void attach(bool on)
    {
#if defined(HL_FAMILY_BOSS)
        hldata::boss::familyRegister(on);
#elif defined(HL_FAMILY_MILITARY)
        hldata::military::familyRegister(on);
#else
        (void)on;
#endif
    }
} // namespace

extern "C" __attribute__((visibility("default"))) bool GetFamilyAPI(std::uint32_t version,
                                                                    const hlrt::GameLayout &layout,
                                                                    const hlrt::EngineFuncs *engine,
                                                                    hlrt::FamilyFuncs *out)
{
    if (version != hlrt::kFamilyApiVersion || !(layout == hlrt::gameLayout()) || engine != &hlEngine)
        return false;
    *out = hlrt::FamilyFuncs{};
    out->attach = attach;
#ifdef HL_FAMILY_NET
    out->netStart = [](hldata::NetMatch &match, hldata::LogicWorld &w, hldata::PlayerState &player, int firstActor,
                       int model, const hldata::NetSpawn *spawns, int spawnCount, std::uint32_t seed) {
        hldata::netStart(match, w, player, firstActor, model, spawns, spawnCount, seed);
    };
    out->netTick = [](hldata::NetMatch &match, hldata::LogicWorld &w, hldata::PlayerState &player, std::uint16_t yaw,
                      std::int32_t pitch, std::uint32_t did, bool lamp, int ms) {
        hldata::netTick(match, w, player, yaw, pitch, did, lamp, ms);
    };
    out->netRead = [](hldata::NetMatch &match, const std::uint8_t *bytes, int size, int who) {
        hldata::netRead(match, bytes, size, who);
    };
    out->netUsed = [](hldata::NetMatch &match, int index) { hldata::netUsed(match, index); };
    out->netLobby = [](hldata::NetMatch &match, hldata::LogicWorld &none, hldata::PlayerState &nobody) {
        hldata::netLobby(match, none, nobody);
    };
#endif
    return true;
}
