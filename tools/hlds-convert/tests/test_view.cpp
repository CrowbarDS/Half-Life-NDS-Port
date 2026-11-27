// What moves the view and what being hurt shows (shared/HlView.hpp): the
// kick that dies away, the bob and the lean of a walk, the cost and the jolt
// of a fall, the marks of a blow, the camera's fall at death; a fall in a room
// made here, and where rounds land in another. With HLDS_VALVE_DIR set, a
// real fall in a real map, and rounds on its faces.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "HlMarkPlan.hpp"
#include "HlSharedRandom.hpp"
#include "HlView.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    void testKick()
    {
        ViewState view;
        viewKick(view, 10 * kViewDegree);
        check(view.punch[0] == 10 * kViewDegree && viewTurns(view.punch[0]) > 1800 && viewTurns(view.punch[0]) < 1840,
              "a kick lifts the view by what it says (ten degrees is a 36th of a turn)");
        int ms = 0;
        fx last = view.punch[0];
        bool falling = true;
        while (view.punch[0] > 0 && ms < 5000)
        {
            viewTick(view, 16, 0, 0, true, false);
            falling = falling && view.punch[0] <= last;
            last = view.punch[0];
            ms += 16;
        }
        check(falling && view.punch[0] == 0 && ms > 300 && ms < 1200, "... and dies away, in well under a second or so");
        for (int n = 0; n < 20; ++n)
            viewKick(view, 10 * kViewDegree, 0, -10 * kViewDegree);
        check(view.punch[0] == 30 * kViewDegree && view.punch[2] == -30 * kViewDegree, "kicks add up, to no more than a neck allows");
        check(weaponKick(2, 0) == 2 * kViewDegree && weaponKick(3, 0) == 5 * kViewDegree && weaponKick(6, 0) == 10 * kViewDegree &&
                  weaponKick(1, 0) == 0 && weaponKick(5, 0) == -2 * kViewDegree && weaponKick(5, 4 * 4096) == 2 * kViewDegree,
              "each weapon kicks as the game's does: the .357 hardest, the MP5 either way, the crowbar not at all");
        check(weaponKick(14, 0) == 0 && weaponKick(14, 1) == -kViewDegree && weaponKick(14, 2) == -2 * kViewDegree &&
                  weaponKick(12, 0) == 2 * kViewDegree,
              "... the hornetgun none to two degrees down, the gauss two up");
    }

    void testWalk()
    {
        ViewState view;
        fx low = 0, high = 0;
        for (int ms = 0; ms < 1600; ms += 16)
        {
            viewTick(view, 16, fxFromInt(320), 0, true, false);
            low = view.bob < low ? view.bob : low;
            high = view.bob > high ? view.bob : high;
        }
        // (0.01 of 320 units a second: 3.2 at the top of the step.)
        check(high > fxFromInt(3) && high <= fxFromInt(4) && low < 0 && low > -fxFromInt(2),
              "at a run the eye rises and falls a few units with each step");
        for (int ms = 0; ms < 400; ms += 16)
            viewTick(view, 16, 0, 0, true, false);
        check(view.bob == 0, "standing still it does not");
        viewTick(view, 16, fxFromInt(200), fxFromInt(200), true, false);
        const fx right = view.roll;
        viewTick(view, 16, fxFromInt(200), -fxFromInt(100), true, false);
        const fx left = view.roll;
        viewTick(view, 16, fxFromInt(600), fxFromInt(600), true, false);
        check(right == 2 * kViewDegree && left == -kViewDegree && view.roll == 2 * kViewDegree,
              "a sidestep leans the view into it: two degrees at 200 units a second, and no more");
        view.walks = false;
        viewTick(view, 16, fxFromInt(320), fxFromInt(200), true, false);
        check(view.bob == 0 && view.roll == 0, "none of it, for whoever has switched it off");
    }

    void testFall()
    {
        check(fallDamage(300) == 0 && fallDamage(580) == 0 && fallDamage(802) == 50 && fallDamage(1024) == 100 &&
                  fallDamage(1200) > 100,
              "a fall is safe to 580 units a second and fatal at 1024, in a straight line between");
        ViewState view;
        viewLand(view, 200);
        const bool soft = view.punch[0] == 0;
        viewLand(view, 500);
        const fx dip = view.punch[0];
        viewLand(view, 5000);
        check(soft && dip < -5 * kViewDegree && dip > -8 * kViewDegree && view.punch[0] >= -30 * kViewDegree,
              "a hard landing dips the view; a step down does not");
    }

    void testHurt()
    {
        ViewState view;
        viewHurt(view, 10, fxFromInt(100), 0); // from in front
        check(view.flash > 0 && view.pain[0] == 256 && view.pain[1] == 0 && view.pain[2] == 0 && view.pain[3] == 0 &&
                  view.punch[0] > 0,
              "a blow from in front reddens the screen, marks the front and knocks the head back");
        ViewState side;
        viewHurt(side, 10, 0, -fxFromInt(50)); // from the left
        check(side.pain[3] == 256 && side.pain[0] == 0 && side.pain[1] == 0 && side.punch[2] > 0,
              "one from the left marks the left, and tips the head from it");
        ViewState corner;
        viewHurt(corner, 10, -fxFromInt(50), fxFromInt(50)); // behind and to the right
        check(corner.pain[2] == 256 && corner.pain[1] == 256 && corner.pain[0] == 0 && corner.pain[3] == 0,
              "one from behind and to the right marks both");
        ViewState all;
        viewHurt(all, 30, 0, 0);
        check(all.pain[0] == 256 && all.pain[1] == 256 && all.pain[2] == 256 && all.pain[3] == 256,
              "what comes from nowhere in particular marks every side");
        const int red = all.flash;
        for (int ms = 0; ms < 1000; ms += 16)
            viewTick(all, 16, 0, 0, true, false);
        check(red > 150 && all.flash == 0 && all.pain[0] == 0, "the red and the marks are gone within a second");
        viewHurt(all, 0, 1, 1);
        check(all.flash == 0, "nothing, for no hurt");
    }

    void testDeath()
    {
        ViewState view;
        for (int ms = 0; ms < 500; ms += 16)
            viewTick(view, 16, fxFromInt(100), 0, true, true);
        const int half = deathSink(view.deadMs);
        const fx tilted = deathRoll(view.deadMs);
        for (int ms = 0; ms < 3000; ms += 16)
            viewTick(view, 16, 0, 0, true, true);
        check(half > 100 && half < 200 && tilted > 20 * kViewDegree && tilted < 60 * kViewDegree &&
                  deathSink(view.deadMs) == 256 && deathRoll(view.deadMs) == 80 * kViewDegree && view.flash >= 100 &&
                  view.deadMs >= kDeathWaitMs && view.bob == 0,
              "dead, the camera sinks to the floor and rolls over, and the screen stays red");
        viewTick(view, 16, 0, 0, true, false);
        check(view.deadMs == 0 && deathSink(0) == 0 && deathRoll(0) == 0, "alive again, it is as it was");
    }

    /// A fall, wherever: off something high, the landing is told with how
    /// fast it was.
    void landing(Game &g)
    {
        for (int n = 0; n < 30; ++n)
            g.frame();
        const bool stood = g.player.onGround && g.player.landed == 0;
        // Lifted sixty units (under the ceiling) and let go.
        g.player.origin[2] += fxFromInt(60);
        g.player.onGround = false;
        fx landed = 0;
        int ticks = 0;
        for (int n = 0; n < 120 && landed == 0; ++n)
        {
            g.frame();
            landed = g.player.landed;
            ++ticks;
        }
        g.frame();
        std::printf("test_view: a drop of 60 units lands at %d units a second after %d ticks\n", fxToInt(landed), ticks);
        // (v = sqrt(2 * 800 * 60) = 310.)
        check(stood && fxToInt(landed) > 260 && fxToInt(landed) < 340 && g.player.landed == 0 && g.player.onGround,
              "a drop is told once, as it lands, with how fast it was coming down");
        check(fallDamage(fxToInt(landed)) == 0, "... and one of 60 units costs nothing, as in the game");
    }

    /// The fall on a room of our own, and a longer one, which costs.
    void testRoomLanding()
    {
        BoxRoom room;
        room.maxs[2] = 1024;
        auto game = std::make_unique<Game>();
        std::string error;
        if (game->bsp.parse(buildBoxRoom(room), error))
            finish(*game, nullptr);
        check(game->ok, "the tall room cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        landing(g);
        // (v = sqrt(2 * 800 * 400) = 800, well past what is safe.)
        g.player.origin[2] += fxFromInt(400);
        g.player.onGround = false;
        fx landed = 0;
        for (int n = 0; n < 120 && landed == 0; ++n)
        {
            g.frame();
            landed = g.player.landed;
        }
        check(fxToInt(landed) > 740 && fxToInt(landed) < 860 && fallDamage(fxToInt(landed)) > 0,
              "a drop of 400 lands the faster for it, and costs");
    }

    /// A real fall: off something high in a real map.
    void testLanding(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a0.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c1a0.bsp loads").c_str());
            return;
        }
        finish(*game, nullptr, dir);
        check(game->ok, "the map cooks");
        if (!game->ok)
            return;
        landing(*game);
    }
    /// Marks and pieces, on a real map: a shot leaves its hole and dust
    /// where it lands, blood stains what is behind, a blast scorches the
    /// floor; something broken throws pieces of itself, which fall and lie
    /// and are gone.
    void testMarks(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a0.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c1a0.bsp loads").c_str());
            return;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return;
        Game &g = *game;
        for (int n = 0; n < 30; ++n)
            g.frame();
        ActorWorld &a = g.actors;
        auto marks = [&]()
        {
            int count = 0;
            for (const Decal &decal : a.decals)
                count += decal.active;
            return count;
        };
        auto flying = [&](std::uint8_t kind)
        {
            int count = 0;
            for (const Piece &piece : a.pieces)
                count += piece.kind == kind;
            return count;
        };
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + fxFromInt(28)};
        const fx down[3] = {eye[0] + fxFromInt(40), eye[1], eye[2] - fxFromInt(400)};
        check(marks() == 0 && markShot(a, g.move, eye, down, false) && marks() == 1 && flying(kPieceSpeck) == 3,
              "a shot that lands leaves a hole, and three specks of dust in the air");
        const Decal &hole = a.decals[0];
        check(hole.picture < kDecalShots && hole.normal[2] > 3900 && hole.at[2] < eye[2] && hole.brush == -1 &&
                  hole.colour == kMarkShot,
              "... a bullet's hole, on the floor it hit, facing up");
        const fx nowhere[3] = {eye[0], eye[1], eye[2] - fxFromInt(2)};
        check(!markShot(a, g.move, eye, nowhere, false) && marks() == 1, "one that lands on nothing leaves nothing");
        check(markShot(a, g.move, eye, down, true) && a.decals[1].picture == kDecalDent, "a blow leaves its dent");
        const fx along[3] = {0, 0, -kFxOne};
        const fx wound[3] = {eye[0], eye[1], eye[2] - fxFromInt(20)};
        check(markBlood(a, g.move, wound, along, false, false) && a.decals[2].colour == kMarkBlood &&
                  a.decals[2].picture >= kDecalBlood && a.decals[2].picture < kDecalBlood + kDecalBloods,
              "blood from a wound stains the floor behind it");
        const int small = a.decals[2].half;
        check(markBlood(a, g.move, wound, along, true, true) && a.decals[3].colour == kMarkAlien &&
                  a.decals[3].half > small - 6 && (a.decals[3].picture == kDecalAlien || a.decals[3].picture == kDecalAlien2),
              "an alien's is its own colour");
        const fx far[3] = {eye[0], eye[1], eye[2] + fxFromInt(4)};
        const fx up[3] = {0, 0, 0};
        (void)far;
        (void)up;
        const fx feet[3] = {eye[0] + fxFromInt(30), eye[1], g.player.origin[2] - fxFromInt(30)};
        markBlast(a, g.move, feet, 48);
        check(marks() == 5 && a.decals[4].picture == kDecalScorch && a.decals[4].half >= 32 && a.decals[4].normal[2] > 3900,
              "a blast scorches the floor under it");
        // A brush met at a slant: the mark is where the line meets its face
        // (c1a0's pane, x 226 to 232, y -1154 to -1054), not where its clip
        // hull met it brought back onto that face's plane (16 units aside).
        {
            const fx from[3] = {fxFromInt(150), fxFromInt(-1150), fxFromInt(-150)};
            const fx to[3] = {fxFromInt(300), fxFromInt(-1000), fxFromInt(-150)};
            fx at[3], normal[3];
            int on = -1;
            const bool met = traceShotWall(g.move, from, to, at, normal, &on, true);
            std::printf("  slanted pane: met %d on %d at %.2f %.2f %.2f normal %d %d %d\n", met, on,
                        at[0] / 4096.0, at[1] / 4096.0, at[2] / 4096.0, normal[0], normal[1], normal[2]);
            check(met && on >= 0 && normal[0] < -4000 && std::abs(at[0] - fxFromInt(226)) <= kFxOne / 4 &&
                      std::abs(at[1] - fxFromInt(-1074)) <= kFxOne / 4,
                  "a mark on a brush met at a slant is where the line meets its face");
            // Broken, the pane takes its marks with it, at once: none is
            // left hanging where it was.
            int record = -1;
            for (int i = 0; met && on >= 0 && i < g.logic.recordCount; ++i)
                record = g.logic.records[i].brush == on && hldata::detail::isBreakable(g.logic.records[i]) ? i : record;
            check(record >= 0, "the pane is a breakable");
            if (record >= 0)
            {
                markOn(a, g.move, on, kDecalShot, at, normal, 8, kMarkShot);
                int onPane = 0;
                for (const Decal &decal : a.decals)
                    onPane += decal.active && decal.brush == on;
                hldata::detail::breakIt(g.logic, g.player, record, 0);
                int after = 0;
                for (const Decal &decal : a.decals)
                    after += decal.active && decal.brush == on;
                check(onPane == 1 && after == 0, "a pane that breaks takes its marks with it at once");
                const bool again = traceShotWall(g.move, from, to, at, normal, &on, true);
                check(!again || on < 0, "... and is not met again");
            }
        }
        // One spot, hit again and again, is one mark: not a pile of them.
        const int before = marks();
        for (int n = 0; n < 20; ++n)
            markShot(a, g.move, eye, down, false);
        check(marks() == before, "twenty shots into one spot leave the one hole that was there");
        for (int n = 0; n < 20; ++n)
            markShot(a, g.move, eye, down, true);
        check(marks() == before, "... and twenty blows the one dent");
        // Apart, they are apart.
        const fx aside[3] = {down[0] + fxFromInt(120), down[1], down[2]};
        check(markShot(a, g.move, eye, aside, false) && marks() == before + 1, "a hole a step away is another hole");
        // A pane has one crack, wherever it is hit.
        {
            const fx up[3] = {0, 0, kFxOne};
            const fx here[3] = {fxFromInt(10), 0, 0}, there[3] = {fxFromInt(60), fxFromInt(30), 0};
            const int had = marks();
            decalAdd(a, kDecalCrack, here, up, 20, kMarkCrack, 3);
            const Decal &crack = decalAdd(a, kDecalCrack, there, up, 20, kMarkCrack, 3);
            decalAdd(a, kDecalCrack, here, up, 20, kMarkCrack, 4);
            check(marks() == had + 2 && crack.at[0] == there[0] && crack.brush == 3,
                  "a pane has one crack, moved to where it was last hit; another pane has its own");
            for (Decal &decal : a.decals)
                decal.active = decal.active && decal.brush < 0;
        }
        // The ring: the oldest gives way.
        a.decalLimit = 6;
        for (int n = 0; n < 8; ++n)
        {
            const fx spread[3] = {down[0] + fxFromInt(200 + 60 * n), down[1] + fxFromInt(40 * n), down[2]};
            markShot(a, g.move, eye, spread, false);
        }
        check(marks() == 6, "when there is no room for another mark, one gives way");
        // What gives way is what is least worth keeping: a burst of holes
        // takes the room of other holes, never of the blood or the scorch
        // (a DS's 32 places).
        {
            for (Decal &decal : a.decals)
                decal.active = false;
            a.decalLimit = 32;
            const fx up[3] = {0, 0, kFxOne};
            const fx poolAt[3] = {fxFromInt(100), fxFromInt(100), 0}, otherPool[3] = {fxFromInt(140), fxFromInt(100), 0},
                     scorchAt[3] = {fxFromInt(-100), fxFromInt(-100), 0}, mapAt[3] = {fxFromInt(0), fxFromInt(300), 0};
            decalAdd(a, kDecalBlood, poolAt, up, 24, kMarkBlood);
            decalAdd(a, kDecalBlood + 1, otherPool, up, 24, kMarkBlood);
            decalAdd(a, kDecalScorch, scorchAt, up, 40, kMarkSoot);
            decalAdd(a, kDecalPool, mapAt, up, 16, kMarkBlood, -1, true);
            check(marks() == 4, "two pools forty units apart are two stains, not one moved");
            for (int n = 0; n < 40; ++n)
            {
                const fx hole[3] = {fxFromInt(400 + 30 * n), fxFromInt(-400), 0};
                decalAdd(a, kDecalShot, hole, up, 8, kMarkShot);
            }
            int pools = 0, scorches = 0, maps = 0;
            for (const Decal &decal : a.decals)
            {
                pools += decal.active && decalFamily(decal.picture) == 5 && !(decal.turn & kDecalTheMaps);
                scorches += decal.active && decal.picture == kDecalScorch;
                maps += decal.active && (decal.turn & kDecalTheMaps) != 0;
            }
            check(marks() == 32 && pools == 2 && scorches == 1 && maps == 1,
                  "forty holes on a DS's thirty-two places: the blood, the scorch and the map's own stain stay");
            // ... and a stain landing on top of one is that one.
            const fx onTop[3] = {fxFromInt(104), fxFromInt(100), 0};
            const int had = marks();
            decalAdd(a, kDecalBlood + 2, onTop, up, 24, kMarkBlood);
            check(marks() == had, "blood on top of blood is the one stain");
            a.decalLimit = 6;
        }
        // They go: holes after a third of a minute, blood and soot after
        // two thirds -- twice that where they are kept longer.
        for (Decal &decal : a.decals)
            decal.active = false;
        a.decalNext = 0;
        markShot(a, g.move, eye, down, false);
        markBlood(a, g.move, wound, along, false, true);
        const int both = decalsTick(a, 19000);
        const int stain = decalsTick(a, 2000);
        const int gone = decalsTick(a, 20000);
        a.markLifePercent = 200;
        markShot(a, g.move, eye, down, false);
        const int kept = decalsTick(a, 30000);
        const int later = decalsTick(a, 11000);
        a.markLifePercent = 100;
        check(both == 2 && stain == 1 && gone == 0 && kept == 1 && later == 0,
              "marks go: a hole after twenty seconds, blood after forty; twice as long where they are kept longer");
        // Specks are gone in half a second.
        for (int n = 0; n < 20; ++n)
            g.frame();
        check(flying(kPieceSpeck) == 0, "the dust is gone in half a second");

        // Something of glass, two units by three, breaks.
        const fx mins[3] = {eye[0] + fxFromInt(20), eye[1] - fxFromInt(48), eye[2] - fxFromInt(20)};
        const fx maxs[3] = {eye[0] + fxFromInt(24), eye[1] + fxFromInt(48), eye[2] + fxFromInt(60)};
        const std::uint16_t textures[2] = {3, 4};
        a.pieceLimit = kMaxPieces;
        const int glass = piecesBurst(a, mins, maxs, 0, textures, 2);
        bool own = true, inside = true;
        for (const Piece &piece : a.pieces)
        {
            if (piece.kind != kPieceShard)
                continue;
            own = own && (piece.texture == 3 || piece.texture == 4) && piece.alpha < 31;
            for (int k = 0; k < 3; ++k)
                inside = inside && piece.pos[k] >= mins[k] && piece.pos[k] <= maxs[k];
        }
        bool lasting = true;
        for (const Piece &piece : a.pieces)
            lasting = lasting && (piece.kind != kPieceShard || (piece.leftMs >= 2500 && piece.leftMs <= 3500));
        check(glass == (4 * 96 + 96 * 80 + 80 * 4) / 432 && flying(kPieceShard) == glass && own && inside,
              "broken glass throws a piece of itself for each 12 by 12 units of its sides, see-through, from where it was");
        check(lasting, "... each for two and a half seconds and up to one more");
        int lay = 0, soonest = 100000;
        for (int n = 0; n < 60; ++n)
        {
            g.frame();
            int lying = 0;
            for (const Piece &piece : a.pieces)
            {
                if (piece.kind == kPieceShard && pieceLanded(piece) >= 2 && piece.pos[2] < mins[2])
                {
                    ++lying;
                    soonest = piece.leftMs < soonest ? piece.leftMs : soonest;
                }
            }
            lay = lying > lay ? lying : lay;
        }
        check(lay >= glass / 3, "... which fall, and lie");
        check(soonest <= 500, "what lies is not left lying long");
        for (int n = 0; n < 46; ++n)
            g.frame();
        check(flying(kPieceShard) == 0, "... and all of it is gone within three and a half seconds");
        check(piecesBurst(a, mins, maxs, 2, nullptr, 0) == glass && breakStyle(5).weight < breakStyle(2).weight &&
                  breakStyle(2).weight == 128,
              "metal of the same size breaks into as many; the pieces fall at half the rate, ceiling tiles drift");
        // (Kept half as long again, where there is the room for it.)
        {
            const fx still[3] = {0, 0, 0};
            const int plain = pieceAdd(a, kPieceShell, eye, still, 1200, 0).leftMs;
            a.lifePercent = 150;
            const int longer = pieceAdd(a, kPieceShell, eye, still, 1200, 0).leftMs;
            a.lifePercent = 100;
            check(plain == 1200 && longer == 1800, "small things last half as long again where they are kept longer");
        }
        // A casing rings once as it lands.
        for (Piece &piece : a.pieces)
            piece.kind = 0;
        const fx toss[3] = {fxFromInt(40), 0, fxFromInt(60)};
        shellEject(a, kPieceShell, eye, toss);
        int rang = 0, rings = 0;
        for (int n = 0; n < 60; ++n)
        {
            const int now = piecesTick(a, g.move, 33);
            rang |= now;
            rings += now != 0 ? 1 : 0;
        }
        check(rang == 1 && rings == 1 && flying(kPieceShell) == 0,
              "a spent casing rings once as it lands, and is gone within two seconds");
        // ... and a shotgun's, with its own ring (TE_BOUNCE_SHOTSHELL).
        shellEject(a, kPieceShotShell, eye, toss);
        rang = 0;
        for (int n = 0; n < 60; ++n)
            rang |= piecesTick(a, g.move, 33);
        check(rang == 2 && flying(kPieceShotShell) == 0, "a shotgun's shell rings as a shotgun's");
    }
    /// Where a round lands, in a room of computers with a wooden crate:
    /// the sound of what it lands on, by the texture there -- the world's,
    /// or the brush entity's own -- quieter on what breaks; half the time a
    /// whine; a computer's sparks half the time; a shotgun's pellet with
    /// no sound of the wall; two impacts heard a tick at most; and a map
    /// cooked before those sounds were common has the first two whines.
    void testImpacts()
    {
        const MaterialBook book = parseMaterials("P TESTWALL\nW CRATEWOOD\n");
        check(materialOfLetter('F') == Material::Flesh && materialOfLetter('Y') == Material::Glass,
              "the game's letters, and a mod's flesh");
        check(commonsOf(72) == kCommonSoundsBefore73 && commonsOf(73) == kCommonSoundsBefore74 &&
                  commonsOf(74) == static_cast<std::uint16_t>(CommonSound::Count),
              "a map of version 72 has the common sounds before the new whines; of 73, before what breaks");
        // The table: each of a material's sounds can be drawn, and only those.
        {
            const MaterialHit &grate = materialHit(Material::Grate);
            bool seen[4] = {};
            for (std::uint32_t roll = 0; roll < 0x10000; roll += 97)
            {
                const int k = materialHitSound(grate, roll) - static_cast<int>(CommonSound::Grate1);
                if (k >= 0 && k < 4)
                    seen[k] = true;
            }
            check(seen[0] && !seen[1] && !seen[2] && seen[3], "a grate's two sounds are its first and its fourth");
            const MaterialHit &flesh = materialHit(Material::Flesh);
            check(materialHitSound(flesh, 0) == static_cast<std::uint16_t>(CommonSound::BulletFlesh) &&
                      materialHitSound(flesh, 0xFFFF) == static_cast<std::uint16_t>(CommonSound::BulletFlesh2) &&
                      flesh.loud == 127,
                  "flesh is one of the two bullet hits, at full");
        }
        BoxRoom room;
        BrushBox crate;
        crate.mins[0] = 40;
        crate.mins[1] = -16;
        crate.maxs[0] = 72;
        crate.maxs[1] = 16;
        crate.maxs[2] = 32;
        crate.className = "func_breakable";
        crate.keys = "\"material\" \"1\"\n\"health\" \"200\"\n";
        crate.textureName = "cratewood";
        room.brushes.push_back(crate);
        auto game = std::make_unique<Game>();
        game->materials = &book;
        std::string error;
        check(game->bsp.parse(buildBoxRoom(room), error), "the room of computers builds");
        finish(*game, nullptr);
        check(game->ok && game->view.brushCount == 1, "... and cooks, with its crate");
        if (!game->ok || game->view.brushCount != 1)
            return;
        Game &g = *game;
        auto named = [&](CommonSound c) { return g.view.soundThere(static_cast<int>(c)); };
        check(named(CommonSound::Spark5) && named(CommonSound::Spark6) && named(CommonSound::Ricochet5) &&
                  named(CommonSound::WoodHit1) && !named(CommonSound::BulletFlesh2) && !named(CommonSound::Metal1),
              "a map of computers keeps their sparks; no flesh, no metal");
        check(named(CommonSound::BustCrate1) && named(CommonSound::BustCrate2) && !named(CommonSound::BustGlass1) &&
                  !named(CommonSound::DebrisMetal1) && !named(CommonSound::BustCeiling),
              "... and a wooden crate's breaking, not glass's nor metal's");
        // What it is made of: the world's wall, and the crate's top.
        const fx wall[3] = {fxFromInt(-128), fxFromInt(10), fxFromInt(70)}, east[3] = {kFxOne, 0, 0};
        const fx top[3] = {fxFromInt(56), 0, fxFromInt(32)}, up[3] = {0, 0, kFxOne};
        check(wallMaterial(g.view, wall, east) == Material::Computer, "the wall is a computer, by its texture");
        check(brushMaterial(g.view, 0, top, up) == Material::Wood &&
                  hldata::detail::surfaceMaterial(g.logic, top, up, 0) == Material::Wood &&
                  hldata::detail::surfaceMaterial(g.logic, top, up, -1) != Material::Wood,
              "the crate is wood, by its own texture: not the world's under it");

        auto hits = [&](CommonSound first, int count)
        {
            int n = 0;
            for (int k = 0; k < count; ++k)
                n += g.times(static_cast<CommonSound>(static_cast<int>(first) + k));
            return n;
        };
        auto whines = [&]()
        {
            return hits(CommonSound::Ricochet1, 2) + hits(CommonSound::Ricochet3, 3);
        };
        g.drain();
        std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
        std::fill(g.loudestHeard.begin(), g.loudestHeard.end(), 0);
        // One round a tick on the wall, each a little apart.
        const int effectsBefore = [&]()
        {
            int n = 0;
            for (const Effect &e : g.actors.effects)
                n += e.kind != 0;
            return n;
        }();
        for (int n = 0; n < 64; ++n)
        {
            const fx at[3] = {wall[0], wall[1] + fxFromInt(n), wall[2] + fxFromInt(n % 7)};
            hldata::detail::bulletImpact(g.logic, at, east, -1, false);
            g.drain();
        }
        int effectsAfter = 0;
        for (const Effect &e : g.actors.effects)
            effectsAfter += e.kind != 0;
        const int sparks = hits(CommonSound::Spark5, 2);
        std::printf("test_view: 64 rounds on a computer: %d glass, %d whines (%d of the new three), %d sparks\n",
                    hits(CommonSound::GlassHit1, 3), whines(), hits(CommonSound::Ricochet3, 3), sparks);
        check(hits(CommonSound::GlassHit1, 3) == 64 && g.loudestHeard[static_cast<int>(CommonSound::GlassHit1)] <= 102,
              "every round on a computer sounds of it, at 0.8");
        check(whines() >= 16 && whines() <= 48 && hits(CommonSound::Ricochet3, 3) > 0, "about half of them whine, of five");
        check(sparks >= 12 && sparks <= 52, "about half of them spark, and crackle");
        check(effectsAfter > effectsBefore, "and sparks are seen");
        // A shotgun's pellets: no sound of the wall.
        std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
        for (int n = 0; n < 16; ++n)
        {
            const fx at[3] = {wall[0], wall[1] - fxFromInt(n), wall[2]};
            hldata::detail::bulletImpact(g.logic, at, east, -1, true);
            g.drain();
        }
        check(hits(CommonSound::GlassHit1, 3) == 0 && whines() > 0, "a shotgun's pellets whine, but the wall is not heard");
        // Five in one tick: two of them heard.
        std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
        for (int n = 0; n < 5; ++n)
        {
            const fx at[3] = {wall[0], wall[1] + fxFromInt(3 * n), wall[2]};
            hldata::detail::bulletImpact(g.logic, at, east, -1, false);
        }
        g.drain();
        check(hits(CommonSound::GlassHit1, 3) == kMaxImpactSounds, "five rounds in a tick: two are heard");
        // On the crate: wood, two thirds as loud, since it breaks.
        std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
        std::fill(g.loudestHeard.begin(), g.loudestHeard.end(), 0);
        for (int n = 0; n < 8; ++n)
        {
            const fx at[3] = {top[0] + fxFromInt(n), top[1], top[2]};
            hldata::detail::bulletImpact(g.logic, at, up, 0, false);
            g.drain();
        }
        int loudest = 0;
        for (int k = 0; k < 3; ++k)
            loudest = std::max(loudest, g.loudestHeard[static_cast<int>(CommonSound::WoodHit1) + k]);
        check(hits(CommonSound::WoodHit1, 3) == 8 && loudest == 114 * 2 / 3,
              "a round on the crate sounds of wood, quieter: it breaks");
        // Shot, and struck with the crowbar: a hole and a dent on the crate,
        // which go with it; and it is heard, wood struck.
        {
            auto onCrate = [&](std::uint8_t picture)
            {
                int n = 0;
                for (const Decal &decal : g.actors.decals)
                    n += decal.active && decal.brush == 0 && decal.picture == picture;
                return n;
            };
            int holes = 0;
            for (int k = 0; k < kDecalShots; ++k)
                holes += onCrate(static_cast<std::uint8_t>(kDecalShot + k));
            const int dents = onCrate(kDecalDent);
            std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
            const fx above[3] = {top[0], top[1] + fxFromInt(4), top[2] + fxFromInt(40)}, down[3] = {0, 0, -kFxOne};
            fireLine(g.logic, g.player, above, down, 64, 8, true);
            g.drain();
            int after = 0;
            for (int k = 0; k < kDecalShots; ++k)
                after += onCrate(static_cast<std::uint8_t>(kDecalShot + k));
            check(after == holes + 1, "a round leaves its hole on the crate, and it is the crate's");
            check(hits(CommonSound::WoodHit1, 3) >= 1, "... and the crate is heard struck: wood");
            const fx near[3] = {top[0] - fxFromInt(6), top[1], top[2] + fxFromInt(20)};
            fireLine(g.logic, g.player, near, down, 32, 8, false, false, nullptr, -1, nullptr, kDmgClub);
            g.drain();
            check(onCrate(kDecalDent) == dents + 1, "a blow of the crowbar leaves its dent on it");
        }
        // A map cooked before: no new whine, no crackle.
        g.heard.commons = kCommonSoundsBefore73;
        std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
        for (int n = 0; n < 32; ++n)
        {
            const fx at[3] = {wall[0], wall[1] + fxFromInt(n), wall[2] - fxFromInt(n % 5)};
            hldata::detail::bulletImpact(g.logic, at, east, -1, false);
            g.drain();
        }
        check(hits(CommonSound::Ricochet3, 3) == 0 && hits(CommonSound::Spark5, 2) == 0 &&
                  hits(CommonSound::Ricochet1, 2) > 0,
              "an older map whines with its own two, and has no crackle");
        g.heard.commons = static_cast<std::uint16_t>(CommonSound::Count);

        // A room with no computer has no use for their sparks.
        BoxRoom plain;
        auto other = std::make_unique<Game>();
        check(other->bsp.parse(buildBoxRoom(plain), error), "a plain room builds");
        finish(*other, nullptr);
        check(other->ok && !other->view.soundThere(static_cast<int>(CommonSound::Spark5)) &&
                  other->view.soundThere(static_cast<int>(CommonSound::Ricochet3)),
              "... and keeps its whines, not a computer's sparks");
        check(other->ok && !other->view.soundThere(static_cast<int>(CommonSound::BustCrate1)) &&
                  !other->view.soundThere(static_cast<int>(CommonSound::BustGlass1)),
              "... nor anything's breaking, with nothing to break");
    }

    /// ... and on a real map, with the game's own list of what its
    /// textures are made of: a round fired at a face of metal, a vent, a
    /// grate, tiles or a computer sounds of it, and computers crackle.
    /// (Glass, in the game's maps, is mostly behind something else that
    /// stops a round first -- a brush that is not drawn, a clip -- and
    /// sounds of that, as in the game.)
    void testImpactsOnMap(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a0.bsp", error))
        {
            check(false, "c1a0 loads");
            return;
        }
        std::ifstream file(std::string(dir) + "/sound/materials.txt", std::ios::binary);
        const MaterialBook book =
            parseMaterials(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
        game->materials = &book;
        finish(*game, nullptr, dir);
        if (!game->ok)
            return;
        Game &g = *game;
        for (int n = 0; n < 10; ++n)
            g.frame();
        g.drain();
        // Each face of those, its first six that a round reaches: fired at
        // its middle from 32 units in front of it.
        int tried[static_cast<int>(Material::Count)] = {}, right[static_cast<int>(Material::Count)] = {};
        int crackles = 0;
        for (std::uint32_t i = 0; i < g.view.surfaceCount; ++i)
        {
            const Surface &surface = g.view.surfaces[i];
            if (surface.texture >= g.view.textureCount)
                continue;
            const Material made = textureMaterial(g.view.textures[surface.texture].flags);
            const int m = static_cast<int>(made);
            if (made == Material::Concrete || made == Material::Glass || tried[m] >= 6)
                continue;
            const Plane &plane = g.view.planes[surface.planeRef & kSurfacePlaneMask];
            const int side = (surface.planeRef & kSurfacePlaneBack) ? -1 : 1;
            fx out[3], from[3], along[3];
            for (int k = 0; k < 3; ++k)
            {
                out[k] = side * (plane.normal[k] >> 18);
                from[k] = fxFromInt(surface.center[k]) + out[k] * 32;
                along[k] = -out[k];
            }
            std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
            fireLine(g.logic, g.player, from, along, 64, 8, true);
            g.drain();
            int heard = 0;
            for (int c = 0; c < static_cast<int>(CommonSound::Count); ++c)
                heard += g.timesHeard[c];
            if (heard == 0)
                continue; // (a face no round reaches)
            const MaterialHit &hit = materialHit(made);
            int its = 0;
            for (int k = 0; k < hit.count; ++k)
                its += g.timesHeard[static_cast<int>(hit.first) + k * hit.step];
            ++tried[m];
            right[m] += its > 0;
            crackles += g.times(CommonSound::Spark5) + g.times(CommonSound::Spark6);
        }
        std::printf("test_view: rounds on c1a0's faces, heard as theirs: metal %d/%d, vent %d/%d, grate %d/%d, tile "
                    "%d/%d, computer %d/%d; %d crackles\n",
                    right[1], tried[1], right[3], tried[3], right[4], tried[4], right[5], tried[5], right[8], tried[8],
                    crackles);
        bool all = true;
        for (const Material made : {Material::Metal, Material::Vent, Material::Grate, Material::Tile, Material::Computer})
            all = all && tried[static_cast<int>(made)] > 0 && right[static_cast<int>(made)] == tried[static_cast<int>(made)];
        check(all, "a round on c1a0's metal, vents, grates, tiles and computers sounds of each");
        check(crackles > 0, "... and its computers crackle");
    }

    /// What breaks, broken, in a room made here: two crates that go off
    /// ("explodemagnitude"), close enough for the first to set off the
    /// second; a pane whose pieces fly the way the blow went ("explosion"
    /// 1 -- and one marked "directed", which the game reads as not); and
    /// pieces of wood heard landing, one landing in six, one a tick at most.
    void testBreaks()
    {
        const MaterialBook book = parseMaterials("W CRATEWOOD\n");
        BoxRoom room;
        for (int k = 0; k < 2; ++k)
        {
            room.mins[k] = -512;
            room.maxs[k] = 512;
        }
        auto box = [&](float x0, float y0, float x1, float y1, float z1, const char *keys)
        {
            BrushBox b;
            b.mins[0] = x0;
            b.mins[1] = y0;
            b.maxs[0] = x1;
            b.maxs[1] = y1;
            b.maxs[2] = z1;
            b.className = "func_breakable";
            b.keys = keys;
            b.textureName = "cratewood";
            room.brushes.push_back(b);
        };
        box(40, -16, 72, 16, 32, "\"material\" \"1\"\n\"health\" \"10\"\n\"explodemagnitude\" \"60\"\n");
        box(-72, -16, -40, 16, 32, "\"material\" \"1\"\n\"health\" \"10\"\n\"explodemagnitude\" \"60\"\n");
        box(-400, 300, -396, 364, 64, "\"material\" \"0\"\n\"health\" \"1\"\n\"explosion\" \"1\"\n");
        box(300, 300, 304, 364, 64, "\"material\" \"0\"\n\"health\" \"1\"\n\"explosion\" \"directed\"\n");
        auto game = std::make_unique<Game>();
        game->materials = &book;
        std::string error;
        check(game->bsp.parse(buildBoxRoom(room), error), "the room of breakables builds");
        finish(*game, nullptr);
        if (!game->ok || game->view.brushCount != 4)
        {
            check(false, "... and cooks, with its four");
            return;
        }
        Game &g = *game;
        auto recordOf = [&](int brush)
        {
            for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
            {
                if (g.view.logic[i].brush == brush)
                    return static_cast<int>(i);
            }
            return -1;
        };
        const int first = recordOf(0), second = recordOf(1), pane = recordOf(2), other = recordOf(3);
        check(first >= 0 && g.view.logic[first].damage == 60 && g.view.logic[pane].damage == 0 &&
                  (g.view.logic[pane].flags & kLogicDirected) && !(g.view.logic[other].flags & kLogicDirected),
              "what goes off and how much; a pane's pieces fly with the blow for \"1\", not for \"directed\"");
        // The first crate broken: it goes off, and the second with it.
        g.player.origin[0] = fxFromInt(-300);
        g.player.origin[1] = fxFromInt(-300);
        g.player.origin[2] = fxFromInt(36);
        for (int n = 0; n < 5; ++n)
            g.frame();
        g.drain();
        std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
        logicStrike(g.logic, g.player, first, 20);
        g.drain();
        check(g.states[first].dead && g.states[second].dead && g.times(CommonSound::Explode) == 2,
              "a crate that goes off goes off as it breaks, and sets off the one in its blast");
        check(g.logic.struckFrom[0] < -kFxOne / 2,
              "... the second struck from the first's way: its pieces would fly away from it");

        // Pieces thrown along a blow, and not.
        ActorWorld &a = g.actors;
        a.pieceLimit = kMaxPieces;
        const fx mins[3] = {fxFromInt(-200), fxFromInt(-20), fxFromInt(100)};
        const fx maxs[3] = {fxFromInt(-150), fxFromInt(20), fxFromInt(140)};
        auto meanX = [&]()
        {
            std::int64_t sum = 0;
            int n = 0;
            for (const Piece &piece : a.pieces)
            {
                if (piece.kind != kPieceShard)
                    continue;
                sum += piece.velocity[0];
                ++n;
            }
            return n > 0 ? static_cast<int>(sum / n / kFxOne) : 0;
        };
        for (Piece &piece : a.pieces)
            piece.kind = 0;
        const fx along[3] = {fxFromInt(200), 0, 0};
        piecesBurst(a, mins, maxs, 1, nullptr, 0, along);
        const int directed = meanX();
        for (Piece &piece : a.pieces)
            piece.kind = 0;
        piecesBurst(a, mins, maxs, 1, nullptr, 0);
        const int random = meanX();
        check(directed > 140 && directed < 260 && random > -60 && random < 60,
              "pieces fly along the blow at 200, give or take 100; or only give or take");

        // Landing: wood's sounds, one in six, no more than one a tick.
        int heard = 0, mostInATick = 0, landings = 0;
        std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
        for (int round = 0; round < 6; ++round)
        {
            for (Piece &piece : a.pieces)
                piece.kind = 0;
            // (The monsters' chances move on between one break and the
            // next, as they do in play: else each round is the last again.)
            aiRandom(a);
            piecesBurst(a, mins, maxs, 1, nullptr, 0);
            for (int n = 0; n < 45; ++n)
            {
                int before = 0;
                for (const Piece &piece : a.pieces)
                    before += piece.kind == kPieceShard ? pieceLanded(piece) : 0;
                auto wood = [&]()
                {
                    return g.times(CommonSound::WoodHit1) + g.times(CommonSound::WoodHit2) + g.times(CommonSound::WoodHit3);
                };
                const int was = wood();
                g.frame(); // (which empties the queue into timesHeard)
                int after = 0;
                for (const Piece &piece : a.pieces)
                    after += piece.kind == kPieceShard ? pieceLanded(piece) : 0;
                landings += after > before ? after - before : 0;
                const int now = wood() - was;
                heard += now;
                mostInATick = now > mostInATick ? now : mostInATick;
            }
        }
        std::printf("test_view: pieces of wood: %d landings, %d heard\n", landings, heard);
        check(heard > 0 && mostInATick <= 1 && heard * 3 <= landings, "pieces of wood are heard landing, now and then, one a tick at most");
    }

} // namespace

/// Who bleeds what, and what the player's going sounds like.
void testBloodAndSteps()
{
    using K = ActorKind;
    auto kind = [](K k) { return static_cast<std::uint8_t>(k); };
    check(bloodOf(kind(K::Grunt)) == kBloodRed && bloodOf(kind(K::Scientist)) == kBloodRed &&
              bloodOf(kind(K::Guard)) == kBloodRed && bloodOf(kind(K::Sitter)) == kBloodRed &&
              bloodOf(kind(K::Barnacle)) == kBloodRed,
          "people bleed red, and so does the barnacle");
    check(bloodOf(kind(K::Headcrab)) == kBloodYellow && bloodOf(kind(K::Zombie)) == kBloodYellow &&
              bloodOf(kind(K::Houndeye)) == kBloodYellow,
          "the aliens bleed yellow");
    check(bloodOf(kind(K::Turret)) == kBloodNone && bloodOf(kind(K::Tripmine)) == kBloodNone &&
              bloodOf(kind(K::Gman)) == kBloodNone,
          "machines do not bleed, nor the man in the suit");
    // (The man in the suit takes a shot -- and shrugs it off: gman.cpp.)
    check(canBeHurt(kind(K::Scientist)) && canBeHurt(kind(K::Guard)) && canBeHurt(kind(K::Gman)) &&
              !fights(kind(K::Scientist)),
          "people can be hurt, without being something that fights");
    check(decalFamily(kDecalHand) == decalFamily(kDecalBlood) && decalFamily(kDecalPool) == 5 &&
              decalRect(kDecalHand).y == 32 && decalRect(kDecalSplash).w == 32 && decalRect(kDecalShot + 1).w == 16 &&
              decalFamily(kDecalSplash2) == 5 && decalFamily(kDecalWallCrack) != 5,
          "the large pictures are blood, each in a cell of its own; the holes share one");
    {
        // The map's own marks are never folded together; the same one put
        // again is itself.
        ActorWorld marks;
        const fx normal[3] = {kFxOne, 0, 0};
        const fx here[3] = {0, 0, 0}, beside[3] = {0, fxFromInt(20), 0};
        decalAdd(marks, kDecalBlood, here, normal, 26, kMarkBlood, -1, true);
        decalAdd(marks, kDecalBlood + 1, beside, normal, 26, kMarkBlood, -1, true);
        decalAdd(marks, kDecalBlood, here, normal, 26, kMarkBlood, -1, true);
        int count = 0;
        for (const Decal &d : marks.decals)
            count += d.active ? 1 : 0;
        check(count == 2, "two of the map's stains side by side stay two, and one put twice is one");
    }

    // The steps, as the real game's were measured (tools/ref): how many in a
    // second and a half of each way of going, and how loud.
    auto steps = [](fx speed, bool ducked, bool ladder, int water, StepsHeard &last)
    {
        PlayerState p;
        p.onGround = !ladder;
        p.onLadder = ladder;
        p.ducked = ducked;
        p.waterLevel = static_cast<std::uint8_t>(water);
        p.velocity[0] = speed;
        int count = 0;
        for (int n = 0; n < 45; ++n)
        {
            const StepsHeard heard = playerSteps(p, 33);
            if (heard.count != 0)
                last = heard;
            count += heard.count;
        }
        return count;
    };
    StepsHeard last;
    check(steps(fxFromInt(320), false, false, 0, last) == 5 && stepVolume(last) == 63, "a run: a step each 0.3 s, at half volume");
    check(steps(fxFromInt(120), false, false, 0, last) == 4 && stepVolume(last) == 25, "a walk: each 0.4 s, at a fifth: quiet, not silent");
    check(steps(fxFromInt(20), false, false, 0, last) == 4, "... and however slow the walk");
    check(stepVolume(last, 5) == 31 && stepVolume(last, 20) == 50, "dirt and a vent are louder than the plain floor");
    check(steps(fxFromInt(40), true, false, 0, last) == 3 && stepVolume(last) == 8, "crouched: each 0.5 s, at 7 percent");
    check(steps(fxFromInt(100), true, false, 0, last) == 4 && last.ran, "crouched, a run is from 80");
    check(steps(fxFromInt(200), false, true, 0, last) == 4 && last.kind == StepKind::Ladder && stepVolume(last) == 44, "a rung each 0.45 s");
    check(steps(0, false, true, 0, last) == 0 && steps(0, false, false, 0, last) == 0, "and nothing for holding still");
    check(steps(fxFromInt(150), false, false, 2, last) == 3 && last.kind == StepKind::Wade && stepVolume(last) == 82, "water to the waist: a wade each 0.6 s");
    check(steps(fxFromInt(320), false, false, 1, last) == 5 && last.kind == StepKind::Slosh, "water round the feet is a slosh");
    {
        PlayerState air;
        air.velocity[0] = fxFromInt(320);
        check(playerSteps(air, 33).count == 0, "nothing in the air");
        PlayerState old;
        old.onGround = true;
        old.stepLeft = fxFromInt(90);
        old.velocity[0] = fxFromInt(320);
        int waited = 0;
        while (playerSteps(old, 33).count == 0 && waited < 100)
            ++waited;
        check(waited <= 19, "what an old save kept there does not silence the player");
    }
}

namespace
{
    /// The game's spread, drawn as it draws it: rounds the real game fired
    /// (tools/ref/diag.sh's shotgun, its logged seed, round and draws).
    void testSharedSpread()
    {
        struct Round
        {
            std::uint32_t seed;
            int n;
            double x, y;
        };
        const Round rounds[] = {{773, 1, 0.61771, -0.07773}, {773, 6, -0.03058, -0.04060},
                                {837, 1, 0.09013, -0.57930}, {837, 12, -0.14891, -0.87567},
                                {804, 1, 0.34521, 0.42032}};
        bool same = true;
        for (const Round &r : rounds)
        {
            std::int32_t x, y;
            sharedSpread(r.seed, r.n, x, y);
            same = same && std::abs(x / 65536.0 - r.x) < 1e-5 && std::abs(y / 65536.0 - r.y) < 1e-5;
        }
        check(same, "a round's spread is the real game's for the same seed and round");
        // Only the seed's low byte picks where the numbers start.
        std::int32_t a, b, c, d;
        sharedSpread(5, 1, a, b);
        sharedSpread(5 + 256, 1, c, d);
        check(a == c && b == d, "... and seeds 256 apart draw the same");
        check(sharedRandomLong(42, 7, 7) == 7, "a whole number from nothing to choose is that number");
        bool within = true;
        for (std::uint32_t seed = 0; seed < 512; ++seed)
        {
            const int v = sharedRandomLong(seed, 0, 2);
            within = within && v >= 0 && v <= 2;
        }
        check(within, "... and one of 0..2 is one of them");
    }
} // namespace

// A mark on a brush that moves goes with it (a door, a lift, a train); on
// one that turns, there is none (it would be left in the air).
void testMarksOnMovers(const char *dir)
{
    auto game = std::make_unique<Game>();
    std::string error;
    if (!game->bsp.load(std::string(dir) + "/maps/c1a0.bsp", error))
    {
        check(false, (std::string(dir) + "/maps/c1a0.bsp loads").c_str());
        return;
    }
    finish(*game, nullptr, dir);
    if (!game->ok)
        return;
    Game &g = *game;
    ActorWorld &a = g.actors;
    int sliding = -1, turning = -1;
    // (One whose top is flat and wide, met by a line down onto its middle.)
    fx at[3] = {0, 0, 0}, up[3] = {0, 0, kFxOne};
    for (int m = 0; m < g.move.moverCount; ++m)
    {
        const bool pivot = (g.move.brushes[m].flags & kBrushHasPivot) != 0;
        if (!g.brushStates[m].active)
            continue;
        if (pivot && turning < 0)
            turning = m;
        const MoverHull &hull = g.move.movers[m];
        if (pivot || moverIsBox(hull) || sliding >= 0 || hull.maxs[0] - hull.mins[0] < fxFromInt(40) ||
            hull.maxs[1] - hull.mins[1] < fxFromInt(40))
            continue;
        const fx middle[2] = {(hull.mins[0] + hull.maxs[0]) / 2, (hull.mins[1] + hull.maxs[1]) / 2};
        const fx from[3] = {middle[0], middle[1], hull.maxs[2] + fxFromInt(8)};
        const fx to[3] = {middle[0], middle[1], hull.maxs[2] - fxFromInt(8)};
        int on = -1;
        if (traceShotWall(g.move, from, to, at, up, &on, true) && on == m && up[2] > 4000)
            sliding = m;
    }
    check(sliding >= 0, "c1a0 has a brush that moves without turning, flat on top");
    if (sliding < 0)
        return;
    markOn(a, g.move, sliding, kDecalShot, at, up, 8, kMarkShot);
    const Decal *mark = nullptr;
    for (const Decal &decal : a.decals)
        mark = decal.active ? &decal : mark;
    check(mark != nullptr && mark->brush == sliding &&
              mark->at[0] + g.brushStates[sliding].pos[0] == at[0] &&
              mark->at[2] + g.brushStates[sliding].pos[2] == at[2],
          "a mark on a brush that moves is kept from where the brush is");
    if (mark == nullptr)
        return;
    fx moved[3], was[3];
    for (int k = 0; k < 3; ++k)
    {
        was[k] = g.brushStates[sliding].pos[k];
        moved[k] = g.brushStates[sliding].pos[k] + (k == 2 ? fxFromInt(64) : 0);
    }
    logicPlaceBrush(g.logic, sliding, moved);
    check(mark->at[2] + g.brushStates[sliding].pos[2] == at[2] + fxFromInt(64), "... and goes up with it");
    logicPlaceBrush(g.logic, sliding, was);
    if (turning >= 0)
    {
        int before = 0, after = 0;
        for (const Decal &decal : a.decals)
            before += decal.active;
        markOn(a, g.move, turning, kDecalShot, at, up, 8, kMarkShot);
        for (const Decal &decal : a.decals)
            after += decal.active;
        check(before == after, "a brush that turns takes no mark");
    }
    // A mark near its edge is made smaller, to lie on it: none hangs past
    // the edge, where it would be seen in the air. (Its top at its edge, x
    // the brush's own: 5 units in, a hole 8 across each way is 4; 2 units
    // in, there is room for none.)
    {
        for (Decal &decal : a.decals)
            decal.active = false;
        const MoverHull &hull = g.move.movers[sliding];
        const fx middleY = (hull.mins[1] + hull.maxs[1]) / 2;
        // (Where its top ends along x: the last whole unit a line down from
        // above it meets it, out from its middle.)
        const fx top = at[2];
        fx edgeX = (hull.mins[0] + hull.maxs[0]) / 2;
        for (fx x = edgeX; x <= hull.maxs[0] + fxFromInt(32); x += kFxOne)
        {
            const fx from[3] = {x, middleY, top + fxFromInt(8)}, to[3] = {x, middleY, top - fxFromInt(8)};
            fx on[3], facing[3];
            int met = -1;
            if (!traceShotWall(g.move, from, to, on, facing, &met, true) || met != sliding || facing[2] < 4000 ||
                std::abs(on[2] - top) > kFxOne / 8)
                break;
            edgeX = x;
        }
        auto lastHalf = [&](int inside) {
            const fx from[3] = {edgeX - fxFromInt(inside), middleY, top + fxFromInt(8)};
            const fx to[3] = {from[0], middleY, top - fxFromInt(8)};
            fx on[3], facing[3];
            int met = -1;
            if (!traceShotWall(g.move, from, to, on, facing, &met, true) || met != sliding)
                return -1;
            for (Decal &decal : a.decals)
                decal.active = false;
            markOn(a, g.move, sliding, kDecalShot, on, facing, 8, kMarkShot);
            for (const Decal &decal : a.decals)
                if (decal.active)
                    return static_cast<int>(decal.half);
            return 0;
        };
        const int roomy = lastHalf(20), near = lastHalf(5), edge = lastHalf(2);
        std::printf("  marks near a brush's edge (x %.0f): 20 in %d, 5 in %d, 2 in %d\n", edgeX / 4096.0, roomy, near,
                    edge);
        check(roomy == 8 && near == 4 && edge == 0, "a mark near an edge is made smaller to lie on it, or not made");
    }
}

// The sky takes no mark, as in the game: c1a0e's sky over its yard, at z
// 3488 (a face of the sky facing down, its middle at -1525 -2472).
void testNoMarkOnSky(const char *dir)
{
    auto game = std::make_unique<Game>();
    std::string error;
    if (!game->bsp.load(std::string(dir) + "/maps/c1a0e.bsp", error))
    {
        check(false, (std::string(dir) + "/maps/c1a0e.bsp loads").c_str());
        return;
    }
    finish(*game, nullptr, dir);
    if (!game->ok)
        return;
    Game &g = *game;
    const fx under[3] = {fxFromInt(-1525), fxFromInt(-2472), fxFromInt(3480)};
    const fx above[3] = {under[0], under[1], fxFromInt(3600)};
    fx at[3], normal[3];
    int on = -1;
    const bool met = traceShotWall(g.move, under, above, at, normal, &on, true);
    check(met && on < 0 && std::abs(at[2] - fxFromInt(3488)) <= kFxOne && normal[2] < -4000,
          "a line up from c1a0e's yard meets its sky");
    check(!traceShotWall(g.move, under, above, at, normal, &on, true, true), "... which takes no mark");
    const fx upward[3] = {0, 0, kFxOne};
    const fx wound[3] = {under[0], under[1], fxFromInt(3440)};
    int before = 0, after = 0;
    for (const Decal &decal : g.actors.decals)
        before += decal.active;
    // (Blood flies on a little down: from this close, still up into it.)
    markBlood(g.actors, g.move, wound, upward, false, false);
    for (const Decal &decal : g.actors.decals)
        after += decal.active;
    check(before == after, "blood thrown up into the sky leaves no stain on it");
    check(markShot(g.actors, g.move, under, above, false), "a round into the sky still lands (its sound, its sparks)");
    after = 0;
    for (const Decal &decal : g.actors.decals)
        after += decal.active;
    check(before == after, "... but leaves no hole in it");
}

// Which marks are drawn (HlMarkPlan.hpp): a stain beside the eye does not
// leave the rest of a corridor's out; none thins out to nothing.
void testMarkChoice()
{
    // A corridor along x, its wall at y = 40 facing the eye at the origin: a
    // blood stain 30 ahead of the eye, and ten more farther along.
    auto candidate = [](int slot, int x, int half, int share, bool shown) {
        const double dist = std::sqrt(double(x) * x + 40.0 * 40.0);
        MarkCandidate one;
        one.slot = static_cast<std::uint8_t>(slot);
        one.shown = shown;
        one.near = static_cast<std::int32_t>(dist);
        one.area = markArea(half, one.near, static_cast<int>(40.0 / dist * 4096), share);
        return one;
    };
    for (const int share : {kMarkAreaDs, kMarkAreaDsi})
    {
        MarkCandidate list[11];
        list[0] = candidate(0, 30, 20, share, false);
        for (int i = 1; i < 11; ++i)
            list[i] = candidate(i, 30 + i * 60, 20, share, false);
        check(list[0].area <= share / 4, "a stain beside the eye counts a quarter of the share at most");
        std::uint64_t drawn = 0;
        check(markChoose(list, 11, 24, share, drawn) == 11 && drawn == 0x7FF,
              "... and the corridor's other stains are all drawn with it");
    }
    {
        // The edge of the share: one drawn the pass before keeps its place to
        // the end of it, one new needs a tenth spare.
        MarkCandidate list[2];
        list[0].near = 10;
        list[0].area = 9000;
        list[1].near = 20;
        list[1].area = 8500;
        std::uint64_t drawn = 0;
        list[1].shown = false;
        check(markChoose(list, 2, 24, 18000, drawn) == 1 && drawn == 1, "a new mark needs a tenth of the share spare");
        list[1].shown = true;
        check(markChoose(list, 2, 24, 18000, drawn) == 2 && drawn == 3, "... one already shown keeps its place to its end");
        // (One that does not fit leaves room for a smaller one after it.)
        MarkCandidate three[3];
        three[0].near = 1;
        three[0].area = 9000;
        three[1].near = 2;
        three[1].area = 9000;
        three[2].near = 3;
        three[2].area = 100;
        check(markChoose(three, 3, 24, 18000, drawn) == 2 && drawn == 5, "a smaller mark past one that does not fit is drawn");
        check(markChoose(three, 3, 1, 18000, drawn) == 1 && drawn == 1, "no more than the count");
    }
    // Range: a hole goes at 680 units, a new one comes in a sixteenth nearer.
    check(markRange(8) == 680 && markInRange(680, 8, true) && !markInRange(680, 8, false) &&
              markInRange(680 - 680 / 16, 8, false),
          "a mark at the end of its range stays, a new one comes in nearer");
    // Alpha: the same however far; only its last three seconds fade.
    check(markAlpha(40000) == kMarkAlpha && markAlpha(3000) == kMarkAlpha && markAlpha(1500) < kMarkAlpha &&
              markAlpha(0) == 1,
          "a mark is drawn as it is until its last three seconds");
}

/// What is only seen -- pieces, specks, casings, marks -- has room for more
/// on a DSi than on a DS: it must take nothing from the monsters' chances,
/// or the same fight would go another way on the other console.
void testDebrisLeavesMonstersAlone()
{
    const MaterialBook book = parseMaterials("W CRATEWOOD\n");
    auto make = [&](bool dsi)
    {
        BoxRoom room;
        BrushBox crate;
        crate.mins[0] = 40;
        crate.mins[1] = -16;
        crate.maxs[0] = 72;
        crate.maxs[1] = 16;
        crate.maxs[2] = 32;
        crate.className = "func_breakable";
        crate.keys = "\"material\" \"1\"\n\"health\" \"10\"\n";
        crate.textureName = "cratewood";
        room.brushes.push_back(crate);
        auto game = std::make_unique<Game>();
        game->materials = &book;
        std::string error;
        if (game->bsp.parse(buildBoxRoom(room), error))
            finish(*game, nullptr);
        if (game->ok)
        {
            consoleLimits(game->actors, game->logic, dsi);
            game->actors.seed = 12345;
        }
        return game;
    };
    auto ds = make(false), dsi = make(true);
    check(ds->ok && dsi->ok && ds->actors.pieceLimit < dsi->actors.pieceLimit, "a room cooks, for a DS and a DSi");
    if (!ds->ok || !dsi->ok)
        return;
    // A big crate's worth of pieces (more than either has room for), twenty
    // rounds into one spot and blows beside it (one mark on a DSi, which
    // keeps them; on a DS some of them new), casings, a can's worth.
    auto fight = [](Game &g)
    {
        ActorWorld &a = g.actors;
        const fx mins[3] = {fxFromInt(-100), fxFromInt(-100), fxFromInt(0)};
        const fx maxs[3] = {fxFromInt(100), fxFromInt(100), fxFromInt(100)};
        const std::uint16_t textures[1] = {0};
        piecesBurst(a, mins, maxs, 1, textures, 1);
        const fx eye[3] = {0, 0, fxFromInt(48)};
        const fx east[3] = {kFxOne, 0, 0};
        for (int n = 0; n < 20; ++n)
        {
            const fx to[3] = {fxFromInt(-400), fxFromInt(n * 3), fxFromInt(48)};
            fx at[3], normal[3];
            if (markShot(a, g.move, eye, to, n & 1, at, normal))
                hldata::detail::bulletImpact(g.logic, at, east, -1, false);
            const fx toss[3] = {fxFromInt(40), 0, fxFromInt(60)};
            shellEject(a, n & 1 ? kPieceShell : kPieceShotShell, eye, toss);
            g.frame();
        }
        int draws = 0;
        for (int n = 0; n < 50; ++n)
            draws = draws * 31 + aiRandomLong(a, 0, 1000);
        return draws;
    };
    const std::uint32_t before = ds->actors.seed;
    {
        ActorWorld &a = ds->actors;
        const fx at[3] = {0, 0, fxFromInt(8)}, up[3] = {0, 0, kFxOne}, still[3] = {0, 0, 0};
        const fx mins[3] = {0, 0, 0}, maxs[3] = {fxFromInt(64), fxFromInt(64), fxFromInt(64)};
        piecesBurst(a, mins, maxs, 0, nullptr, 0);
        specksAdd(a, at, up, 5, 0x7FFF);
        pieceAdd(a, kPieceShell, at, still, 1200, 0);
        shellEject(a, kPieceShell, at, still);
        decalAdd(a, kDecalShot, at, up, 8, kMarkShot);
        check(a.seed == before, "pieces, specks, casings and marks draw nothing from the monsters' chances");
        for (Piece &piece : a.pieces)
            piece = Piece();
        for (Decal &decal : a.decals)
            decal = Decal();
    }
    const int onDs = fight(*ds), onDsi = fight(*dsi);
    check(ds->actors.seed == dsi->actors.seed && onDs == onDsi,
          "... so a fight goes the same on a DS as on a DSi, which has room for more of them");

    // Debris set three quarters into the floor (as a map's scripted debris
    // is into a ceiling): its pieces come only out of what is open, as many
    // as there is open room for -- never inside the floor, to lie unseen.
    {
        ActorWorld &a = dsi->actors;
        for (Piece &piece : a.pieces)
            piece = Piece();
        const fx mins[3] = {fxFromInt(-40), fxFromInt(-40), fxFromInt(-48)};
        const fx maxs[3] = {fxFromInt(40), fxFromInt(40), fxFromInt(16)};
        const int open = piecesBurst(a, mins, maxs, 1, nullptr, 0, nullptr, &dsi->move);
        bool clear = true;
        for (const Piece &piece : a.pieces)
            clear = clear && (piece.kind != kPieceShard || pointContents(dsi->move, piece.pos) != kContentsSolid);
        check(open > 0 && clear, "debris sunk into the floor throws its pieces from what is open of it, none from inside");
    }
}

/// A crate broken on a DS, with its room for 24 small things: its pieces
/// leave the next shots' casings room to be seen.
void testCasingRoom()
{
    auto world = std::make_unique<ActorWorld>();
    ActorWorld &a = *world;
    a.pieceLimit = 24;
    const fx mins[3] = {0, 0, 0}, maxs[3] = {fxFromInt(200), fxFromInt(200), fxFromInt(200)};
    const int thrown = piecesBurst(a, mins, maxs, 1, nullptr, 0);
    const fx eye[3] = {0, 0, fxFromInt(48)}, toss[3] = {fxFromInt(40), 0, fxFromInt(60)};
    for (int n = 0; n < 4; ++n)
        shellEject(a, kPieceShell, eye, toss);
    int shards = 0, shells = 0;
    for (const Piece &piece : a.pieces)
    {
        shards += piece.kind == kPieceShard;
        shells += piece.kind == kPieceShell;
    }
    check(thrown == 20 && shards == 20 && shells == 4,
          "a big crate broken on a DS throws 20 pieces, and four casings after it are all seen");
}

/// Who may come in from the map next door is cooked with walk and run, to
/// be told where to go; a mine on a wall there comes as it is.
void testVisitorClips()
{
    std::vector<Entity> here, next;
    std::string error;
    const bool parsed = parseEntities("{\n\"classname\" \"worldspawn\"\n}\n", here, error) &&
                        parseEntities("{\n\"classname\" \"info_landmark\"\n\"targetname\" \"door\"\n"
                                      "\"origin\" \"0 0 0\"\n}\n"
                                      "{\n\"classname\" \"monster_tripmine\"\n\"targetname\" \"mine\"\n"
                                      "\"origin\" \"64 0 32\"\n}\n"
                                      "{\n\"classname\" \"monster_zombie\"\n\"targetname\" \"zed\"\n"
                                      "\"origin\" \"128 0 0\"\n}\n",
                                      next, error);
    std::vector<ModelNeed> needs;
    if (parsed)
        addVisitorNeeds(here, next, "door", needs);
    auto has = [&](const char *name, const char *clip)
    {
        for (const ModelNeed &need : needs)
        {
            if (need.name == name)
                return std::find(need.clips.begin(), need.clips.end(), clip) != need.clips.end();
        }
        return false;
    };
    check(parsed && has("zombie", "walk") && has("zombie", "run"),
          "a zombie by the door next door is cooked to walk and run here");
    check(has("v_tripmine", "world") && !has("v_tripmine", "walk") && !has("v_tripmine", "run"),
          "... a tripmine by it, as it lies on its wall, and not to walk");
}

/// What a breakable comes apart into, cooked: its material's model of
/// pieces (the game's), or its own ("gibmodel"); none for the glass that
/// never breaks, for no material, or for a box that only moves.
void testBreakGibNeeds()
{
    auto needed = [](const char *text)
    {
        std::vector<Entity> entities;
        std::string error;
        std::vector<std::string> names;
        if (!parseEntities(text, entities, error))
            return names;
        for (const ModelNeed &need : collectModelNeeds(entities))
        {
            if (need.name != "hgibs")
                names.push_back(need.name);
        }
        return names;
    };
    auto only = [](const std::vector<std::string> &names, const char *name)
    { return names.size() == 1 && names[0] == name; };
    check(only(needed("{\n\"classname\" \"func_breakable\"\n\"material\" \"1\"\n}\n"), "woodgibs"),
          "a wooden crate needs the game's pieces of wood");
    check(only(needed("{\n\"classname\" \"func_breakable\"\n\"material\" \"0\"\n}\n"), "glassgibs") &&
              only(needed("{\n\"classname\" \"func_breakable\"\n\"material\" \"8\"\n}\n"), "rockgibs"),
          "... a pane, of glass; rock, of rock");
    check(only(needed("{\n\"classname\" \"func_breakable\"\n\"material\" \"1\"\n"
                      "\"gibmodel\" \"Models\\MyCrate.mdl\"\n}\n"),
               "mycrate"),
          "... and one that names its own pieces, those");
    check(needed("{\n\"classname\" \"func_breakable\"\n\"material\" \"7\"\n}\n").empty() &&
              needed("{\n\"classname\" \"func_breakable\"\n\"material\" \"9\"\n}\n").empty(),
          "the glass that never breaks needs none, nor what is made of nothing");
    check(needed("{\n\"classname\" \"func_pushable\"\n\"material\" \"1\"\n}\n").empty() &&
              only(needed("{\n\"classname\" \"func_pushable\"\n\"material\" \"2\"\n\"spawnflags\" \"128\"\n}\n"),
                   "metalplategibs"),
          "a box that only moves needs none; one that breaks, its material's");
    LogicRecord record{};
    record.extra[1] = 2u | 5u << 16;
    check(breakMaterial(record) == 2 && breakGibRef(record) == 4, "its material and its pieces' model share extra[1]");
    record.extra[1] = 6;
    check(breakMaterial(record) == 6 && breakGibRef(record) == -1, "... none in a map cooked before");
}

/// With the game's models: a room of a wooden crate and a pane, cooked; each
/// knows its model of pieces, which only a DSi loads; broken, they throw
/// those pieces, see-through glass, heard landing as their shards are.
void testBreakGibs(const char *dir)
{
    const MaterialBook book = parseMaterials("W CRATEWOOD\n");
    BoxRoom room;
    auto box = [&](float x0, float y0, float x1, float y1, float z1, const char *keys)
    {
        BrushBox b;
        b.mins[0] = x0;
        b.mins[1] = y0;
        b.maxs[0] = x1;
        b.maxs[1] = y1;
        b.maxs[2] = z1;
        b.className = "func_breakable";
        b.keys = keys;
        b.textureName = "cratewood";
        room.brushes.push_back(b);
    };
    box(40, -16, 72, 16, 32, "\"material\" \"1\"\n\"health\" \"10\"\n");
    box(-72, -48, -68, 48, 64, "\"material\" \"0\"\n\"health\" \"1\"\n");
    auto game = std::make_unique<Game>();
    game->materials = &book;
    std::string error;
    check(game->bsp.parse(buildBoxRoom(room), error), "a room of a crate and a pane builds");
    finish(*game, nullptr, dir);
    if (!game->ok || game->view.brushCount != 2)
    {
        check(false, "... and cooks, with both");
        return;
    }
    Game &g = *game;
    int crate = -1, pane = -1;
    for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
    {
        if (g.view.logic[i].brush == 0)
            crate = breakGibRef(g.view.logic[i]);
        if (g.view.logic[i].brush == 1)
            pane = breakGibRef(g.view.logic[i]);
    }
    const auto name = [&](int m) { return m >= 0 && m < static_cast<int>(g.view.modelRefCount) ? g.view.modelRefs[m].name : ""; };
    check(std::string(name(crate)) == "woodgibs" && std::string(name(pane)) == "glassgibs",
          "the crate's pieces are the game's wood, the pane's its glass");
    check(modelRefDsiOnly(g.view.modelRefs[crate]) && modelRefDsiOnly(g.view.modelRefs[pane]) &&
              g.actors.humanGibs >= 0 && !modelRefDsiOnly(g.view.modelRefs[g.actors.humanGibs]),
          "... which a DS does not load (a person's pieces it does)");
    check(g.breakGibs.count == 2 && breakGibMaterial(g.actors, crate) == 1 && breakGibMaterial(g.actors, pane) == 0 &&
              breakGibMaterial(g.actors, g.actors.humanGibs) == -1,
          "the map knows what each model of pieces is of");
    check(breakStyle(breakGibMaterial(g.actors, pane)).alpha < 31, "... glass's drawn see-through, as its shards");
    ActorWorld &a = g.actors;
    consoleLimits(a, g.logic, true);
    const int pieces = gibPieces(a, crate);
    const std::uint32_t seed = a.seed;
    const fx mins[3] = {fxFromInt(40), fxFromInt(-16), 0}, maxs[3] = {fxFromInt(72), fxFromInt(16), fxFromInt(32)};
    const int thrown = breakGibsBurst(a, mins, maxs, 1, crate, nullptr, &g.move);
    int flying = 0;
    bool bodies = true, lasting = true;
    for (const Gib &gib : a.gibs)
    {
        if (!gib.active)
            continue;
        ++flying;
        bodies = bodies && gib.model == crate && gib.body < pieces && gibBodyDrawn(a.models[crate], gib.body);
        lasting = lasting && gib.leftMs >= 2500 * 150 / 100 && gib.leftMs <= 3500 * 150 / 100;
    }
    check(pieces > 0 && thrown == (32 * 32 + 32 * 32 + 32 * 32) / 432 && flying == thrown && bodies,
          "the crate broken throws as many of the game's pieces of wood as of its texture, each one of them");
    check(lasting && a.seed == seed, "... for as long, and of chances of their own");
    // (Thrown from a box on the floor, low in it: they move as points, as
    // the game's do -- not as a crouched person, which the floor would hold
    // them inside of and soon put out.)
    for (int n = 0; n < 3; ++n)
        g.frame();
    int still = 0;
    for (const Gib &gib : a.gibs)
        still += gib.active && gib.leftMs > 2000;
    check(still == thrown, "... and none of them lost in the floor it lay on");
    a.gibLimit = 4;
    check(breakGibsBurst(a, mins, maxs, 1, crate) == 4, "... no more than there is room for");
    a.gibLimit = kMaxGibs;
    // (The pane's: the cooked glassgibs has a piece with nothing in it,
    // which is never thrown.)
    {
        const fx thin[3] = {fxFromInt(-68), fxFromInt(48), fxFromInt(64)}, at[3] = {fxFromInt(-72), fxFromInt(-48), 0};
        for (Gib &gib : a.gibs)
            gib = Gib();
        breakGibsBurst(a, at, thin, 0, pane);
        bool seen = true;
        for (const Gib &gib : a.gibs)
            seen = seen && (!gib.active || gibBodyDrawn(a.models[pane], gib.body));
        check(seen, "a pane's pieces are each one with something to see");
    }
    // Heard landing: some, of wood.
    g.drain();
    std::fill(g.timesHeard.begin(), g.timesHeard.end(), 0);
    for (int round = 0; round < 6; ++round)
    {
        for (Gib &gib : a.gibs)
            gib = Gib();
        const fx high[3] = {mins[0], mins[1], mins[2] + fxFromInt(60)};
        const fx top[3] = {maxs[0], maxs[1], maxs[2] + fxFromInt(60)};
        breakGibsBurst(a, high, top, 1, crate);
        for (int n = 0; n < 60; ++n)
            g.frame();
    }
    const int wood = g.times(CommonSound::WoodHit1) + g.times(CommonSound::WoodHit2) + g.times(CommonSound::WoodHit3);
    std::printf("test_view: the game's pieces of wood: %d heard landing\n", wood);
    check(wood > 0, "... and heard landing, of wood");
    // Where its model is not loaded (a DS's), there are none to throw.
    const ModelView kept = a.models[crate];
    g.modelViews[static_cast<std::size_t>(crate)].header = nullptr;
    check(breakGibsBurst(a, mins, maxs, 1, crate) == 0 && gibPieces(a, crate) == 0,
          "a DS without the model throws none of it: its texture's patches, as before");
    g.modelViews[static_cast<std::size_t>(crate)] = kept;
}

int main()
{
    testVisitorClips();
    testBreakGibNeeds();
    testCasingRoom();
    testDebrisLeavesMonstersAlone();
    testMarkChoice();
    testSharedSpread();
    testBloodAndSteps();
    testKick();
    testWalk();
    testFall();
    testHurt();
    testDeath();
    testRoomLanding();
    testImpacts();
    testBreaks();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testLanding(dir);
        testMarks(dir);
        testMarksOnMovers(dir);
        testNoMarkOnSky(dir);
        testImpactsOnMap(dir);
        testBreakGibs(dir);
    }
    else
        std::printf("test_view: HLDS_VALVE_DIR not set, no real fall\n");
    if (g_failures == 0)
        std::printf("test_view: ok\n");
    return g_failures == 0 ? 0 : 1;
}
