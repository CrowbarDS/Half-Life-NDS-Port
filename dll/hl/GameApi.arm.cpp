#include "game/GameLayout.hpp"

// The game library's one export (GameApi.hpp): the game's table, for the
// engine's. What is not one of Game's own functions is here, ARM like the
// rest of the game's code.

namespace
{
    using namespace hlrt;

    constexpr GameFuncs kGame = {
        .reserve = [](Game &g) { return g.reserve(); },
        .create = [](Game &g) { return g.create(); },
        .destroy = [](Game &g) { g.destroy(); },
        .frame = [](Game &g, int vblanks) { g.frame(vblanks); },
        .makeArrival = [](const Game &g) { return g.makeArrival(); },
        .applyArrival = [](Game &g, const hldata::Arrival &arrival) { return g.applyArrival(arrival); },
        .resetProgress = [](Game &g) { g.resetProgress(); },
        .rememberMap = [](Game &g, std::uint32_t mapHash) { g.rememberMap(mapHash); },
        .restoreMap = [](Game &g, std::uint32_t mapHash) { g.restoreMap(mapHash); },
        .snapshot = [](Game &g, const std::string &map) { return g.snapshot(map); },
        .adoptProgress = [](Game &g, const hldata::SaveData &save) { g.adoptProgress(save); },
        .placeFromSave = [](Game &g, const hldata::SaveData &save) { g.placeFromSave(save); },
        .arrivalOf = [](const hldata::SaveData &save, hldata::Arrival &out) { return Game::arrivalOf(save, out); },
        .layState = [](Game &g, const hldata::SaveData &save) { return g.layState(save); },
        .heading = [](const Game &g, const std::string &map) { return g.heading(map); },
        .storedBytes = [](Game &g) { return g.storedBytes(); },
        .storeMap = [](Game &g, hldata::detail::StreamOut &out) { return g.storeMap(out); },
        .layStored = [](Game &g, hldata::detail::StreamIn &in, const std::int16_t *sentAway, std::uint8_t landed) {
            return g.layStored(in, sentAway, landed);
        },

        .thinkRunOne = [](hldata::LogicWorld &w, void (*yield)()) {
            hldata::thinkYield = yield;
            return hldata::thinkRunOne(w);
        },
        .thinkClear = [](hldata::ThinkQueue &q) { hldata::thinkClear(q); },
        .hurtPlayer = [](hldata::PlayerState &player, std::int32_t amount, std::uint32_t type)
        { hldata::logicHurt(player, amount, 0, type); },
        .killAllOfKind = [](hldata::LogicWorld &w, hldata::ActorKind kind) { return hldata::killAllOfKind(w, kind); },
        .netStart = [](hldata::NetMatch &match, hldata::LogicWorld &w, hldata::PlayerState &player, int firstActor,
                       int model, const hldata::NetSpawn *spawns, int spawnCount, std::uint32_t seed) {
            hldata::netStart(match, w, player, firstActor, model, spawns, spawnCount, seed);
        },
        .netTick = [](hldata::NetMatch &match, hldata::LogicWorld &w, hldata::PlayerState &player, std::uint16_t yaw,
                      std::int32_t pitch, std::uint32_t did, bool lamp, int ms) {
            hldata::netTick(match, w, player, yaw, pitch, did, lamp, ms);
        },
        .netRead = [](hldata::NetMatch &match, const std::uint8_t *bytes, int size, int who) {
            hldata::netRead(match, bytes, size, who);
        },
        .netUsed = [](hldata::NetMatch &match, int index) { hldata::netUsed(match, index); },
        .netLobby = [](hldata::NetMatch &match, hldata::LogicWorld &none, hldata::PlayerState &nobody) {
            hldata::netLobby(match, none, nobody);
        },
    };
} // namespace

extern "C" __attribute__((visibility("default"))) bool GetGameAPI(std::uint32_t version,
                                                                  const hlrt::GameLayout &layout,
                                                                  const hlrt::EngineFuncs *engine,
                                                                  hlrt::GameFuncs *game)
{
    if (version != hlrt::kGameApiVersion || !(layout == hlrt::gameLayout()) || engine != &hlEngine ||
        engine->version != hlrt::kGameApiVersion)
        return false;
    *game = kGame;
    return true;
}
