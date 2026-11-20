// What moves the view and what being hurt shows (shared/HlView.hpp): the
// kick that dies away, the bob and the lean of a walk, the cost and the jolt
// of a fall, the marks of a blow, the camera's fall at death; a fall in a room
// made here, and where rounds land in another. With HLDS_VALVE_DIR set, a
// real fall in a real map, and rounds on its faces.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

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
            const int plain = pieceAdd(a, kPieceShell, eye, still, 1200).leftMs;
            a.lifePercent = 150;
            const int longer = pieceAdd(a, kPieceShell, eye, still, 1200).leftMs;
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

int main()
{
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
        testImpactsOnMap(dir);
    }
    else
        std::printf("test_view: HLDS_VALVE_DIR not set, no real fall\n");
    if (g_failures == 0)
        std::printf("test_view: ok\n");
    return g_failures == 0 ? 0 : 1;
}
