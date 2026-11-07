// The early game's own machinery, on its own maps: a train that is turned
// (the tram's door), the turning lift, lights that are switched and that
// flicker (the level's LightPatch records), and the microwave's chain of
// buttons with everything it sets off.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>

#include "HlLightStyle.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    std::unique_ptr<Game> load(const char *dir, const char *map)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
        finish(*game, nullptr, dir);
        return game->ok ? std::move(game) : nullptr;
    }

    int nameOf(const Game &g, const char *name)
    {
        const std::uint32_t hash = nameHash(name);
        for (std::uint32_t i = 1; i < g.view.nameCount; ++i)
        {
            if (g.view.nameHashes[i] == hash)
                return static_cast<int>(i);
        }
        return 0;
    }

    void testStyles()
    {
        check(lightStyleValue(0, 5, 0) == kStyleWhole && lightStyleValue(40, 5, 0) == kStyleWhole &&
                  lightStyleValue(40, 5, std::uint64_t(1) << 40) == 0 && lightStyleValue(41, 5, std::uint64_t(1) << 40) == kStyleWhole,
              "plain light is whole; a switched light is all or nothing, each by its own switch");
        // (Style 4 is "mamamama...": on and off by turns. Style 2 swells
        // from dark to twice as bright and back.)
        check(lightStyleValue(4, 0, 0) == kStyleWhole && lightStyleValue(4, 1, 0) == 0 && lightStyleValue(4, 12, 0) == kStyleWhole &&
                  lightStyleValue(2, 0, 0) == 0 && lightStyleValue(2, 25, 0) > 500 && lightStyleValue(2, 51, 0) == 0,
              "the game's patterns play a letter each tenth of a second, round and round");
        std::uint16_t values[kLightStyles] = {};
        const bool first = lightStyleValues(0, 0, values);
        const bool same = lightStyleValues(0, 0, values);
        const bool flicker = lightStyleValues(1, 0, values);
        check(first && !same && flicker, "a change of any style is told, and no change is not");
        // A vertex: dim by itself, with a switched light adding to it.
        const LightPatch patches[] = {{2, 4 | 4 << 5 | 4 << 10, kPatchBase, 0},
                                      {2, 20 | 10 << 5 | 0 << 10, 40, 0},
                                      {5, 31 | 31 << 5 | 31 << 10, kPatchBase, 0},
                                      {5, 8 | 8 << 5 | 8 << 10, 4, 0}};
        std::uint32_t stream[8] = {};
        lightStyleValues(0, 0, values);
        lightPatchApply(patches, 4, values, stream);
        const bool lit = stream[2] == (24u | 14u << 5 | 4u << 10) && stream[5] == (31u | 31u << 5 | 31u << 10);
        lightStyleValues(1, std::uint64_t(1) << 40, values);
        lightPatchApply(patches, 4, values, stream);
        check(lit && stream[2] == (4u | 4u << 5 | 4u << 10) && stream[5] == (31u | 31u << 5 | 31u << 10) && stream[0] == 0,
              "a vertex is what it is without the changing light, plus what each adds at its strength; never past white");
    }

    void testTramDoor(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c0a0e");
        check(game != nullptr, "the tram's last map cooks");
        if (game == nullptr)
            return;
        const int name = nameOf(*game, "traindoor");
        int door = -1;
        for (std::uint32_t i = 0; i < game->view.logicCount; ++i)
        {
            if (game->view.logic[i].name == name && game->view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Train))
                door = static_cast<int>(i);
        }
        check(door >= 0, "the tram's door is a train of its own");
        if (door < 0)
            return;
        const LogicRecord &record = game->view.logic[door];
        const BrushState &brush = game->brushStates[record.brush];
        check(record.extra[1] == 16384 && brush.angles[1] == 16384 && brush.angles[0] == 0 && brush.angles[2] == 0 &&
                  game->movers[record.brush].turned,
              "... and is a quarter turn round from how it was built, as the map says: drawn so, and solid so");
    }

    /// A door on hinges starts turned as its map turns it (the game's
    /// CRotDoor opens FROM its angles): c1a0's keypad, which flips out of
    /// the console the scientist uses, and c1a0e's cover over the reactor's
    /// motor button.
    void testHinged(const char *dir)
    {
        // An entity's angles as the engine reads them: the last of "angle"
        // and "angles" written.
        Entity both;
        both.pairs = {{"classname", "func_door_rotating"}, {"angles", "0 0 0"}, {"angle", "315"}};
        Entity back;
        back.pairs = {{"angle", "90"}, {"angles", "10 20 30"}};
        double a[3], b[3];
        entityAngles(both, a);
        entityAngles(back, b);
        check(a[0] == 0 && a[1] == 315 && a[2] == 0 && b[0] == 10 && b[1] == 20 && b[2] == 30,
              "of \"angle\" and \"angles\", the one written last is what an entity is turned by");
        struct Case
        {
            const char *map, *name;
            int yaw, axis, swing;
        };
        for (const Case &c : {Case{"c1a0", "introroomgizmokeys", 315, kPitch, 185},
                              Case{"c1a0e", "motor_button_cover", 45, kRoll, 160}})
        {
            const std::unique_ptr<Game> game = load(dir, c.map);
            if (game == nullptr)
            {
                check(false, "the map with a hinged brush cooks");
                continue;
            }
            const int name = nameOf(*game, c.name);
            int at = -1;
            for (int i = 0; i < game->logic.recordCount; ++i)
            {
                if (game->logic.records[i].name == name &&
                    game->logic.records[i].kind == static_cast<std::uint8_t>(LogicKind::DoorRotating))
                    at = i;
            }
            if (at < 0)
            {
                check(false, "the hinged brush is there");
                continue;
            }
            const LogicRecord &record = game->logic.records[at];
            const BrushState &brush = game->logic.brushStates[record.brush];
            const auto turn = [](int degrees) { return static_cast<std::uint16_t>((degrees * 65536 / 360) & 0xFFFF); };
            const bool rests = brush.angles[0] == 0 && brush.angles[1] == turn(c.yaw) && brush.angles[2] == 0;
            logicUse(game->logic, game->player, at, -1, kUseToggle);
            for (int t = 0; t < 120; ++t)
                logicTick(game->logic, game->player, 33);
            // (Both are "reverse direction": the swing is back, and the yaw
            // they rest at is kept while they swing.)
            const BrushState &open = game->logic.brushStates[record.brush];
            const int swung = static_cast<std::int16_t>(open.angles[c.axis]);
            const auto off = static_cast<std::int16_t>(open.angles[c.axis] - turn(-c.swing));
            const bool opens = open.angles[kYaw] == turn(c.yaw) && std::abs(off) < 400;
            std::printf("test_incident: %s's %s rests at yaw %d, swings %d on its hinge\n", c.map, c.name,
                        brush.angles[1] * 360 / 65536, swung * 360 / 65536);
            check(rests && opens, "a door on hinges rests turned as its map turns it, and swings from there");
        }
    }

    void testLift(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a0b");
        check(game != nullptr, "the lift's map cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const int lift = g.recordOfKind(LogicKind::PlatRot);
        check(lift >= 0, "the round lift is in the level");
        if (lift < 0)
            return;
        const LogicRecord &record = g.view.logic[lift];
        BrushState &brush = g.brushStates[record.brush];
        const fx z0 = brush.pos[2];
        const std::uint16_t yaw0 = brush.angles[1];
        std::printf("test_incident: the lift travels %d units and turns %d degrees; it starts turned %d\n",
                    fxToInt(record.move[2]), static_cast<int>(static_cast<std::int32_t>(record.extra[0])) * 360 / 65536,
                    yaw0 * 360 / 65536);
        check(record.move[2] == fxFromInt(216) && static_cast<std::int32_t>(record.extra[0]) == 16384 && yaw0 == 16384,
              "it is built turned a quarter, with 216 units to go");
        // Someone on it goes with it, and is turned as far.
        const MoverHull &mover = g.movers[record.brush];
        for (int a = 0; a < 2; ++a)
            g.player.origin[a] = (mover.mins[a] + mover.maxs[a]) / 2 + fxFromInt(a == 0 ? 20 : 0);
        g.player.origin[2] = mover.maxs[2] + fxFromInt(37);
        g.player.onGround = true;
        g.player.groundMover = static_cast<std::int16_t>(record.brush);
        const fx rider0[3] = {g.player.origin[0] - brush.pos[0], g.player.origin[1] - brush.pos[1], g.player.origin[2] - brush.pos[2]};
        logicUse(g.logic, g.player, lift, -1, kUseToggle);
        int turned = 0, ticks = 0;
        for (; ticks < 400 && g.states[lift].move != MoveState::AtTop; ++ticks)
        {
            g.player.onGround = true;
            g.player.groundMover = static_cast<std::int16_t>(record.brush);
            logicTick(g.logic, g.player, 33);
            turned += static_cast<std::int16_t>(g.logic.viewTurn);
            g.logic.viewTurn = 0;
        }
        const fx rider1[3] = {g.player.origin[0] - brush.pos[0], g.player.origin[1] - brush.pos[1], g.player.origin[2] - brush.pos[2]};
        std::printf("test_incident: after %d ticks it is %d units from where it began, turned %d; the rider's view turned %d\n",
                    ticks, fxToInt(brush.pos[2] - z0), brush.angles[1] * 360 / 65536, turned * 360 / 65536);
        check(g.states[lift].move == MoveState::AtTop && brush.pos[2] - z0 == fxFromInt(216) && brush.angles[1] == 0,
              "used, it travels its 216 units and turns its quarter");
        // (Where they stood on it, turned a quarter the same way; and no
        // nearer or farther from its floor.)
        const fx dz = rider1[2] - rider0[2];
        check(turned < -16000 && turned > -16800 && dz > -fxFromInt(2) && dz < fxFromInt(2) &&
                  std::abs(fxToInt(rider1[0])) + std::abs(fxToInt(rider1[1])) > 15,
              "... and whoever stands on it goes with it, turned as far");
        logicUse(g.logic, g.player, lift, -1, kUseToggle);
        for (int n = 0; n < 400 && g.states[lift].move != MoveState::AtBottom; ++n)
            logicTick(g.logic, g.player, 33);
        check(brush.pos[2] == z0 && brush.angles[1] == yaw0, "used again, it goes back");
    }

    void testLights(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a0b");
        if (game == nullptr)
            return;
        Game &g = *game;
        const LevelView &v = g.view;
        int lights = 0, first = -1;
        std::set<int> styles;
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
        {
            if (v.logic[i].kind != static_cast<std::uint8_t>(LogicKind::Light))
                continue;
            ++lights;
            styles.insert(static_cast<int>(v.logic[i].extra[0]));
            first = first < 0 ? static_cast<int>(i) : first;
        }
        std::set<int> patched;
        std::uint32_t bases = 0;
        bool ordered = true;
        for (std::uint32_t i = 0; i < v.lightPatchCount; ++i)
        {
            const LightPatch &p = v.lightPatches[i];
            if (p.style == kPatchBase)
            {
                ++bases;
                // (Each base is followed by at least one thing added.)
                ordered = ordered && i + 1 < v.lightPatchCount && v.lightPatches[i + 1].style != kPatchBase &&
                          v.lightPatches[i + 1].word == p.word;
            }
            else
            {
                patched.insert(p.style);
            }
        }
        std::printf("test_incident: c1a0b has %d switched lights of %zu styles; %u vertices in changing light (%u records)\n",
                    lights, styles.size(), bases, v.lightPatchCount);
        bool covered = true;
        for (int style : styles)
            covered = covered && patched.count(style) != 0;
        check(lights >= 5 && bases > 50 && ordered && covered,
              "the lights a switch works are in the level, and the vertices each reaches are known");
        if (first < 0)
            return;
        // Switch one: its vertices change, and change back.
        const int style = static_cast<int>(v.logic[first].extra[0]);
        std::vector<std::uint32_t> stream(v.gxStream, v.gxStream + v.gxWordCount);
        std::uint16_t values[kLightStyles] = {};
        logicTick(g.logic, g.player, 33);
        lightStyleValues(0, g.logic.stylesOff, values);
        lightPatchApply(v.lightPatches, v.lightPatchCount, values, stream.data());
        // (As cooked: what the stream has is what the same sum gives.)
        int differ = 0;
        for (std::uint32_t i = 0; i < v.gxWordCount; ++i)
            differ += stream[i] != v.gxStream[i];
        check(differ == 0, "what is cooked into the vertices is what the console's own sum gives as the map starts");
        // (By its name, as a switch does: every light of that name with it.)
        auto switchIt = [&]()
        {
            for (std::uint32_t i = 0; i < v.logicCount; ++i)
            {
                if (v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Light) && v.logic[i].name == v.logic[first].name)
                    logicUse(g.logic, g.player, static_cast<int>(i), -1, kUseToggle);
            }
        };
        const bool wasOff = (g.logic.stylesOff >> style) & 1;
        switchIt();
        logicTick(g.logic, g.player, 33);
        const bool nowOff = (g.logic.stylesOff >> style) & 1;
        lightStyleValues(0, g.logic.stylesOff, values);
        lightPatchApply(v.lightPatches, v.lightPatchCount, values, stream.data());
        int changed = 0, darker = 0;
        for (std::uint32_t i = 0; i < v.gxWordCount; ++i)
        {
            if (stream[i] == v.gxStream[i])
                continue;
            ++changed;
            darker += (stream[i] & 31) <= (v.gxStream[i] & 31);
        }
        std::printf("test_incident: switching style %d (%s) changes %d vertices\n", style, nowOff ? "off" : "on", changed);
        check(wasOff != nowOff && changed > 5 && (nowOff ? darker == changed : darker < changed),
              "a light switched changes the vertices it reaches: darker off, brighter on");
        switchIt();
        logicTick(g.logic, g.player, 33);
        lightStyleValues(0, g.logic.stylesOff, values);
        lightPatchApply(v.lightPatches, v.lightPatchCount, values, stream.data());
        differ = 0;
        for (std::uint32_t i = 0; i < v.gxWordCount; ++i)
            differ += stream[i] != v.gxStream[i];
        check(differ == 0, "... and switched back, they are as they were");
    }

    /// t0a0's jump lamps (jump1l..jump3l, styles 36..38): small boards whose
    /// switched light falls inside them, not on their corners. Each is lit
    /// by its light as its brightest sample is (not left near dark), and
    /// the trigger that should switch it on does.
    void testHazardLamps(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "t0a0");
        if (game == nullptr)
            return;
        Game &g = *game;
        const LevelView &v = g.view;
        int most[3] = {0, 0, 0};
        for (std::uint32_t i = 0; i < v.lightPatchCount; ++i)
        {
            const int style = v.lightPatches[i].style;
            if (style < 36 || style > 38)
                continue;
            const int c = v.lightPatches[i].colour;
            most[style - 36] = std::max({most[style - 36], c & 31, (c >> 5) & 31, (c >> 10) & 31});
        }
        std::printf("test_incident: t0a0's jump lamps add at most +%d +%d +%d\n", most[0], most[1], most[2]);
        check(most[0] >= 14 && most[1] >= 14 && most[2] >= 14, "t0a0's jump lamps light up when switched on");
        g.frame();
        const bool offBefore = (g.logic.stylesOff >> 36) & 1;
        logicFire(g.logic, g.player, static_cast<std::uint16_t>(nameOf(g, "jump1l")), -1, kUseToggle, 0);
        g.frame();
        check(offBefore && !((g.logic.stylesOff >> 36) & 1), "... and the first is off until its trigger fires it");
    }

    /// t0a0a's valve: the wheel turns the bridge (a wheel too, flagged to be
    /// turned only with it); the bridge itself is not used by hand, even by
    /// someone standing on it.
    void testWheelBridge(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "t0a0a");
        if (game == nullptr)
            return;
        Game &g = *game;
        int wheel = -1, bridge = -1;
        for (int i = 0; i < g.logic.recordCount; ++i)
        {
            const LogicRecord &r = g.logic.records[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::MomentaryButton) || r.target == 0)
                continue;
            if (r.flags & kLogicTriggerOnly)
                bridge = i;
            else
                wheel = i;
        }
        check(wheel >= 0 && bridge >= 0 && g.logic.records[wheel].target == g.logic.records[bridge].target,
              "t0a0a has the valve's wheel, and the bridge it turns");
        if (wheel < 0 || bridge < 0)
            return;
        // From the middle of the bridge, looking anywhere: not the bridge.
        const MoverHull &hull = g.movers[g.logic.records[bridge].brush];
        const fx eye[3] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2, hull.mins[2] + fxFromInt(100)};
        bool usedBridge = false;
        const fx ways[4][3] = {{kFxOne, 0, 0}, {-kFxOne, 0, 0}, {0, kFxOne, 0}, {0, 0, -kFxOne}};
        for (const auto &way : ways)
            usedBridge = usedBridge || logicFindUsable(g.logic, eye, way) == bridge;
        check(!usedBridge, "... which is not used by hand, even standing on it");
        // The wheel, used: it goes round, and the bridge with it.
        const BrushState &turned = g.brushStates[g.logic.records[bridge].brush];
        const std::uint16_t before[3] = {turned.angles[0], turned.angles[1], turned.angles[2]};
        logicUse(g.logic, g.player, wheel, -1, kUseSet);
        for (int n = 0; n < 30 * 20 && g.states[wheel].running; ++n)
            g.frame();
        std::printf("test_incident: t0a0a's wheel at %d, the bridge at %d, turned %d %d %d -> %d %d %d\n",
                    static_cast<int>(g.states[wheel].phase), static_cast<int>(g.states[bridge].phase), before[0],
                    before[1], before[2], turned.angles[0], turned.angles[1], turned.angles[2]);
        check(!g.states[wheel].running && g.states[wheel].phase == 65536 && g.states[bridge].phase == 65536 &&
                  turned.angles[1] != before[1],
              "... and the wheel, turned to its end, has turned the bridge as far round");
    }

    /// t0a0b's range: a shot (an MP5's grenade's line too) meets a target
    /// at its face, not a crouched person's size out from it, and one that
    /// passes beside it does not meet it.
    void testTargetFace(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "t0a0b");
        if (game == nullptr)
            return;
        Game &g = *game;
        int tested = 0, onFace = 0, missed = 0;
        for (int i = 0; i < g.logic.recordCount; ++i)
        {
            const LogicRecord &r = g.logic.records[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::GunTarget) || r.brush == kNoBrush)
                continue;
            const MoverHull &hull = g.movers[r.brush];
            if (!hull.solid || hull.turned)
                continue;
            // Its panel as it is: its model's box, where the brush is.
            const Model &model = g.view.models[g.view.brushes[r.brush].model];
            fx mins[3], maxs[3];
            for (int k = 0; k < 3; ++k)
            {
                mins[k] = fxFromInt(model.mins[k]) + hull.origin[k];
                maxs[k] = fxFromInt(model.maxs[k]) + hull.origin[k];
            }
            // Across its thin way, from 12 out to its middle.
            const int thin = (maxs[1] - mins[1]) < (maxs[0] - mins[0]) ? 1 : 0;
            const int wide = 1 - thin;
            fx mid[3], from[3];
            for (int k = 0; k < 3; ++k)
                mid[k] = from[k] = (mins[k] + maxs[k]) / 2;
            from[thin] = mins[thin] - fxFromInt(12);
            const Shot hit = traceShot(g.move, from, mid);
            const fx stop = from[thin] + static_cast<fx>((static_cast<std::int64_t>(mid[thin] - from[thin]) * hit.fraction) >>
                                                         kFractionShift);
            ++tested;
            onFace += hit.mover == static_cast<int>(r.brush) && stop >= mins[thin] - fxFromInt(1) &&
                      stop <= mins[thin] + fxFromInt(1);
            // The same, 10 beside its edge.
            fx besideFrom[3], besideTo[3];
            for (int k = 0; k < 3; ++k)
            {
                besideFrom[k] = from[k];
                besideTo[k] = mid[k];
            }
            besideFrom[wide] = besideTo[wide] = maxs[wide] + fxFromInt(10);
            missed += traceShot(g.move, besideFrom, besideTo).mover != static_cast<int>(r.brush);
        }
        std::printf("test_incident: t0a0b's targets: %d, met at the face %d, missed beside %d\n", tested, onFace, missed);
        check(tested >= 7 && onFace == tested && missed == tested,
              "a shot meets a range target at its face, and misses one it passes beside");
    }

    /// t0a0's numbers, arrows and the rest of its signs (infodecals of
    /// decals.wad's own pictures): each a mark of one of the map's sprites.
    void testSigns(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "t0a0");
        if (game == nullptr)
            return;
        Game &g = *game;
        int signs = 0, inRange = 0;
        std::set<std::uint32_t> pictures;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Mark) || r.extra[0] != kDecalSign)
                continue;
            ++signs;
            inRange += r.extra[2] < g.view.spriteCount;
            pictures.insert(r.extra[2]);
        }
        std::printf("test_incident: t0a0 has %d signs, of %zu pictures\n", signs, pictures.size());
        // (Its large 1 to 4, its numbers 1 to 3, an arrow and a turn.)
        check(signs >= 30 && inRange == signs && pictures.size() == 9, "t0a0's numbers and signs are the map's own sprites");
        // Near the first large number: it is put on its wall.
        g.player.origin[0] = fxFromInt(492);
        g.player.origin[1] = fxFromInt(880);
        g.player.origin[2] = fxFromInt(-43);
        for (int n = 0; n < 5; ++n)
            g.frame();
        int placed = 0;
        for (int i = 0; i < kMaxDecals; ++i)
        {
            const Decal &d = g.actors.decals[i];
            if (!d.active || d.picture != kDecalSign)
                continue;
            ++placed;
            std::printf("test_incident: a sign at %d %d %d facing %d %d %d, %d across\n", fxToInt(d.at[0]),
                        fxToInt(d.at[1]), fxToInt(d.at[2]), d.normal[0], d.normal[1], d.normal[2], d.half * 2);
        }
        check(placed > 0, "... and those near the player are on their walls");
    }

    /// c1a0d's suit, taken: it says its whole logon (!HEV_AAx), at once and
    /// at the suit's volume, with no pickup sound; and nothing more until
    /// that is said.
    void testSuitLogon(Game &g)
    {
        int suit = -1;
        for (int i = 0; i < g.actors.count && suit < 0; ++i)
        {
            if (g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                g.actors.states[i].item == static_cast<std::uint8_t>(ItemKind::Suit))
                suit = i;
        }
        check(suit >= 0, "c1a0d has the suit");
        if (suit < 0)
            return;
        const std::uint32_t word = g.actors.records[suit].item;
        const int line = itemSuitLine(word);
        int words = -1;
        if (line >= 0 && line < static_cast<int>(g.view.sentenceCount))
            words = g.view.sentences[line].wordCount;
        std::printf("test_incident: c1a0d's suit says line %d (%d words)\n", line, words);
        check(line >= kSuitLines && !(word & kItemSuitShort) && words == 19,
              "... cooked with its whole logon: !HEV_AAx's ten words, and a pause at each comma");
        g.drain();
        g.player.inv.have &= ~kHaveSuit;
        const bool taken = itemPickUp(g.logic, g.player, suit);
        int said = 0, others = 0;
        for (int i = 0; i < g.heard.count; ++i)
        {
            const SoundEvent &e = g.heard.events[i];
            if ((e.flags & kSoundSentence) && e.sound == line && e.volume == 32)
                ++said;
            else if (e.sound == static_cast<std::uint16_t>(CommonSound::GunPickup))
                ++others;
        }
        check(taken && (g.player.inv.have & kHaveSuit) && said == 1 && others == 0,
              "taken, it says its logon at the suit's volume, and nothing is picked up to be heard");
        check(g.player.suit.value.waitMs >= 30000, "... and the suit says nothing else until that is said");
        g.drain();
    }

    /// c1a0d: those sitting are on their seats (func_walls, at -252), not on
    /// the floor under them (-288) with their legs through it; and the suit
    /// is down on the floor of its case (-280), too low a place for the
    /// player's box -- the game drops both in a small box, onto brushes too.
    void testPlacedOnWhatIsUnder(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a0d");
        if (game == nullptr)
            return;
        Game &g = *game;
        int sitters = 0, seated = 0;
        bool suit = false;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &a = g.actors.states[i];
            const int z = fxToInt(a.pos[2]);
            if (a.kind == static_cast<std::uint8_t>(ActorKind::Sitter))
            {
                ++sitters;
                seated += z >= -254 && z <= -250;
                std::printf("test_incident: c1a0d's sitter %d at z %d\n", i, z);
            }
            if (a.kind == static_cast<std::uint8_t>(ActorKind::Item) && fxToInt(a.pos[0]) == -3648 &&
                fxToInt(a.pos[1]) == 960)
            {
                std::printf("test_incident: c1a0d's suit at z %d\n", z);
                suit = z >= -282 && z <= -278;
            }
        }
        check(sitters == 2 && seated == 2, "c1a0d's two who sit are on their seats");
        check(suit, "... and the suit is on the floor of its case");
        testSuitLogon(g);
        // The scientist at the cart walks to it across a floor of grates
        // (func_walls over a pit): he stays on them, not in the pit under
        // them with his head at the player's feet.
        const std::uint32_t hash = nameHash("cartguy");
        int cart = -1;
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].name != 0 && g.view.nameHashes[g.actors.states[i].name] == hash)
                cart = i;
        }
        check(cart >= 0, "c1a0d has the scientist at the cart");
        if (cart < 0)
            return;
        // (His trigger: walked into by the player.)
        logicFire(g.logic, g.player, static_cast<std::uint16_t>(nameOf(g, "cartstudy")), -1, kUseOn, 0);
        int lowest = 0;
        for (int n = 0; n < 30 * 40; ++n)
        {
            g.frame();
            const int zNow = fxToInt(g.actors.states[cart].pos[2]);
            lowest = std::min(lowest, zNow);
        }
        const ActorState &a = g.actors.states[cart];
        std::printf("test_incident: c1a0d's cart scientist at %d %d %d, lowest %d\n", fxToInt(a.pos[0]), fxToInt(a.pos[1]),
                    fxToInt(a.pos[2]), lowest);
        check(lowest >= -290, "... and the scientist at the cart walks on its grates, not under them");
    }

    /// The microwave of c1a0d: five presses, and it goes.
    void testMicrowave(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a0d");
        check(game != nullptr, "the microwave's map cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const LevelView &v = g.view;
        for (int n = 0; n < 30; ++n)
            g.frame();
        // Its buttons: each fires the multisource that is the next one's
        // master (beep1ms .. beep5ms); the first has none.
        int marks = 0;
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
            marks += v.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Mark);
        check(marks >= 9, "the marks it leaves are in the level, waiting");
        auto live = [&]()
        {
            int count = 0;
            for (const Decal &decal : g.actors.decals)
                count += decal.active;
            return count;
        };
        const int before = live();
        int presses = 0;
        bool broke = false;
        const int sentences = g.sentencesHeard;
        // Pressed as the player presses: USE from in front of it, which finds
        // whatever it finds. The five are one box, each locked until the one
        // before it fires -- the first of them in the map's order is the one
        // to take, and once pressed it has gone into the wall.
        const fx at[3] = {fxFromInt(-2158), fxFromInt(1210), fxFromInt(-238)};
        const fx eye[3] = {fxFromInt(-2190), fxFromInt(1178), fxFromInt(-230)};
        fx forward[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
        const fx length = fxLength(forward);
        for (fx &k : forward)
            k = static_cast<fx>((static_cast<std::int64_t>(k) << kFxShift) / length);
        for (int step = 1; step <= 5; ++step)
        {
            char name[16];
            std::snprintf(name, sizeof(name), "beep%dms", step);
            const int target = nameOf(g, name);
            const int button = logicFindUsable(g.logic, eye, forward);
            if (button < 0 || v.logic[button].kind != static_cast<std::uint8_t>(LogicKind::Button) ||
                v.logic[button].target != target || target == 0)
            {
                std::printf("test_incident: press %d does not find the button that fires %s\n", step, name);
                continue;
            }
            ++presses;
            logicUse(g.logic, g.player, button, -1, kUseSet);
            for (int n = 0; n < 60; ++n)
            {
                logicTick(g.logic, g.player, 33);
                broke = broke || g.logic.brokeCount > 0;
                g.logic.brokeCount = 0;
                g.drain();
            }
        }
        std::printf("test_incident: the microwave: %d of 5 buttons pressed, %d marks left, %s, %d sentences\n", presses,
                    live() - before, broke ? "the stew burst" : "nothing burst", g.sentencesHeard - sentences);
        check(presses == 5, "its five buttons are pressed in turn, from where the player stands");
        check(g.sentencesHeard > sentences, "... and the scientist by it says what he says");
        check(live() - before >= 3, "after the fifth press its marks are on the wall");
        check(broke, "... and what was in it has burst");
    }
    // c1a1's lift that falls: three scientists stand in its cab on a floor
    // that cannot be seen and breaks when the call button is pressed; they
    // fall the length of the shaft, and what is at the bottom of it kills
    // them. (They used to be left standing in the air beside the ladder.)
    void testFallingLift(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a1");
        check(game != nullptr, "c1a1 cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        int riders[3] = {-1, -1, -1}, found = 0;
        for (int i = 0; i < g.actors.count && found < 3; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Scientist) &&
                std::abs(fxToInt(actor.pos[0]) - 728) < 80 && std::abs(fxToInt(actor.pos[1]) - 2112) < 80 &&
                fxToInt(actor.pos[2]) > 400)
                riders[found++] = i;
        }
        check(found == 3, "three scientists are in the cab at the top of the shaft");
        const int manager = nameOf(g, "dethvmm");
        check(manager != 0, "the button's chain is in the level");
        if (found != 3 || manager == 0)
            return;
        // (The player well out of it, on the top floor.)
        g.player.origin[0] = fxFromInt(1120);
        g.player.origin[1] = fxFromInt(2112);
        g.player.origin[2] = fxFromInt(836);
        for (int n = 0; n < 30; ++n)
            g.frame();
        bool stood = true;
        int floorZ[3];
        for (int r = 0; r < 3; ++r)
        {
            const ActorState &actor = g.actors.states[riders[r]];
            floorZ[r] = fxToInt(actor.pos[2]);
            stood = stood && actor.groundMover >= 0 && !actor.airborne && floorZ[r] >= 570 && floorZ[r] <= 576;
        }
        std::printf("test_incident: the lift's three stand at %d, %d, %d\n", floorZ[0], floorZ[1], floorZ[2]);
        check(stood, "before the button they stand on the cab's floor, which is a brush");
        logicFire(g.logic, g.player, static_cast<std::uint16_t>(manager), -1, kUseToggle, 0);
        int fell = -1, gone = -1;
        for (int n = 0; n < 30 * 6 && gone < 0; ++n)
        {
            g.frame();
            bool allLow = true, allDead = true;
            for (int r = 0; r < 3; ++r)
            {
                const ActorState &actor = g.actors.states[riders[r]];
                allLow = allLow && (!actor.active || fxToInt(actor.pos[2]) < 0);
                allDead = allDead && (!actor.active || actor.ai >= kAiDying);
            }
            if (allLow && fell < 0)
                fell = n;
            if (allDead)
                gone = n;
        }
        std::printf("test_incident: they are past the bottom floor %.1f s after the button, and dead at %.1f s\n",
                    fell / 30.0, gone / 30.0);
        check(fell > 0 && fell < 75, "the floor gone, they fall down the shaft");
        check(gone > 0 && gone < 105, "... and do not live through what is at the bottom");
    }
    // c1a1b's valve: a wheel that turns when it is used (a func_rot_button)
    // and then lets the water in -- a train of water that slides up the
    // tunnels, a gate that rises, a current switched on. (The wheel used to
    // do nothing; the water, had it moved, was not water; the current was
    // not in the level at all.)
    void testValve(const char *dir)
    {
        const std::unique_ptr<Game> game = load(dir, "c1a1b");
        check(game != nullptr, "c1a1b cooks");
        if (game == nullptr)
            return;
        Game &g = *game;
        const LevelView &v = g.view;
        int wheel = -1, water = -1, gate = -1, pushes = 0, push = -1;
        for (std::uint32_t i = 0; i < v.logicCount; ++i)
        {
            const LogicRecord &record = v.logic[i];
            const auto kind = static_cast<LogicKind>(record.kind);
            if (kind == LogicKind::Button && record.target == nameOf(g, "water_doormm"))
                wheel = static_cast<int>(i);
            if (kind == LogicKind::Train && record.name == nameOf(g, "water_door_move3"))
                water = static_cast<int>(i);
            if (kind == LogicKind::Door && record.name == nameOf(g, "water_door"))
                gate = static_cast<int>(i);
            if (kind == LogicKind::TriggerPush && record.name == nameOf(g, "water_door_current"))
            {
                ++pushes;
                push = static_cast<int>(i);
            }
        }
        check(wheel >= 0 && (v.logic[wheel].flags & kLogicTurns), "the valve's wheel is in the level, as a button that turns");
        check(water >= 0 && v.logic[water].brush != kNoBrush && v.brushes[v.logic[water].brush].kind == static_cast<std::uint8_t>(static_cast<std::int8_t>(kContentsWater)),
              "the train the wheel sets moving is water");
        check(gate >= 0 && pushes == 6, "the gate and the six parts of the current are there");
        if (wheel < 0 || water < 0 || gate < 0 || push < 0)
            return;
        // Stood at the wheel: dry, and the current not yet running.
        auto stand = [&]()
        {
            g.player.origin[0] = fxFromInt(2380);
            g.player.origin[1] = fxFromInt(-1161);
            g.player.origin[2] = fxFromInt(-382);
            for (fx &c : g.player.velocity)
                c = 0;
        };
        stand();
        for (int n = 0; n < 10; ++n)
            g.frame();
        const BrushState &block = g.brushStates[v.logic[water].brush];
        const BrushState &disc = g.brushStates[v.logic[wheel].brush];
        const fx waterZ = block.pos[2];
        check(g.player.waterLevel <= 1 && g.states[push].running, "before: no more than ankle deep, and no current");
        // USE finds the wheel from there.
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        const fx ahead[3] = {kFxOne, 0, 0};
        check(logicFindUsable(g.logic, eye, ahead) == wheel, "USE, stood before it, is the wheel");
        logicUse(g.logic, g.player, wheel, -1, kUseToggle);
        // It turns a quarter in under two seconds, and only then lets go.
        int turned = -1;
        for (int n = 0; n < 90 && turned < 0; ++n)
        {
            stand();
            g.frame();
            if (g.states[wheel].move == MoveState::AtTop)
                turned = n;
        }
        std::uint16_t most = 0;
        for (int a = 0; a < 3; ++a)
            most = std::max(most, disc.angles[a]);
        std::printf("test_incident: the wheel is round %.1f s after it is used, turned %d degrees\n", turned / 30.0,
                    static_cast<int>(most) * 360 / 65536);
        check(turned >= 48 && turned <= 60 && most > 16000 && most < 16800, "the wheel turns its quarter in 1.8 seconds");
        // Then the water comes: up the tunnel, over the player's head.
        int deep = -1;
        for (int n = 0; n < 300 && deep < 0; ++n)
        {
            stand();
            g.frame();
            if (g.player.waterLevel == 3)
                deep = n;
        }
        for (int n = 0; n < 240; ++n)
        {
            stand();
            g.frame();
        }
        std::printf("test_incident: the water is over the player's head %.1f s later; it has risen %d units, come %d along\n",
                    deep / 30.0, fxToInt(block.pos[2] - waterZ), fxToInt(block.pos[0]) - 2676);
        check(deep > 0, "the water rises over whoever stands at the wheel");
        check(fxToInt(block.pos[2] - waterZ) == 165 && fxToInt(block.pos[0]) == 1776, "... all the way to the end of its path");
        check(g.states[gate].move != MoveState::AtBottom, "the gate has risen");
        check(!g.states[push].running, "the current is running");
        // ... and carries whoever is in it.
        const LogicRecord &current = v.logic[push];
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = fxFromInt((current.mins[a] + current.maxs[a]) / 2);
        const fx before[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        // (The current is what they are carried by in their own move, as
        // the game's base velocity: the logic says it, the move does it.)
        for (int n = 0; n < 15; ++n)
        {
            logicTick(g.logic, g.player, 33);
            playerMove(g.player, MoveCommand(), g.move, kPlayerTick);
        }
        const fx moved[3] = {g.player.origin[0] - before[0], g.player.origin[1] - before[1], 0};
        const std::int64_t along = static_cast<std::int64_t>(moved[0]) * current.move[0] +
                                   static_cast<std::int64_t>(moved[1]) * current.move[1];
        std::printf("test_incident: half a second in the current moves them %d units\n", fxToInt(fxLength(moved)));
        check(fxLength(moved) > fxFromInt(10) && along > 0, "whoever is in the current is carried the way it runs");
    }
} // namespace

namespace
{
    int actorNamed(const Game &g, const char *name)
    {
        const std::uint32_t hash = nameHash(name);
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].nameHash == hash)
                return i;
        }
        return -1;
    }

    /// c1a2b's scenes end as the game's do: the zombie that bursts through
    /// the window lands on the floor below it; the one that breaks the wall
    /// comes for the player; the scientist hanging in the shaft falls, and is
    /// a body at the end of it.
    void testScenes(const char *dir)
    {
        auto g = load(dir, "c1a2b");
        check(g != nullptr, "c1a2b loads");
        if (g == nullptr)
            return;
        const int jumper = actorNamed(*g, "jumperz"), breakout = actorNamed(*g, "breakout"),
                  cliff = actorNamed(*g, "cliff");
        check(jumper >= 0 && breakout >= 0 && cliff >= 0, "its zombies and the dangling scientist are there");
        if (jumper < 0 || breakout < 0 || cliff < 0)
            return;
        // The player in the room the window and the wall give onto.
        g->player.origin[0] = fxFromInt(590);
        g->player.origin[1] = fxFromInt(-1634);
        g->player.origin[2] = fxFromInt(420);
        for (int f = 0; f < 30; ++f)
            g->frame();
        logicEnqueue(g->logic, static_cast<std::uint16_t>(nameOf(*g, "zombie")), 0, kUseToggle, -1, 0);
        logicEnqueue(g->logic, static_cast<std::uint16_t>(nameOf(*g, "dangler")), 0, kUseToggle, -1, 0);
        bool rose = false;
        for (int f = 0; f < 12 * 30; ++f)
        {
            g->frame();
            g->player.health = 100; // (not the point here)
            // (The player firing now and then: the game's monsters come out
            // of a scene idle, and go for what they see or hear.)
            if (f % 30 == 0)
                aiWeaponVolume(g->logic, kNormalGunVolume);
            rose = rose || g->actors.states[breakout].ai != kAiIdle;
            if (std::getenv("HLDS_AI_TRACE") != nullptr && f % 15 == 0)
            {
                const ActorState &b = g->actors.states[breakout];
                const AiMonster *m = aiFind(g->actors, breakout);
                std::printf("  breakout f=%d ai=%d script=%d phase=%d st=%d sched=%s enemy=%d pos=%d %d %d yaw=%u leaf=%d pleaf=%d\n", f, b.ai, b.script,
                            b.scriptPhase, m ? m->state : -1, m && m->schedule ? m->schedule->name : "-", m ? m->enemy : -9,
                            fxToInt(b.pos[0]), fxToInt(b.pos[1]), fxToInt(b.pos[2]), b.yaw, m ? m->leaf : -1, g->ai.playerLeaf);
            }
        }
        const ActorState &j = g->actors.states[jumper];
        check(!j.airborne && fxToInt(j.pos[2]) > 370 && fxToInt(j.pos[2]) < 400,
              "the zombie through the window is on the floor below it, not in the air");
        check(rose, "the zombie through the wall comes out for the player");
        const ActorState &c = g->actors.states[cliff];
        check(c.ai == kAiDead && c.frozen, "the scientist in the shaft falls, and is a body where he fell");
    }

    /// c1a3's silo: the scientist who beats on the glass runs off through a
    /// tripmine's beam, and it blows him apart.
    void testTripmine(const char *dir)
    {
        auto g = load(dir, "c1a3");
        check(g != nullptr, "c1a3 loads");
        if (g == nullptr)
            return;
        const int glassman = actorNamed(*g, "glassman");
        check(glassman >= 0, "the scientist behind the glass is there");
        if (glassman < 0)
            return;
        // (The player well away, where nothing reaches them.)
        g->player.origin[0] = fxFromInt(422);
        g->player.origin[1] = fxFromInt(1400);
        g->player.origin[2] = fxFromInt(40);
        for (int f = 0; f < 30; ++f)
            g->frame();
        logicEnqueue(g->logic, static_cast<std::uint16_t>(nameOf(*g, "beatglass")), 0, kUseToggle, -1, 0);
        bool gone = false;
        for (int f = 0; f < 40 * 30 && !gone; ++f)
        {
            g->frame();
            g->player.health = 100;
            const ActorState &a = g->actors.states[glassman];
            gone = !a.active || a.ai >= kAiDying;
        }
        check(gone, "running off, he sets off the tripmine and it kills him");
    }
} // namespace

int main()
{
    testStyles();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testTramDoor(dir);
        testHinged(dir);
        testLift(dir);
        testFallingLift(dir);
        testValve(dir);
        testLights(dir);
        testMicrowave(dir);
        testPlacedOnWhatIsUnder(dir);
        testHazardLamps(dir);
        testWheelBridge(dir);
        testTargetFace(dir);
        testSigns(dir);
        testScenes(dir);
        testTripmine(dir);
    }
    else
    {
        std::printf("test_incident: HLDS_VALVE_DIR not set, the game's maps were not read\n");
    }
    if (g_failures == 0)
        std::printf("test_incident: ok\n");
    return g_failures == 0 ? 0 : 1;
}
