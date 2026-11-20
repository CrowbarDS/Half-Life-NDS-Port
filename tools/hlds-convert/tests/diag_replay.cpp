// The port's side of tools/ref/diag.sh: a scenario played through the game's
// own code (shared/), on the host, in the console's order of things each
// frame, writing the same log the real game's diagnostic build writes.
//
//   diag_replay <map.bsp> <scenario.txt> <out.csv> [settle frames] [game's log]
//
// The map is the copy diag.sh made (start moved, entities stripped), so both
// play the same map. Every frame is a sixtieth of a second, as the real game's
// run is (host_framerate): one vblank, as on the console at full speed. Given
// the real game's log, each scripted frame's move takes its command's own
// length instead (whole milliseconds there: 17, 17, 16; its physics steps a
// sixtieth), so that the two are given the same steps of time as well as the
// same input. Before
// the script, the player stands for `settle` frames (120) to come to rest on
// the floor, as the real game's player has before its script starts.
//
// Rows (and their columns: the "#cols" lines) are the real game's, in its
// units: whole units Q12 -> float, angles in degrees, the game's buttons.
// What the port has no counterpart for is 0.

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "HlFrameClock.hpp"
#include "TestGame.hpp"

using namespace hldstest;

namespace
{
    // The game's buttons (common/in_buttons.h), for the cmd rows.
    constexpr int kInAttack = 1 << 0, kInJump = 1 << 1, kInDuck = 1 << 2, kInForward = 1 << 3,
                  kInBack = 1 << 4, kInUse = 1 << 5, kInMoveLeft = 1 << 9, kInMoveRight = 1 << 10,
                  kInAttack2 = 1 << 11, kInReload = 1 << 13;

    struct Span
    {
        int frames = 0;
        float forward = 0, side = 0, yaw = 0, pitch = 0;
        int buttons = 0;
    };

    bool loadScenario(const char *path, std::vector<Span> &spans)
    {
        std::ifstream file(path);
        if (!file)
            return false;
        std::string line;
        while (std::getline(file, line))
        {
            const auto hash = line.find('#');
            if (hash != std::string::npos)
                line.resize(hash);
            if (line.rfind("cmd ", 0) == 0)
                continue; // (console commands: nothing on this side yet)
            std::istringstream in(line);
            Span s;
            std::string buttons;
            if (!(in >> s.frames >> s.forward >> s.side >> s.yaw >> s.pitch) || s.frames <= 0)
                continue;
            in >> buttons;
            for (char c : buttons)
            {
                s.buttons |= c == 'a' ? kInAttack : c == 'b' ? kInAttack2 : c == 'u' ? kInUse
                           : c == 'j' ? kInJump : c == 'd' ? kInDuck : c == 'r' ? kInReload : 0;
            }
            spans.push_back(s);
        }
        return !spans.empty();
    }

    double units(fx v) { return static_cast<double>(v) / kFxOne; }
    std::uint16_t turns(float degrees)
    {
        return static_cast<std::uint16_t>(static_cast<std::int32_t>(std::lround(degrees * 65536.0 / 360.0)));
    }

    /// The BSP's model number ("*N") of each brush, found by its bounds at
    /// rest: the level keeps no number of its own.
    std::vector<std::string> brushNames(const Game &g)
    {
        std::vector<std::string> names(g.view.brushCount);
        for (std::uint32_t b = 0; b < g.view.brushCount; ++b)
        {
            const Brush &brush = g.view.brushes[b];
            names[b] = "?";
            for (std::size_t m = 1; m < g.bsp.models.size(); ++m)
            {
                const BspModel &model = g.bsp.models[m];
                bool same = true;
                for (int a = 0; a < 3 && same; ++a)
                    same = std::lround(model.mins[a]) == brush.mins[a] && std::lround(model.maxs[a]) == brush.maxs[a];
                if (same)
                {
                    names[b] = "*" + std::to_string(m);
                    break;
                }
            }
        }
        return names;
    }

    /// Ladder `i`'s BSP model ("*N"), found by its bounds (the converter's
    /// are the model's, rounded out to whole units).
    std::string ladderName(const Game &g, int i)
    {
        const LadderVolume &ladder = g.move.ladders[i];
        for (std::size_t m = 1; m < g.bsp.models.size(); ++m)
        {
            const BspModel &model = g.bsp.models[m];
            bool same = true;
            for (int a = 0; a < 3 && same; ++a)
            {
                same = static_cast<fx>(std::floor(model.mins[a])) == ladder.mins[a] / kFxOne &&
                       static_cast<fx>(std::ceil(model.maxs[a])) == ladder.maxs[a] / kFxOne;
            }
            if (same)
                return "*" + std::to_string(m);
        }
        return "?";
    }

    /// What the frame's move made of a ladder (hldata::diag::ladder).
    struct
    {
        bool on = false;
        diag::Ladder seen{};
    } frameLadder;
} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: diag_replay <map.bsp> <scenario.txt> <out.csv> [settle frames]\n");
        return 2;
    }
    const int settle = argc > 4 ? std::atoi(argv[4]) : 120;
    // The real game's commands' lengths, by frame (0: not known).
    std::vector<int> gameMs;
    if (argc > 5)
    {
        std::ifstream log(argv[5]);
        std::string line;
        while (std::getline(log, line))
        {
            if (line.rfind("cmd,", 0) != 0)
                continue;
            // cmd,frame,seq,t,sframe,msec,...
            std::istringstream in(line);
            std::string field[6];
            for (std::string &f : field)
                std::getline(in, f, ',');
            const int frame = std::atoi(field[1].c_str()), ms = std::atoi(field[5].c_str());
            if (frame > 0)
            {
                if (static_cast<int>(gameMs.size()) <= frame)
                    gameMs.resize(frame + 1, 0);
                gameMs[frame] += ms;
            }
        }
    }
    std::vector<Span> spans;
    if (!loadScenario(argv[2], spans))
    {
        std::fprintf(stderr, "diag_replay: no scenario in %s\n", argv[2]);
        return 1;
    }
    Globals globals;
    auto game = std::make_unique<Game>();
    Game &g = *game;
    std::string error;
    if (!g.bsp.load(argv[1], error))
    {
        std::fprintf(stderr, "diag_replay: %s: %s\n", argv[1], error.c_str());
        return 1;
    }
    finish(g, &globals);
    if (!g.ok)
        return 1;
    FILE *out = std::fopen(argv[3], "w");
    if (!out)
    {
        std::fprintf(stderr, "diag_replay: cannot write %s\n", argv[3]);
        return 1;
    }
    const std::vector<std::string> names = brushNames(g);
    std::vector<int> boxes;
    for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
    {
        if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Pushable) && g.view.logic[i].brush != kNoBrush)
            boxes.push_back(static_cast<int>(i));
    }
    std::fprintf(out, "#diag port 1\n");
    std::fprintf(out, "#cols cmd frame seq t sframe msec fwd side up pitch yaw buttons impulse seed\n");
    std::fprintf(out, "#cols pl frame seq t ox oy oz vx vy vz pitch yaw punchp punchy og duck ground water mt lad\n");
    std::fprintf(out, "#cols phys frame seq t sframe ft\n");
    std::fprintf(out, "#cols box frame seq t id cx cy cz vx vy vz og ground water\n");
    std::fprintf(out, "#cols touch frame seq t id who push result tvx tvy bvx0 bvy0 bvx bvy\n");
    std::fprintf(out, "#cols lad frame seq t id hit frac nx ny nz onfloor fwd right\n");
    diag::ladder = [](const diag::Ladder &seen) {
        frameLadder.on = true;
        frameLadder.seen = seen;
    };

    const auto groundName = [&](const PlayerState &p) -> std::string {
        if (!p.onGround)
            return "-";
        return p.groundMover >= 0 ? names[p.groundMover] : "world";
    };

    FrameClock clock;
    constexpr std::int32_t kVblankTime = 1092; // a sixtieth of a second, Q16 (source/game/Player.arm.cpp)
    long timeMs = 0;
    int frameAll = 0;
    const int scripted = [&] {
        int n = 0;
        for (const Span &s : spans)
            n += s.frames;
        return n;
    }();
    std::size_t spanAt = 0;
    int spanLeft = spans[0].frames;
    // DIAG_VBLANKS=2: the console's pace (about 30 a second), the real
    // game's frames taken two at a time -- their lengths added, their
    // buttons both -- and only every second frame logged.
    const char *pace = std::getenv("DIAG_VBLANKS");
    const int step = pace != nullptr && std::atoi(pace) > 1 ? std::atoi(pace) : 1;
    int heldButtons = 0, heldMs = 0;
    std::int32_t heldDt = 0;
    for (int f = 1 - settle; f <= scripted; ++f, ++frameAll)
    {
        Span span;
        if (f >= 1)
        {
            while (spanLeft == 0)
                spanLeft = spans[++spanAt].frames;
            span = spans[spanAt];
            --spanLeft;
        }
        else
        {
            span.yaw = spans[0].yaw;
            span.pitch = spans[0].pitch;
        }
        // The logic's step: a vblank's, in milliseconds as the console counts
        // them (the real game's physics steps a sixtieth exactly). The
        // player's: the command's length, in the real game whole
        // milliseconds, if known; else a vblank's.
        int ms = clock.ms(1);
        const bool fromGame = f >= 1 && f < static_cast<int>(gameMs.size()) && gameMs[f] > 0;
        std::int32_t dt =
            fromGame ? static_cast<std::int32_t>((static_cast<std::int64_t>(gameMs[f]) << 16) / 1000) : kVblankTime;
        timeMs += ms;
        if (f >= 1 && step > 1)
        {
            heldButtons |= span.buttons;
            heldDt += dt;
            heldMs += ms;
            if (f % step != 0)
                continue;
            span.buttons = heldButtons;
            dt = heldDt;
            ms = heldMs;
            heldButtons = heldMs = 0;
            heldDt = 0;
        }
        const double t = timeMs / 1000.0;
        const bool useHeld = (span.buttons & kInUse) != 0;

        // The view, as the console's camera has it: yaw and pitch in turns,
        // the game's pitch down.
        fx sy, cy, sp, cp;
        fxSinCos(turns(span.yaw), sy, cy);
        fxSinCos(turns(span.pitch), sp, cp);
        const fx forward[3] = {fxMul(cp, cy), fxMul(cp, sy), -sp};

        int buttons = span.buttons;
        buttons |= span.forward > 0 ? kInForward : span.forward < 0 ? kInBack : 0;
        buttons |= span.side > 0 ? kInMoveRight : span.side < 0 ? kInMoveLeft : 0;
        if (f >= 1)
        {
            std::fprintf(out, "cmd,%d,%d,%.4f,%d,%d,%.1f,%.1f,0.0,%.4f,%.4f,%d,0,0\n", f, f, t, frameAll, ms,
                         span.forward, span.side, span.pitch, span.yaw, buttons);
        }

        // What the player's last move ran into: the box's shove, this frame.
        // (The real game shoves it in that move's touch, and moves it in the
        // physics after: the same place between two moves.)
        std::vector<fx> before;
        for (int r : boxes)
        {
            before.push_back(g.states[r].travel[0]);
            before.push_back(g.states[r].travel[1]);
        }
        const int touched = g.player.touchedMover;
        const bool touchedOnGround = g.player.onGround;
        const bool touchedHolding = g.logic.useHeldAtTouch.value; // (USE as it was at the touch)
        const fx touchV[2] = {g.player.touchVelocity[0], g.player.touchVelocity[1]};

        // Game::frame's order (dll/hl/Game.arm.cpp): the logic (boxes
        // move), USE, the monsters, the weapons, then the player's move.
        g.logic.useHeld.value = useHeld;
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        int pulled = -1; // (pulled after the move, as Game::frame does)
        if (useHeld && g.player.health > 0)
        {
            const int target = logicFindUsable(g.logic, eye, forward);
            if (target >= 0 && g.logic.records[target].kind == static_cast<std::uint8_t>(LogicKind::Pushable))
                pulled = target;
        }
        monstersTick(g.logic, g.player, ms);
        g.drain();

        MoveCommand cmd;
        cmd.forward = span.forward > 0 ? 1 : span.forward < 0 ? -1 : 0;
        cmd.side = span.side > 0 ? 1 : span.side < 0 ? -1 : 0;
        cmd.jump = (span.buttons & kInJump) != 0;
        cmd.duck = (span.buttons & kInDuck) != 0;
        cmd.use = useHeld;
        cmd.forwardXY[0] = cy, cmd.forwardXY[1] = sy;
        cmd.rightXY[0] = sy, cmd.rightXY[1] = -cy;
        for (int a = 0; a < 3; ++a)
            cmd.viewForward[a] = forward[a];
        cmd.viewRight[0] = sy, cmd.viewRight[1] = -cy, cmd.viewRight[2] = 0;
        frameLadder.on = false;
        playerMove(g.player, cmd, g.move, dt);
        if (pulled >= 0)
            logicPull(g.logic, g.player, pulled, ms);

        if (f < 1)
            continue;
        for (std::size_t n = 0; n < boxes.size(); ++n)
        {
            const LogicRecord &record = g.view.logic[boxes[n]];
            if (touched != record.brush)
                continue;
            // (Filed under the move that made it, as the real game's are.)
            std::fprintf(out, "touch,%d,%d,%.4f,%s,player,1,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", f - 1, f - 1, t,
                         names[record.brush].c_str(),
                         touchedHolding ? "buttons" : !touchedOnGround && g.player.waterLevel == 0 ? "air" : "ok", units(touchV[0]), units(touchV[1]),
                         units(before[2 * n]), units(before[2 * n + 1]), units(g.states[boxes[n]].travel[0]),
                         units(g.states[boxes[n]].travel[1]));
        }
        const PlayerState &p = g.player;
        const std::string ladder = frameLadder.on ? ladderName(g, frameLadder.seen.index) : "-";
        // (mt: the game's movetype, 5 on a ladder, 3 walking.)
        std::fprintf(out, "pl,%d,%d,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,0,0,%d,%d,%s,%d,%d,%s\n", f, f, t,
                     units(p.origin[0]), units(p.origin[1]), units(p.origin[2]), units(p.velocity[0]),
                     units(p.velocity[1]), units(p.velocity[2]), span.pitch, span.yaw, p.onGround ? 1 : 0,
                     p.ducked ? 1 : 0, groundName(p).c_str(), p.waterLevel, p.onLadder ? 5 : 3, ladder.c_str());
        if (frameLadder.on)
        {
            // (No fraction: the port finds the face without the game's trace.)
            const diag::Ladder &l = frameLadder.seen;
            constexpr double kOne = 1 << 30;
            std::fprintf(out, "lad,%d,%d,%.4f,%s,%d,0,%.4f,%.4f,%.4f,%d,%.1f,%.1f\n", f, f, t, ladder.c_str(),
                         l.hit ? 1 : 0, l.normal[0] / kOne, l.normal[1] / kOne, l.normal[2] / kOne, l.onFloor ? 1 : 0,
                         units(l.forward), units(l.right));
        }
        std::fprintf(out, "phys,%d,%d,%.4f,%d,%.5f\n", f, f, t, frameAll, ms / 1000.0);
        for (int r : boxes)
        {
            const LogicRecord &record = g.view.logic[r];
            const Brush &brush = g.view.brushes[record.brush];
            const fx *pos = g.brushStates[record.brush].pos;
            double c[3];
            for (int a = 0; a < 3; ++a)
                c[a] = units(pos[a]) + (brush.mins[a] + brush.maxs[a]) * 0.5 - units(brush.origin[a]);
            const LogicState &s = g.states[r];
            std::fprintf(out, "box,%d,%d,%.4f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%s,0\n", f, f, t,
                         names[record.brush].c_str(), c[0], c[1], c[2], units(s.travel[0]), units(s.travel[1]),
                         units(s.travel[2]), s.bits != 0 ? 1 : 0, s.bits == 1 ? "world" : s.bits == 2 ? "mover" : "-");
        }
    }
    std::fprintf(out, "END,%d,%d,%.4f\n", scripted, scripted, timeMs / 1000.0);
    std::fclose(out);
    std::printf("%s: %d frames\n", argv[3], scripted);
    return 0;
}
