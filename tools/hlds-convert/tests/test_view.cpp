// What moves the view and what being hurt shows (shared/HlView.hpp): the
// kick that dies away, the bob and the lean of a walk, the cost and the jolt
// of a fall, the marks of a blow, the camera's fall at death; a fall in a room
// made here. With HLDS_VALVE_DIR set, a real fall in a real map.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

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
        check(glass >= 6 && glass <= 16 && flying(kPieceShard) == glass && own && inside,
              "broken glass throws a dozen or so pieces of itself, see-through, from where it was");
        int lay = 0, soonest = 100000;
        for (int n = 0; n < 60; ++n)
        {
            g.frame();
            int lying = 0;
            for (const Piece &piece : a.pieces)
            {
                if (piece.kind == kPieceShard && piece.bounces >= 2 && piece.pos[2] < mins[2])
                {
                    ++lying;
                    soonest = piece.leftMs < soonest ? piece.leftMs : soonest;
                }
            }
            lay = lying > lay ? lying : lay;
        }
        check(lay >= glass / 3, "... which fall, and lie");
        check(soonest <= 500, "what lies is not left lying long");
        check(flying(kPieceShard) == 0, "... and all of it is gone within two seconds");
        check(piecesBurst(a, mins, maxs, 2, nullptr, 0) < glass && breakStyle(5).weight < breakStyle(2).weight &&
                  breakStyle(1).lastMs > breakStyle(0).lastMs && breakStyle(1).lastMs <= 2000,
              "metal breaks into fewer; ceiling tiles drift; wood lasts longer than glass, and none of it long");
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

    // A ladder: a rung for every 48 units up or down, none for standing.
    PlayerState s;
    s.onLadder = true;
    s.stepLeft = 0;
    fx before[3] = {0, 0, 0};
    int rungs = 0;
    for (int n = 0; n < 30; ++n)
    {
        before[2] = s.origin[2];
        s.origin[2] += fxFromInt(8);
        const StepsHeard heard = playerSteps(s, before);
        check(heard.kind == StepKind::Ladder, "on a ladder, what is heard is rungs");
        rungs += heard.count;
    }
    check(rungs == 5 || rungs == 6, "a rung for every 48 units climbed");
    before[2] = s.origin[2];
    check(playerSteps(s, before).count == 0, "and none for holding still");
    // Water: to the waist it is wading, whatever is underfoot.
    PlayerState w;
    w.waterLevel = 2;
    w.velocity[0] = fxFromInt(150);
    int wades = 0;
    fx from[3] = {0, 0, 0};
    for (int n = 0; n < 40; ++n)
    {
        from[0] = w.origin[0];
        w.origin[0] += fxFromInt(6);
        const StepsHeard heard = playerSteps(w, from);
        wades += heard.kind == StepKind::Wade ? heard.count : 100;
    }
    check(wades == 2 || wades == 3, "a wade for every 120 units through deep water");
    PlayerState feet;
    feet.waterLevel = 1;
    feet.origin[0] = from[0];
    check(playerSteps(feet, from).kind == StepKind::Slosh, "water round the feet is a slosh");
}

int main()
{
    testBloodAndSteps();
    testKick();
    testWalk();
    testFall();
    testHurt();
    testDeath();
    testRoomLanding();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testLanding(dir);
        testMarks(dir);
    }
    else
        std::printf("test_view: HLDS_VALVE_DIR not set, no real fall\n");
    if (g_failures == 0)
        std::printf("test_view: ok\n");
    return g_failures == 0 ? 0 : 1;
}
