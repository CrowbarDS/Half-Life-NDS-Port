#include "game/Game.hpp"

#include "game/GameApi.hpp"
#include "Settings.hpp"

// The engine's half of Game: the state is here, the code is the game
// library's (dll/hl/Game.arm.cpp). Each call goes on through its table.

namespace hlrt
{
    Game game;

    // (A console that was sent the game has it linked in: Game's code is
    // the game's own, called straight, and none of this goes between.)
#ifndef HLDS_CHILD
    bool Game::reserve() { return gameApi.reserve(*this); }
    bool Game::create() { return gameApi.create(*this); }
    void Game::destroy() { gameApi.destroy(*this); }
    void Game::frame(int vblanks)
    {
        autoaim = settings.autoaim;
        gameApi.frame(*this, vblanks);
    }
    hldata::Arrival Game::makeArrival() const { return gameApi.makeArrival(*this); }
    bool Game::applyArrival(const hldata::Arrival &arrival) { return gameApi.applyArrival(*this, arrival); }
    void Game::resetProgress() { gameApi.resetProgress(*this); }
    void Game::rememberMap(std::uint32_t mapHash) { gameApi.rememberMap(*this, mapHash); }
    void Game::restoreMap(std::uint32_t mapHash) { gameApi.restoreMap(*this, mapHash); }
    hldata::SaveData Game::snapshot(const std::string &map) { return gameApi.snapshot(*this, map); }
    void Game::adoptProgress(const hldata::SaveData &save) { gameApi.adoptProgress(*this, save); }
    void Game::placeFromSave(const hldata::SaveData &save) { gameApi.placeFromSave(*this, save); }
    bool Game::arrivalOf(const hldata::SaveData &save, hldata::Arrival &out) { return gameApi.arrivalOf(save, out); }
    bool Game::layState(const hldata::SaveData &save) { return gameApi.layState(*this, save); }
    hldata::SaveHeading Game::heading(const std::string &map) const { return gameApi.heading(*this, map); }
    std::uint32_t Game::storedBytes() { return gameApi.storedBytes(*this); }
    bool Game::storeMap(hldata::detail::StreamOut &out) { return gameApi.storeMap(*this, out); }
    bool Game::layStored(hldata::detail::StreamIn &in, const std::int16_t *sentAway, std::uint8_t landed)
    {
        return gameApi.layStored(*this, in, sentAway, landed);
    }
#endif
} // namespace hlrt
