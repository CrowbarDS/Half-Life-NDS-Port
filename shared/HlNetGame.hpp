#pragma once

// A match between consoles: HlNet.hpp's frames and events, joined to the
// game.
//
// Each console runs the whole game for its own player, as it does alone, and
// this is what it adds each frame (netTick, after the game's own tick):
//
//   - says where its player is (the state in the next frame);
//   - turns what its player did to the others into events: a shot fired, a
//     hit on another player (a CLAIM: the shooter's console found it, with
//     the other's own hit boxes, and the victim's console takes it), a
//     grenade gone off, something of the map used or struck, a pickup
//     wanted, its own death;
//   - takes the others' events: is hurt by their claims, hears their shots,
//     sees their grenades burst, uses and strikes the map as they did;
//   - puts the others where they say they are: each an actor of the kind
//     Player, with the players' model, gliding between what is heard;
//   - has each of them walk into the map's triggers, doors and lifts here
//     too (logicGhostTouch), so that the same things move on every console.
//
// The host, besides, says who has a pickup when it is wanted, brings it
// back after its time, keeps the scores, and ends the match.
//
// std-only: the host tests play matches with it (test_net).

#include <cstdio>

#include "HlMonsters.hpp"
#include "HlNet.hpp"
#include "HlNetVote.hpp"
#include "HlRenew.hpp"

namespace hldata
{
    static_assert(kNetPlayers <= kMaxMatchPlayers, "a match's players are actors the map has room for");

    inline constexpr int kNetRespawnMs = 3000;   ///< dead this long, then back
    inline constexpr int kNetWeaponBackMs = 20000; ///< a weapon or ammunition taken is back after this
    inline constexpr int kNetItemBackMs = 30000;   ///< ... a medkit or a battery after this
    inline constexpr int kNetBlameMs = 5000;     ///< a death this soon after a hit is the hitter's
    /// A host's Bye, its first value: nothing, it has left; to one console,
    /// it will not have it (the game is full, or it has let it go); to all,
    /// it is off to send the game, and will be back.
    inline constexpr int kNetRefuseFull = 1, kNetRefuseKicked = 2, kNetByeBackSoon = 3, kNetRefuseLate = 4;

    inline constexpr int kNetSprayWaitMs = 30000; ///< a spray, then none until this long after (decalfrequency)
    inline constexpr int kNetSprayReach = 128;    ///< how far off a wall may be sprayed
    inline constexpr int kNetSprayHalf = 32;      ///< units from a spray's middle to its edge (64 across, as the game's)
    /// The health another player's actor is kept at: nobody dies of it, and
    /// what is missing from it is what this console's player did to them.
    inline constexpr std::int16_t kNetActorHealth = 20000;

    /// Where a player may come into the map (its info_player_deathmatch).
    struct NetSpawn
    {
        fx pos[3];
        std::uint16_t yaw;
    };

    /// Something for whoever draws the screen to say: who killed whom, who
    /// came, who went, the match over.
    struct NetNotice
    {
        NetEventType type = NetEventType::None;
        std::uint8_t who = 0;   ///< the player it is about
        std::uint8_t other = 0; ///< (a death: who did it; kNetPlayers: nobody else)
        std::uint8_t weapon = 0; ///< (a death by another: what they had in hand when it was heard)
    };

    /// The rules of a match, as its host set them (Rules).
    struct NetRules
    {
        std::uint8_t mode = kNetModeDeathmatch;
        std::uint16_t flags = kRuleAutoaim; ///< kRule*
        std::uint8_t most = 4;              ///< how many may play
        std::uint32_t mapHash = 0;          ///< nameHash of the map it is to be on (0: not said)
        std::uint8_t frags = 0, minutes = 0; ///< the limits, for the lobby to show (0: none)
        std::uint8_t chatSeconds = 10;       ///< how long the scores are shown (RulesMore)
        std::uint8_t nextMode = 0;           ///< how the next map is chosen (HlSettings.hpp's kNext*)
        bool totals = false;                 ///< a lobby's matches added up
        std::uint8_t voteSeconds = 10;       ///< how long a vote stays open
        bool teams() const { return mode == kNetModeTeamplay; }
        bool has(std::uint16_t flag) const { return (flags & flag) != 0; }
    };

    /// A player as the others see them (Profile, SprayPiece), and their team
    /// (Team: the host's to say).
    struct NetProfile
    {
        bool known = false; ///< their Profile has come
        std::uint8_t model = 0;
        std::uint8_t top = 30, bottom = 6; ///< the game's topcolor and bottomcolor
        std::uint8_t asked = 0;            ///< the team they would be on (0: either)
        std::uint8_t team = 0;             ///< the team they are on (0: none)
        std::uint8_t sprayColour = 0;      ///< HlSettings.hpp's kSprayColour
        std::uint32_t sprayHash = 0;       ///< netSprayHash of their spray (0: none)
        std::uint32_t pieces = 0;          ///< a bit for each piece of it that has come
        std::uint8_t spray[kNetSprayBytes] = {};
        bool sprayWhole() const
        {
            return sprayHash != 0 && pieces == kNetSprayAll && netSprayHash(spray) == sprayHash;
        }
    };

    /// A player's spray on a wall: their last.
    struct NetSprayMark
    {
        bool active = false;
        fx at[3] = {0, 0, 0};
        fx normal[3] = {0, 0, kFxOne}; ///< Q12: the way the wall faces
    };

    /// The scores as a match ended, kept as they were: who leaves stays on
    /// them, greyed, and who comes later is not on them.
    struct NetFinal
    {
        struct Row
        {
            char name[kNetTextBytes] = {};
            std::int16_t frags = 0, deaths = 0;
            std::uint8_t top = 30, bottom = 6, team = 0;
            bool gone = false;
        };
        Row rows[kNetPlayers];
        std::uint8_t order[kNetPlayers] = {}; ///< the players, the most frags first, then the fewest deaths
        std::uint8_t count = 0;
        std::uint8_t winnerTeam = 0;          ///< (teamplay) 0: level
        std::int16_t teamFrags[3] = {};
        /// Whom the match's facts are of (kNetPlayers: nobody), and how many.
        std::uint8_t mostKills = kNetPlayers, longestStreak = kNetPlayers, mostSuicides = kNetPlayers;
        std::uint8_t kills = 0, streak = 0, suicides = 0;
        /// Changes with whatever is shown of it.
        std::uint16_t stamp = 0;
    };

    struct NetMatch
    {
        enum class Role : std::uint8_t
        {
            Off,
            Host,
            Client,
        };
        Role role = Role::Off;
        int me = 0; ///< which player this console is
        NetHost host;
        NetClient client;

        char names[kNetPlayers][kNetTextBytes] = {};
        char myName[kNetTextBytes] = {};
        std::int16_t frags[kNetPlayers] = {};
        std::int16_t deaths[kNetPlayers] = {};

        bool playing = false; ///< a map is loaded and the match is on
        bool over = false;
        bool warned = false;  ///< the last minute has been reached
        bool warnDue = false; ///< ... and not yet said (whoever runs the screens clears it)
        NetFinal final;
        NetVote vote;
        char nextName[NetVote::kNameBytes] = {}; ///< the map that comes next, if it is known
        std::uint16_t endDraw = 0; ///< (the host's, said by End) which of a map's places to show it from
        // Counted from the deaths every console hears, for the match's facts.
        std::uint8_t kills[kNetPlayers] = {}, streaks[kNetPlayers] = {}, bestStreaks[kNetPlayers] = {},
                     suicides[kNetPlayers] = {};
        // A lobby's matches added up, seat by seat, as they end.
        std::int16_t sessionFrags[kNetPlayers] = {}, sessionDeaths[kNetPlayers] = {};
        int fragLimit = 0;    ///< 0: none
        int minuteLimit = 0;  ///< 0: none
        NetRules rules;
        /// Everyone as they look, and their team.
        NetProfile profiles[kNetPlayers];
        /// Each team's frags (teamplay): [1] and [2].
        std::int16_t teamFrags[3] = {};
        /// Counts the profiles and teams that have come: whoever shows them
        /// (the players' colours) looks again when it changes.
        std::uint32_t profileStamp = 0;
        /// This console's own (netSetProfile), until it has its place; then
        /// profiles[me] too.
        NetProfile myProfile;
        int sprayNext = kNetSprayPieces; ///< the next piece of this console's spray to send
        /// The host: of each player's spray, the pieces it has passed on.
        std::uint32_t relayed[kNetPlayers] = {};
        /// The host would not let this console in (too many).
        /// The host would not have this console: why (kNetRefuse*; 0: it would).
        /// Each player's model, if not `model` (0: is `model`): who they chose
        /// to be, where this console has that one.
        std::uint16_t characterModel[kNetPlayers] = {};
        std::uint8_t refused = 0;
        /// The host's: those it has let go (their tokens), who are not let
        /// back in while its game lasts.
        std::uint16_t banned[8] = {};
        NetSprayMark sprays[kNetPlayers];
        std::int32_t sprayWaitMs = 0; ///< until this console's player may spray again
        std::int32_t playedMs = 0;

        // The others, here.
        int firstActor = -1; ///< player p is actor firstActor + p
        int model = -1;      ///< the players' model, in the map's ModelRefs
        NetGlide glide[kNetPlayers];
        std::uint8_t pose[kNetPlayers] = {}; ///< which clip each is in (kPose*)

        // This console's own player.
        NetPlayer mine;
        std::uint8_t spawns = 0;
        bool wasAlive = false;
        std::int32_t deadMs = 0;
        int blamed = kNetPlayers;  ///< who last hit them
        std::int32_t blameMs = 0;  ///< ... and for how much longer that counts
        /// Set when the player has just been put back into the map: the
        /// console puts the view and the weapon in hand to rights, and
        /// clears it.
        bool respawned = false;

        const NetSpawn *spawnPoints = nullptr;
        int spawnCount = 0;
        std::uint32_t seed = 1;

        // Grenades in flight a tick ago: one that is gone has gone off.
        bool shotWas[kMaxProjectiles] = {};
        fx shotAt[kMaxProjectiles][3] = {};

        // The host: what has been taken, and when it is back.
        struct Taken
        {
            std::int16_t actor = -1;
            std::int32_t ms = 0;
        };
        static constexpr int kMostTaken = 96;
        Taken taken[kMostTaken];

        static constexpr int kMostNotices = 8;
        NetNotice notices[kMostNotices];
        int noticeCount = 0;
        /// Events the match itself does nothing with (the lobby's: a start,
        /// an end, a goodbye), for whoever runs the screens.
        NetEvent passed[kMostNotices];
        int passedCount = 0;
    };

    inline constexpr std::uint8_t kPoseIdle = 1;
    inline constexpr std::uint8_t kPoseWalk = 2;
    inline constexpr std::uint8_t kPoseRun = 3;
    inline constexpr std::uint8_t kPoseCrouch = 4;
    inline constexpr std::uint8_t kPoseCrawl = 5;
    inline constexpr std::uint8_t kPoseJump = 6;
    inline constexpr std::uint8_t kPoseDead = 7;

    /// Player `p` as last heard (this console's own: as it last said).
    inline const NetPlayer &netPlayer(const NetMatch &match, int p)
    {
        if (p == match.me)
            return match.mine;
        return match.role == NetMatch::Role::Host ? match.host.players[p] : match.client.players[p];
    }
    /// Whether this console has its place in the game: the host always; a
    /// client once the host has said which player it is.
    inline bool netPlaced(const NetMatch &match)
    {
        return match.role == NetMatch::Role::Host || (match.role == NetMatch::Role::Client && match.me != 0);
    }
    inline bool netHere(const NetMatch &match, int p)
    {
        return p == match.me || (netPlayer(match, p).flags & kNetHere) != 0;
    }

    /// Say something to everyone.
    inline void netSay(NetMatch &match, NetEvent event)
    {
        event.from = static_cast<std::uint8_t>(match.me);
        if (match.role == NetMatch::Role::Host)
            match.host.out.add(event);
        else if (match.role == NetMatch::Role::Client)
            match.client.out.add(event);
    }
    inline NetEvent netEvent(NetEventType type, int a = 0, int v0 = 0, int v1 = 0, int v2 = 0, int v3 = 0,
                             int v4 = 0, int v5 = 0)
    {
        NetEvent e;
        e.type = type;
        e.a = static_cast<std::uint8_t>(a);
        const int v[6] = {v0, v1, v2, v3, v4, v5};
        for (int i = 0; i < 6; ++i)
            e.v[i] = static_cast<std::int16_t>(v[i]);
        return e;
    }

    /// Whether `a` and `b` are on one team, and may not hurt each other.
    inline bool netTeammates(const NetMatch &match, int a, int b)
    {
        return match.rules.teams() && !match.rules.has(kRuleFriendlyFire) && a != b && a >= 0 && b >= 0 &&
               a < kNetPlayers && b < kNetPlayers && match.profiles[a].team != 0 &&
               match.profiles[a].team == match.profiles[b].team;
    }

    /// What a Profile says of `profile`.
    inline NetEvent netProfileEvent(const NetProfile &profile)
    {
        return netEvent(NetEventType::Profile, 0, profile.model, profile.top | profile.bottom << 8, profile.asked,
                        profile.sprayColour, static_cast<int>(profile.sprayHash & 0xFFFF),
                        static_cast<int>(profile.sprayHash >> 16));
    }
    /// Piece `piece` of `profile`'s spray, as a SprayPiece.
    inline NetEvent netSprayPiece(const NetProfile &profile, int piece)
    {
        NetEvent e = netEvent(NetEventType::SprayPiece, piece);
        for (int i = 0; i < 6; ++i)
        {
            const int at = piece * 12 + i * 2;
            const int low = at < kNetSprayBytes ? profile.spray[at] : 0;
            const int high = at + 1 < kNetSprayBytes ? profile.spray[at + 1] : 0;
            e.v[i] = static_cast<std::int16_t>(low | high << 8);
        }
        return e;
    }
    /// A wall's facing (Q12) as Spray's v[3..4]: ten bits each of x, y and
    /// z, in 512ths.
    inline void netPackNormal(const fx *normal, std::int16_t &low, std::int16_t &high)
    {
        std::uint32_t bits = 0;
        for (int i = 0; i < 3; ++i)
        {
            int n = normal[i] >> 3;
            n = n < -511 ? -511 : (n > 511 ? 511 : n);
            bits |= (static_cast<std::uint32_t>(n) & 0x3FF) << (i * 10);
        }
        low = static_cast<std::int16_t>(bits & 0xFFFF);
        high = static_cast<std::int16_t>(bits >> 16);
    }
    inline void netUnpackNormal(std::int16_t low, std::int16_t high, fx *normal)
    {
        const std::uint32_t bits = static_cast<std::uint16_t>(low) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(high)) << 16;
        for (int i = 0; i < 3; ++i)
        {
            int n = static_cast<int>((bits >> (i * 10)) & 0x3FF);
            n = n >= 512 ? n - 1024 : n;
            normal[i] = n << 3;
        }
    }

    namespace detail
    {
        inline void netNotice(NetMatch &match, NetEventType type, int who, int other = kNetPlayers)
        {
            if (match.noticeCount >= NetMatch::kMostNotices)
                return;
            NetNotice &notice = match.notices[match.noticeCount++];
            notice.type = type;
            notice.who = static_cast<std::uint8_t>(who);
            notice.other = static_cast<std::uint8_t>(other);
            // (What the killer holds as it is heard: a grenade thrown just
            // before they changed weapon is put down to the new one.)
            notice.weapon = type == NetEventType::Died && other < kNetPlayers && other != who
                                ? netPlayer(match, other).weapon
                                : static_cast<std::uint8_t>(0);
        }

        /// Where player `p` is, as someone the map's logic can be asked
        /// about: the middle of them, standing or crouched.
        inline PlayerState netGhost(const NetMatch &match, const LogicWorld &w, int p)
        {
            const NetPlayer &heard = netPlayer(match, p);
            PlayerState ghost;
            for (int i = 0; i < 3; ++i)
            {
                ghost.origin[i] = p == match.me ? heard.pos[i] * 1024 : match.glide[p].pos[i];
                ghost.velocity[i] = heard.velocity[i] * 8 * kFxOne;
            }
            ghost.ducked = (heard.flags & kNetCrouched) != 0;
            ghost.onGround = (heard.flags & kNetOnGround) != 0;
            ghost.health = 100;
            fx sine, cosine;
            fxSinCos(heard.yaw, sine, cosine);
            ghost.facingXY[0] = cosine;
            ghost.facingXY[1] = sine;
            // What they stand on, for the lifts: looked for under them.
            if (ghost.onGround && w.world != nullptr)
            {
                const fx below[3] = {ghost.origin[0], ghost.origin[1], ghost.origin[2] - fxFromInt(4)};
                ghost.groundMover = traceAll(*w.world, ghost.ducked, ghost.origin, below).mover;
            }
            return ghost;
        }

        /// A death, as every console hears it: for the match's facts.
        inline void netCount(NetMatch &match, int victim, int killer)
        {
            if (match.over || victim < 0 || victim >= kNetPlayers)
                return;
            match.streaks[victim] = 0;
            if (killer >= kNetPlayers || killer == victim)
            {
                ++match.suicides[victim];
                return;
            }
            ++match.kills[killer];
            if (++match.streaks[killer] > match.bestStreaks[killer])
                match.bestStreaks[killer] = match.streaks[killer];
        }

        /// The match has just ended: the scores kept as they stand.
        inline void netFinalTake(NetMatch &match)
        {
            NetFinal &final = match.final;
            const std::uint16_t stamp = final.stamp;
            final = NetFinal();
            final.stamp = static_cast<std::uint16_t>(stamp + 1);
            for (int p = 0; p < kNetPlayers; ++p)
            {
                if (!netHere(match, p))
                    continue;
                NetFinal::Row &row = final.rows[p];
                std::memcpy(row.name, match.names[p], kNetTextBytes);
                row.frags = match.frags[p];
                row.deaths = match.deaths[p];
                const NetProfile &profile = match.profiles[p];
                row.top = profile.known ? profile.top : 30;
                row.bottom = profile.known ? profile.bottom : 6;
                row.team = profile.team;
                int at = final.count++;
                for (; at > 0; --at)
                {
                    const NetFinal::Row &other = final.rows[final.order[at - 1]];
                    if (other.frags > row.frags || (other.frags == row.frags && other.deaths <= row.deaths))
                        break;
                    final.order[at] = final.order[at - 1];
                }
                final.order[at] = static_cast<std::uint8_t>(p);
                if (match.rules.totals)
                {
                    match.sessionFrags[p] = static_cast<std::int16_t>(match.sessionFrags[p] + row.frags);
                    match.sessionDeaths[p] = static_cast<std::int16_t>(match.sessionDeaths[p] + row.deaths);
                }
                if (match.kills[p] > final.kills)
                {
                    final.kills = match.kills[p];
                    final.mostKills = static_cast<std::uint8_t>(p);
                }
                if (match.bestStreaks[p] > final.streak)
                {
                    final.streak = match.bestStreaks[p];
                    final.longestStreak = static_cast<std::uint8_t>(p);
                }
                if (match.suicides[p] > final.suicides)
                {
                    final.suicides = match.suicides[p];
                    final.mostSuicides = static_cast<std::uint8_t>(p);
                }
            }
            final.teamFrags[1] = match.teamFrags[1];
            final.teamFrags[2] = match.teamFrags[2];
            if (match.rules.teams() && final.teamFrags[1] != final.teamFrags[2])
                final.winnerTeam = final.teamFrags[1] > final.teamFrags[2] ? 1 : 2;
        }

        /// The host: how many have chosen map `slot`, to everyone.
        inline void netSayTally(NetMatch &match, int slot)
        {
            if (slot >= 0)
                match.host.out.add(netEvent(NetEventType::NextTally, slot, match.vote.tally[slot]));
        }

        /// Who has gone since it ended: greyed where they are.
        inline void netFinalWatch(NetMatch &match)
        {
            NetFinal &final = match.final;
            for (int n = 0; n < final.count; ++n)
            {
                NetFinal::Row &row = final.rows[final.order[n]];
                if (!row.gone && !netHere(match, final.order[n]))
                {
                    row.gone = true;
                    ++final.stamp;
                }
            }
        }

        /// The host: player `victim` has died, by `killer` (or by nobody
        /// else). The scores, to everyone.
        inline void netScore(NetMatch &match, int victim, int killer)
        {
            // (The match over, the scores are final: a death still on its way
            // counts for nothing.)
            if (match.over)
                return;
            ++match.deaths[victim];
            // (In teamplay, a teammate killed costs the killer a frag, as a
            // death by one's own hand does: the game's teamplay rules.)
            const bool teamkill = match.rules.teams() && killer < kNetPlayers && killer != victim &&
                                  match.profiles[killer].team != 0 &&
                                  match.profiles[killer].team == match.profiles[victim].team;
            if (killer < kNetPlayers && killer != victim)
                match.frags[killer] = static_cast<std::int16_t>(match.frags[killer] + (teamkill ? -1 : 1));
            else
                --match.frags[victim];
            match.teamFrags[1] = match.teamFrags[2] = 0;
            for (int p = 0; p < kNetPlayers; ++p)
            {
                const int team = match.profiles[p].team;
                if (match.rules.teams() && (team == 1 || team == 2) && netHere(match, p))
                    match.teamFrags[team] = static_cast<std::int16_t>(match.teamFrags[team] + match.frags[p]);
            }
            for (int p : {victim, killer})
            {
                if (p < kNetPlayers)
                    match.host.out.add(netEvent(NetEventType::Score, p, match.frags[p], match.deaths[p],
                                                match.teamFrags[1], match.teamFrags[2]));
            }
        }

        /// The host: the team player `p` is to play for -- what they asked,
        /// if that leaves the teams no more than one apart; else the smaller
        /// (the first, if they are as big) -- told to everyone.
        inline void netPickTeam(NetMatch &match, int p)
        {
            int team = 0;
            if (match.rules.teams())
            {
                int count[3] = {0, 0, 0};
                for (int q = 0; q < kNetPlayers; ++q)
                {
                    const int t = match.profiles[q].team;
                    if (q != p && (t == 1 || t == 2) && netHere(match, q))
                        ++count[t];
                }
                const int asked = match.profiles[p].asked;
                if ((asked == 1 || asked == 2) && count[asked] <= count[3 - asked])
                    team = asked;
                else
                    team = count[2] < count[1] ? 2 : 1;
            }
            match.profiles[p].team = static_cast<std::uint8_t>(team);
            ++match.profileStamp;
            match.host.out.add(netEvent(NetEventType::Team, p, team));
        }

        /// The host: everything a console that has just come needs to know
        /// of the others -- the rules, everyone's profile and team, their
        /// sprays again -- to everyone (those that had it lose nothing).
        inline void netTellAll(NetMatch &match)
        {
            NetEvent rules = netEvent(NetEventType::Rules, 0, match.rules.mode, match.rules.flags, match.rules.most,
                                    static_cast<int>(match.rules.mapHash & 0xFFFF), static_cast<int>(match.rules.mapHash >> 16),
                                    match.rules.frags | match.rules.minutes << 8);
            match.host.out.add(netEvent(NetEventType::RulesMore, 0, match.rules.chatSeconds, match.rules.nextMode,
                                        match.rules.totals ? 1 : 0, match.rules.voteSeconds));
            match.host.out.add(rules);
            for (int p = 0; p < kNetPlayers; ++p)
            {
                if (!match.profiles[p].known || !netHere(match, p))
                    continue;
                NetEvent profile = netProfileEvent(match.profiles[p]);
                profile.from = static_cast<std::uint8_t>(p);
                match.host.out.add(profile);
                match.host.out.add(netEvent(NetEventType::Team, p, match.profiles[p].team));
                match.relayed[p] = 0;
            }
            // (The host's own spray: from its first piece again.)
            if (match.myProfile.sprayHash != 0)
                match.sprayNext = 0;
        }

        /// A player's spray is put on the wall at `at`, facing `normal`: in
        /// the place of their last.
        inline void netSprayPlace(NetMatch &match, int p, const fx *at, const fx *normal)
        {
            if (p < 0 || p >= kNetPlayers)
                return;
            NetSprayMark &mark = match.sprays[p];
            mark.active = true;
            for (int i = 0; i < 3; ++i)
            {
                mark.at[i] = at[i];
                mark.normal[i] = normal[i];
            }
        }

        /// The host: the item that is actor `index` is `who`'s; it is back
        /// after its time.
        inline void netGrant(NetMatch &match, LogicWorld &w, PlayerState &player, int index, int who)
        {
            ActorWorld &a = *w.actors;
            if (index < 0 || index >= a.count || !a.states[index].active ||
                a.states[index].kind != static_cast<std::uint8_t>(ActorKind::Item))
                return; // (someone else has it already)
            const auto item = static_cast<ItemKind>(a.states[index].item);
            // (Weapons stay: a weapon is given, and is still there for the
            // next -- who has not got it: HlMonsters.hpp asks only then.)
            const bool stays = match.rules.has(kRuleWeaponsStay) && itemViewModel(item) != nullptr;
            if (who == match.me)
                itemPickUp(w, player, index);
            a.states[index].active = stays;
            actorPlaceBox(a, index);
            match.host.out.add(netEvent(NetEventType::Taken, who, index, stays ? 1 : 0));
            if (stays)
                return;
            for (NetMatch::Taken &slot : match.taken)
            {
                if (slot.actor >= 0)
                    continue;
                slot.actor = static_cast<std::int16_t>(index);
                slot.ms = item == ItemKind::HealthKit || item == ItemKind::Battery ? kNetItemBackMs : kNetWeaponBackMs;
                break;
            }
        }

        /// An event from another console, taken here.
        inline void netApply(NetMatch &match, LogicWorld &w, PlayerState &player, const NetEvent &event)
        {
            // (No map is loaded in the lobby: `playing` guards everything
            // that touches one.)
            const int from = event.from;
            const bool host = match.role == NetMatch::Role::Host;
            switch (event.type)
            {
            case NetEventType::Hello:
                if (host && from > 0 && from < kNetPlayers)
                {
                    // (More than the rules let play: told so, and nothing more.
                    // Counted as they came: the host and those it has let in,
                    // whom it knows by name.)
                    int here = 2;
                    for (int p = 1; p < kNetPlayers; ++p)
                        here += p != from && match.names[p][0] != 0 && netHere(match, p) ? 1 : 0;
                    if (here > match.rules.most)
                    {
                        match.host.out.add(netEvent(NetEventType::Bye, from, kNetRefuseFull));
                        break;
                    }
                    // (One it has let go, come back.)
                    bool banned = false;
                    for (const std::uint16_t token : match.banned)
                        banned = banned || (token != 0 && token == match.host.clients[from].token);
                    if (banned)
                    {
                        match.host.out.add(netEvent(NetEventType::Bye, from, kNetRefuseKicked));
                        break;
                    }
                    std::memcpy(match.names[from], event.text, kNetTextBytes);
                    match.names[from][kNetTextBytes - 1] = 0;
                    // Everyone's name to everyone: the newcomer has none yet.
                    for (int p = 0; p < kNetPlayers; ++p)
                    {
                        if (!netHere(match, p) && p != from)
                            continue;
                        NetEvent roster;
                        roster.type = NetEventType::Roster;
                        roster.a = static_cast<std::uint8_t>(p);
                        std::memcpy(roster.text, match.names[p], kNetTextBytes);
                        match.host.out.add(roster);
                    }
                    netTellAll(match);
                    netNotice(match, NetEventType::Hello, from);
                }
                break;
            case NetEventType::Roster:
                if (!host && event.a < kNetPlayers)
                {
                    std::memcpy(match.names[event.a], event.text, kNetTextBytes);
                    match.names[event.a][kNetTextBytes - 1] = 0;
                }
                break;
            case NetEventType::Shot:
                if (from != match.me && from < kNetPlayers && match.playing && w.heard != nullptr &&
                    event.a > kWeaponCrowbar && event.a < kWeaponCount && event.a != kWeaponGrenade &&
                    event.a != kWeaponEgon && event.a != kWeaponSatchel && event.a != kWeaponTripmine)
                {
                    // (Its own. Not the egon's, which fires a dozen times a
                    // second, nor what is only put down.)
                    const CommonSound shot = event.a == kWeaponGauss       ? CommonSound::GaussFire
                                             : event.a == kWeaponRpg       ? CommonSound::RocketFire
                                             : event.a == kWeaponHornetgun ? CommonSound::HornetFire1
                                             : event.a == kWeaponSnark     ? CommonSound::SnarkDeploy
                                                                           : gunKind(event.a).fire[0];
                    w.heard->add(shot, match.glide[from].pos, 110, kRadiusLarge);
                }
                break;
            case NetEventType::Hit:
                if (event.a == match.me && from != match.me && match.playing && !match.over && player.health > 0 &&
                    static_cast<std::uint8_t>(event.v[1]) == match.spawns && !netTeammates(match, from, match.me))
                {
                    playerHurt(player, event.v[0], from < kNetPlayers ? match.glide[from].pos : nullptr);
                    match.blamed = from;
                    match.blameMs = kNetBlameMs;
                }
                break;
            case NetEventType::Burst:
                if (from != match.me && match.playing && !match.over)
                {
                    const fx at[3] = {event.v[0] * 1024, event.v[1] * 1024, event.v[2] * 1024};
                    explodeAt(w, player, at, 100, true, true, false);
                }
                break;
            case NetEventType::Died:
                if (from != match.me && !match.over)
                {
                    netNotice(match, NetEventType::Died, from, event.a);
                    netCount(match, from, event.a);
                }
                if (host)
                    netScore(match, from, event.a);
                break;
            case NetEventType::Score:
                if (!host && event.a < kNetPlayers)
                {
                    match.frags[event.a] = event.v[0];
                    match.deaths[event.a] = event.v[1];
                    match.teamFrags[1] = event.v[2];
                    match.teamFrags[2] = event.v[3];
                }
                break;
            case NetEventType::Rules:
                if (!host)
                {
                    match.rules.mode = static_cast<std::uint8_t>(event.v[0]);
                    match.rules.flags = static_cast<std::uint16_t>(event.v[1]);
                    match.rules.most = static_cast<std::uint8_t>(event.v[2] < 2 ? 2 : (event.v[2] > kNetPlayers ? kNetPlayers : event.v[2]));
                    match.rules.mapHash = static_cast<std::uint16_t>(event.v[3]) |
                                          static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[4])) << 16;
                    match.rules.frags = static_cast<std::uint8_t>(event.v[5] & 0xFF);
                    match.rules.minutes = static_cast<std::uint8_t>((event.v[5] >> 8) & 0xFF);
                    ++match.profileStamp; // (what the lobby shows has changed)
                }
                break;
            case NetEventType::NextDecided:
                if (!host)
                {
                    std::memcpy(match.nextName, event.text, kNetTextBytes);
                    match.nextName[kNetTextBytes - 1] = 0;
                    // (By a vote: all of its name is known.)
                    if (event.a >= 1 && event.a <= match.vote.count)
                        std::memcpy(match.nextName, match.vote.names[event.a - 1], NetVote::kNameBytes);
                    ++match.final.stamp;
                    match.vote.decided = static_cast<std::int8_t>(event.a - 1);
                    ++match.vote.stamp;
                }
                break;
            case NetEventType::NextOpen:
                if (!host)
                {
                    match.vote.open = true;
                    ++match.vote.stamp;
                }
                break;
            case NetEventType::NextMap:
                // (The vote given up, before it came to anything.)
                if (!host && event.a == kNetVoteCancel)
                    match.vote = NetVote();
                else if (!host)
                    match.vote.setName(event.a & ~kNetVoteNameRest, (event.a & kNetVoteNameRest) != 0, event.text);
                break;
            case NetEventType::NextPick:
                if (host && match.vote.cast(from, event.a))
                    netSayTally(match, event.a);
                break;
            case NetEventType::NextTally:
                if (!host)
                {
                    if (event.a < NetVote::kMost)
                        match.vote.tally[event.a] = static_cast<std::uint8_t>(event.v[0]);
                    ++match.vote.stamp;
                }
                break;
            case NetEventType::RulesMore:
                if (!host)
                {
                    match.rules.chatSeconds = static_cast<std::uint8_t>(event.v[0] < 5 ? 5 : (event.v[0] > 30 ? 30 : event.v[0]));
                    match.rules.nextMode = static_cast<std::uint8_t>(event.v[1]);
                    match.rules.totals = event.v[2] != 0;
                    match.rules.voteSeconds = static_cast<std::uint8_t>(event.v[3] < 5 ? 5 : (event.v[3] > 150 ? 150 : event.v[3]));
                }
                break;
            case NetEventType::Profile:
                if (from < kNetPlayers)
                {
                    NetProfile &profile = match.profiles[from];
                    const std::uint32_t hash =
                        static_cast<std::uint16_t>(event.v[4]) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(event.v[5])) << 16;
                    if (hash != profile.sprayHash)
                        profile.pieces = 0;
                    profile.known = true;
                    profile.model = static_cast<std::uint8_t>(event.v[0]);
                    profile.top = static_cast<std::uint8_t>(event.v[1] & 0xFF);
                    profile.bottom = static_cast<std::uint8_t>((event.v[1] >> 8) & 0xFF);
                    profile.asked = static_cast<std::uint8_t>(event.v[2]);
                    profile.sprayColour = static_cast<std::uint8_t>(event.v[3]);
                    profile.sprayHash = hash;
                    ++match.profileStamp;
                    if (host && from != match.me)
                        netPickTeam(match, from);
                }
                break;
            case NetEventType::SprayPiece:
                if (from < kNetPlayers && event.a < kNetSprayPieces)
                {
                    NetProfile &profile = match.profiles[from];
                    for (int i = 0; i < 12; ++i)
                    {
                        const int at = event.a * 12 + i;
                        if (at < kNetSprayBytes)
                            profile.spray[at] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(event.v[i / 2]) >> ((i & 1) * 8));
                    }
                    profile.pieces |= 1u << event.a;
                    ++match.profileStamp;
                }
                break;
            case NetEventType::Team:
                if (!host && event.a < kNetPlayers)
                {
                    match.profiles[event.a].team = static_cast<std::uint8_t>(event.v[0]);
                    ++match.profileStamp;
                }
                break;
            case NetEventType::Spray:
                if (from != match.me && from < kNetPlayers && match.playing)
                {
                    const fx at[3] = {event.v[0] * 1024, event.v[1] * 1024, event.v[2] * 1024};
                    fx normal[3];
                    netUnpackNormal(event.v[3], event.v[4], normal);
                    netSprayPlace(match, from, at, normal);
                }
                break;
            case NetEventType::Take:
                if (host && match.playing)
                    netGrant(match, w, player, event.v[0], from);
                break;
            case NetEventType::Taken:
                if (!host && match.playing && event.v[0] >= 0 && event.v[0] < w.actors->count)
                {
                    ActorWorld &a = *w.actors;
                    if (event.a == match.me && a.states[event.v[0]].active)
                        itemPickUp(w, player, event.v[0]);
                    // (v[1]: weapons stay, and so does this.)
                    a.states[event.v[0]].active = event.v[1] != 0;
                    actorPlaceBox(a, event.v[0]);
                }
                break;
            case NetEventType::Back:
                if (!host && match.playing && event.v[0] >= 0 && event.v[0] < w.actors->count)
                {
                    w.actors->states[event.v[0]].active = true;
                    actorPlaceBox(*w.actors, event.v[0]);
                }
                break;
            case NetEventType::Fire:
                if (from != match.me && from < kNetPlayers && match.playing && event.v[0] >= 0 &&
                    event.v[0] < w.recordCount)
                {
                    // As they did it, by someone standing where they stand;
                    // and nothing of it told back.
                    PlayerState ghost = netGhost(match, w, from);
                    const bool sharing = w.sharing;
                    w.sharing = false;
                    if (event.a == 0)
                        logicUse(w, ghost, event.v[0], -1, kUseSet);
                    else
                        logicStrike(w, ghost, event.v[0], event.v[1]);
                    w.sharing = sharing;
                }
                break;
            case NetEventType::Bye:
                // (The host's to a client it will not have: for that one
                // alone, and the match goes on for the rest.)
                if (from == 0 && (event.v[0] == kNetRefuseFull || event.v[0] == kNetRefuseKicked || event.v[0] == kNetRefuseLate))
                {
                    if (event.a != match.me || match.me == 0)
                        break;
                    match.refused = static_cast<std::uint8_t>(event.v[0]);
                }
                if (match.passedCount < NetMatch::kMostNotices)
                    match.passed[match.passedCount++] = event;
                break;
            case NetEventType::End:
                // (v[0] 1: given up, with no scores to show.)
                if (!match.over && event.v[0] != 1)
                {
                    match.endDraw = static_cast<std::uint16_t>(event.v[1]);
                    netFinalTake(match);
                }
                if (!match.over)
                    netNotice(match, NetEventType::End, 0);
                match.over = true;
                [[fallthrough]];
            case NetEventType::Start:
            case NetEventType::Ready:
            case NetEventType::Go:
            case NetEventType::Cancel:
            case NetEventType::Room:
            case NetEventType::Fetch:
            case NetEventType::FetchHave:
            case NetEventType::FetchAgain:
            case NetEventType::FetchNone:
                if (match.passedCount < NetMatch::kMostNotices)
                    match.passed[match.passedCount++] = event;
                break;
            default:
                break;
            }
        }
    } // namespace detail

    // ---------------------------------------------------------- the frames

    /// This console's frame for the air; 0 if there is none to send.
    inline int netWrite(NetMatch &match, std::uint8_t *bytes)
    {
        if (match.role == NetMatch::Role::Host)
            return netHostWrite(match.host, match.mine, bytes);
        if (match.role == NetMatch::Role::Client)
            return netClientWrite(match.client, match.mine, bytes);
        return 0;
    }
    /// A frame from the air: from client `who`, if this is the host (the
    /// radio says which).
    inline void netRead(NetMatch &match, const std::uint8_t *bytes, int size, int who = 0)
    {
        if (match.role == NetMatch::Role::Host)
            netHostRead(match.host, who, bytes, size);
        else if (match.role == NetMatch::Role::Client)
            netClientRead(match.client, bytes, size);
    }

    // ------------------------------------------------------------ the match

    /// Become the host, or a client. A client is told by the host which
    /// player it is; `token` is what it calls itself until then (anything
    /// but 0, and not what another client is likely to choose).
    inline void netBegin(NetMatch &match, NetMatch::Role role, std::uint16_t token, const char *name)
    {
        renew(match);
        match.role = role;
        match.me = 0;
        match.client.token = token != 0 ? token : 1;
        match.mine.flags = kNetHere;
        std::strncpy(match.myName, name, kNetTextBytes - 1);
        std::strncpy(match.names[0], role == NetMatch::Role::Host ? name : "", kNetTextBytes - 1);
        if (role == NetMatch::Role::Client)
        {
            NetEvent hello;
            hello.type = NetEventType::Hello;
            std::strncpy(hello.text, name, kNetTextBytes - 1);
            netSay(match, hello);
        }
    }

    /// The local player is put into the map, at one of its places to come
    /// in at, as they are when the game starts them.
    inline void netSpawn(NetMatch &match, LogicWorld &w, PlayerState &player)
    {
        // Any of the places nobody is near, by chance (as the game does);
        // if there is someone near every one, the one they are farthest
        // from.
        int best = -1, clear = 0;
        std::int64_t bestNearest = -1;
        for (int s = 0; s < match.spawnCount; ++s)
        {
            std::int64_t nearest = INT64_MAX / 2;
            for (int p = 0; p < kNetPlayers; ++p)
            {
                if (p == match.me || !(netPlayer(match, p).flags & kNetAlive))
                    continue;
                std::int64_t far = 0;
                for (int i = 0; i < 3; ++i)
                {
                    const std::int64_t d = fxToInt(match.spawnPoints[s].pos[i]) - netPlayer(match, p).pos[i] / 4;
                    far += d * d;
                }
                nearest = far < nearest ? far : nearest;
            }
            if (nearest >= 384 * 384)
            {
                // (Each clear place in turn has one chance in as many as
                // there have been of being the one: any of them, evenly.)
                match.seed = match.seed * 1664525u + 1013904223u;
                if (((match.seed >> 16) % static_cast<std::uint32_t>(++clear)) == 0)
                    best = s;
            }
            else if (clear == 0 && nearest > bestNearest)
            {
                bestNearest = nearest;
                best = s;
            }
        }
        {
            // (A new life carries nothing of the last: the later weapons
            // neither -- but they are still the game's to keep.)
            Arsenal *keep = player.arsenal;
            const Cheats *cheats = player.cheats;
            player = PlayerState();
            player.arsenal = keep;
            player.cheats = cheats;
            if (keep != nullptr)
                *keep = Arsenal();
        }
        if (best >= 0)
        {
            for (int i = 0; i < 3; ++i)
                player.origin[i] = match.spawnPoints[best].pos[i];
            fx sine, cosine;
            fxSinCos(match.spawnPoints[best].yaw, sine, cosine);
            player.facingXY[0] = cosine;
            player.facingXY[1] = sine;
        }
        player.inv.have = static_cast<std::uint8_t>(1u | (1u << kWeaponCrowbar) | (1u << kWeaponGlock));
        player.inv.current = kWeaponGlock;
        player.inv.clipGlock = 17;
        player.inv.ammo9mm = 68;
        ++match.spawns;
        match.wasAlive = true;
        match.deadMs = 0;
        match.blamed = kNetPlayers;
        match.blameMs = 0;
        match.respawned = true;
        if (w.actors != nullptr)
            w.actors->wanted = -1;
        netSay(match, netEvent(NetEventType::Spawned));
    }

    /// The map is loaded: the match begins on this console. `firstActor` is
    /// the first of kNetPlayers actor slots that are the map's to spare,
    /// `model` the players' model in its ModelRefs (-1: it has none, and
    /// the others are not seen).
    inline void netStart(NetMatch &match, LogicWorld &w, PlayerState &player, int firstActor, int model,
                         const NetSpawn *spawns, int spawnCount, std::uint32_t seed)
    {
        ActorWorld &a = *w.actors;
        match.firstActor = firstActor;
        match.model = model;
        match.spawnPoints = spawns;
        match.spawnCount = spawnCount;
        match.seed = seed | 1;
        match.playing = true;
        player.suit.value.silent = true;
        match.over = false;
        match.warned = match.warnDue = false;
        match.nextName[0] = 0;
        match.vote = NetVote();
        match.playedMs = 0;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            match.kills[p] = match.streaks[p] = match.bestStreaks[p] = match.suicides[p] = 0;
            // (A seat someone else has taken, or nobody: its totals are not theirs.)
            if (!netHere(match, p) || std::strncmp(match.final.rows[p].name, match.names[p], kNetTextBytes) != 0)
                match.sessionFrags[p] = match.sessionDeaths[p] = 0;
        }
        for (int p = 0; p < kNetPlayers; ++p)
        {
            match.frags[p] = match.deaths[p] = 0;
            match.glide[p] = NetGlide();
            match.pose[p] = 0;
        }
        for (NetMatch::Taken &slot : match.taken)
            slot = NetMatch::Taken();
        a.shared = true;
        a.wanted = -1;
        w.sharing = true;
        w.weaponsStay.value = match.rules.has(kRuleWeaponsStay);
        w.flatFalls.value = !match.rules.has(kRuleRealisticFall);
        w.lampBanned.value = !match.rules.has(kRuleFlashlight);
        w.matchAutoaim.value = match.rules.has(kRuleAutoaim) ? 1 : 0;
        match.sprayWaitMs = 0;
        for (NetSprayMark &mark : match.sprays)
            mark = NetSprayMark();
        for (int t = 0; t < 3; ++t)
            match.teamFrags[t] = 0;
        w.struckCount = 0;
        // Nobody of the map's own is in a match: no monsters, no people.
        // What lies about to be taken stays, and the furniture.
        for (int i = 0; i < a.count; ++i)
        {
            ActorState &actor = a.states[i];
            const bool stays = actor.kind == static_cast<std::uint8_t>(ActorKind::Item) ||
                               actor.kind == static_cast<std::uint8_t>(ActorKind::Furniture);
            if (actor.active && !stays)
            {
                actor.active = false;
                actorPlaceBox(a, i);
            }
        }
        for (int s = 0; s < kMaxProjectiles; ++s)
            match.shotWas[s] = false;
        netSpawn(match, w, player);
    }

    /// The local player used the map's record `index` (a button, a door, a
    /// charger): the others are told.
    inline void netUsed(NetMatch &match, int index)
    {
        if (match.playing)
            netSay(match, netEvent(NetEventType::Fire, 0, index));
    }

    /// How this console's player looks (after netBegin): to everyone once it
    /// has its place, with its spray (`spray`: kNetSprayBytes; null, none).
    inline void netSetProfile(NetMatch &match, NetProfile profile, const std::uint8_t *spray)
    {
        profile.known = true;
        profile.team = match.profiles[match.me].team;
        if (spray != nullptr)
        {
            std::memcpy(profile.spray, spray, kNetSprayBytes);
            profile.sprayHash = netSprayHash(profile.spray);
            profile.pieces = kNetSprayAll;
        }
        else
        {
            profile.sprayHash = 0;
            profile.pieces = 0;
        }
        match.myProfile = profile;
        if (!netPlaced(match))
            return; // (said once the host has said who this console is)
        match.profiles[match.me] = profile;
        ++match.profileStamp;
        netSay(match, netProfileEvent(profile));
        match.sprayNext = spray != nullptr ? 0 : kNetSprayPieces;
        if (match.role == NetMatch::Role::Host)
            detail::netPickTeam(match, match.me);
    }

    /// The host: the rules (before Start; again whenever they change). The
    /// mode changed, the teams are picked again, every player in turn; else
    /// only who has none yet is given one.
    inline void netSetRules(NetMatch &match, const NetRules &rules)
    {
        if (match.role != NetMatch::Role::Host)
            return;
        const bool again = rules.mode != match.rules.mode;
        match.rules = rules;
        match.rules.most = static_cast<std::uint8_t>(rules.most < 2 ? 2 : (rules.most > kNetPlayers ? kNetPlayers : rules.most));
        match.host.out.add(netEvent(NetEventType::Rules, 0, match.rules.mode, match.rules.flags, match.rules.most,
                                    static_cast<int>(match.rules.mapHash & 0xFFFF), static_cast<int>(match.rules.mapHash >> 16),
                                    match.rules.frags | match.rules.minutes << 8));
        match.host.out.add(netEvent(NetEventType::RulesMore, 0, match.rules.chatSeconds, match.rules.nextMode,
                                    match.rules.totals ? 1 : 0, match.rules.voteSeconds));
        for (int p = 0; p < kNetPlayers && again; ++p)
            match.profiles[p].team = 0;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            if (netHere(match, p) && (again || (match.rules.teams() && match.profiles[p].team == 0)))
                detail::netPickTeam(match, p);
        }
    }

    /// The team this console's player would rather be on (1, 2; 0: either):
    /// the host says whether they are.
    inline void netAskTeam(NetMatch &match, int team)
    {
        NetProfile profile = match.myProfile;
        profile.asked = static_cast<std::uint8_t>(team == 1 || team == 2 ? team : 0);
        const bool spray = profile.sprayHash != 0;
        netSetProfile(match, profile, spray ? match.myProfile.spray : nullptr);
        match.sprayNext = kNetSprayPieces; // (the spray is not sent again for that)
    }

    /// The local player sprays (the game's impulse 201): their spray on the
    /// wall kNetSprayReach ahead of `eye` along `forward` (Q12), if there is
    /// one, in the place of their last. False if there is no wall there, it
    /// is too soon after the last, or they have no spray.
    inline bool netSpray(NetMatch &match, LogicWorld &w, const PlayerState &player, const fx *eye, const fx *forward)
    {
        if (!match.playing || match.sprayWaitMs > 0 || player.health <= 0 || w.world == nullptr ||
            !match.profiles[match.me].sprayWhole())
            return false;
        fx to[3];
        for (int i = 0; i < 3; ++i)
            to[i] = eye[i] + forward[i] * kNetSprayReach;
        // (On the world: not on what moves, which would leave it behind.)
        const Trace t = detail::pointTrace(*w.world, eye, to);
        if (!t.hit || t.mover >= 0 || t.fraction >= kFractionOne)
            return false;
        fx normal[3];
        for (int i = 0; i < 3; ++i)
            normal[i] = t.normal[i] >> 18; // (Q30 to Q12)
        detail::netSprayPlace(match, match.me, t.endPos, normal);
        match.sprayWaitMs = kNetSprayWaitMs;
        NetEvent spray = netEvent(NetEventType::Spray, 0, t.endPos[0] >> 10, t.endPos[1] >> 10, t.endPos[2] >> 10);
        netPackNormal(normal, spray.v[3], spray.v[4]);
        netSay(match, spray);
        return true;
    }

    namespace detail
    {
        /// A client: which player it is, once the host has said.
        inline void netPlace(NetMatch &match)
        {
            if (match.role == NetMatch::Role::Client && match.me == 0 && match.client.me != 0)
            {
                match.me = match.client.me;
                std::memcpy(match.names[match.me], match.myName, kNetTextBytes);
                // How this console's player looks, to everyone, now that
                // they know who it is.
                if (match.myProfile.known)
                {
                    match.profiles[match.me] = match.myProfile;
                    ++match.profileStamp;
                    netSay(match, netProfileEvent(match.myProfile));
                    match.sprayNext = 0;
                }
            }
        }

        /// Once a frame: this console's spray, a little at a time; and on
        /// the host, the others' passed on -- while there is room for them
        /// beside what a match says (a spray is not worth a lost link).
        inline void netShare(NetMatch &match)
        {
            if (!netPlaced(match))
                return;
            const bool host = match.role == NetMatch::Role::Host;
            const NetOutbox &out = host ? match.host.out : match.client.out;
            for (int n = 0; n < 2 && match.sprayNext < kNetSprayPieces && out.count < NetOutbox::kMost - 32; ++n)
                netSay(match, netSprayPiece(match.myProfile, match.sprayNext++));
            if (!host)
                return;
            for (int p = 1; p < kNetPlayers; ++p)
            {
                const std::uint32_t pending = match.profiles[p].pieces & ~match.relayed[p];
                for (int piece = 0; piece < kNetSprayPieces && match.host.out.count < NetOutbox::kMost - 32; ++piece)
                {
                    if (!(pending & (1u << piece)))
                        continue;
                    NetEvent e = netSprayPiece(match.profiles[p], piece);
                    e.from = static_cast<std::uint8_t>(p);
                    match.host.out.add(e);
                    match.relayed[p] |= 1u << piece;
                }
            }
        }

        /// The events that have come, taken; and on the host, passed on.
        inline void netEvents(NetMatch &match, LogicWorld &w, PlayerState &player)
        {
            const bool host = match.role == NetMatch::Role::Host;
            NetEvent event;
            if (host)
            {
                for (int c = 1; c < kNetPlayers; ++c)
                {
                    while (match.host.clients[c].here && match.host.clients[c].in.take(event))
                    {
                        detail::netApply(match, w, player, event);
                        // What the other clients need of it, passed on.
                        switch (event.type)
                        {
                        case NetEventType::Shot:
                        case NetEventType::Hit:
                        case NetEventType::Burst:
                        case NetEventType::Died:
                        case NetEventType::Spawned:
                        case NetEventType::Fire:
                        case NetEventType::Bye:
                        case NetEventType::Profile:
                        case NetEventType::Spray:
                            match.host.out.add(event);
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
            else
            {
                while (match.client.in.take(event))
                    detail::netApply(match, w, player, event);
            }
        }
    } // namespace detail

    /// Once a frame while there is no map loaded (the lobby): who is here,
    /// what they are called, and whatever else is said. (`none` and `nobody`
    /// are never looked at: nothing that needs a map is done.)
    inline void netLobby(NetMatch &match, LogicWorld &none, PlayerState &nobody)
    {
        if (match.role == NetMatch::Role::Off)
            return;
        detail::netPlace(match);
        match.mine = NetPlayer();
        match.mine.flags = kNetHere;
        detail::netEvents(match, none, nobody);
        detail::netShare(match);
    }

    /// Once a frame, after the game's own tick. `yaw` and `pitch` are the
    /// view's (65536 to the circle), `did` what weaponsTick said it did
    /// (kWeapon*), `lamp` whether the suit's lamp is on.
    /// The host: the match is over, for everyone -- its limits reached, or
    /// by its END MATCH.
    inline void netEndMatch(NetMatch &match)
    {
        if (match.role != NetMatch::Role::Host || !match.playing || match.over)
            return;
        match.endDraw = static_cast<std::uint16_t>((match.seed * 2654435761u + static_cast<std::uint32_t>(match.playedMs)) >> 16);
        detail::netFinalTake(match);
        match.over = true;
        match.host.out.add(netEvent(NetEventType::End, 0, 0, static_cast<std::int16_t>(match.endDraw)));
        detail::netNotice(match, NetEventType::End, 0);
    }

    /// Where a map is shown from between matches: one of its
    /// info_intermission, by `draw` (the game takes one at random, and every
    /// console here the same one); with none, where a player first comes in
    /// (`spawn` then, the eye being over the middle). Null if neither.
    inline const EntityRecord *netIntermissionPoint(const LevelView &view, std::uint32_t draw, bool &spawn)
    {
        spawn = false;
        std::uint32_t points = 0;
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
            points += view.entities[e].classId == static_cast<std::uint16_t>(EntityClass::InfoIntermission) ? 1 : 0;
        std::uint32_t wanted = points != 0 ? draw % points : 0;
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
        {
            if (view.entities[e].classId == static_cast<std::uint16_t>(EntityClass::InfoIntermission) && wanted-- == 0)
                return &view.entities[e];
        }
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
        {
            if (view.entities[e].classId == static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch))
            {
                spawn = true;
                return &view.entities[e];
            }
        }
        return nullptr;
    }

    /// What the top of the final scores says.
    inline void netFinalTitle(const NetMatch &match, char *line, int room)
    {
        const NetFinal &final = match.final;
        const auto size = static_cast<std::size_t>(room);
        if (match.rules.teams())
        {
            if (final.winnerTeam != 0)
                std::snprintf(line, size, "TEAM %d WINS", final.winnerTeam);
            else
                std::snprintf(line, size, "A DRAW");
            return;
        }
        const NetFinal::Row *first = final.count > 0 ? &final.rows[final.order[0]] : nullptr;
        const NetFinal::Row *second = final.count > 1 ? &final.rows[final.order[1]] : nullptr;
        if (first == nullptr || (second != nullptr && second->frags == first->frags && second->deaths == first->deaths))
            std::snprintf(line, size, "A DRAW");
        else
            std::snprintf(line, size, "%s WINS", first->name[0] != 0 ? first->name : "...");
    }

    /// The match's facts, in a line ("" if nothing happened).
    inline void netFinalFacts(const NetFinal &final, char *line, int room)
    {
        int at = 0;
        line[0] = 0;
        const auto add = [&](const char *what, int who, int many)
        {
            if (who >= kNetPlayers || at >= room - 1)
                return;
            at += std::snprintf(line + at, static_cast<std::size_t>(room - at), "%s%s %.8s %d", at != 0 ? "  " : "", what,
                                final.rows[who].name, many);
        };
        add("Kills", final.mostKills, final.kills);
        add("Streak", final.longestStreak, final.streak);
        add("Suicides", final.mostSuicides, final.suicides);
    }

    /// This console's player votes for the vote's map `slot`.
    inline void netVotePick(NetMatch &match, int slot)
    {
        if (slot < 0 || slot >= match.vote.count || match.vote.decided >= 0)
            return;
        if (match.role == NetMatch::Role::Host)
        {
            if (match.vote.cast(match.me, slot))
                detail::netSayTally(match, slot);
            return;
        }
        if (match.vote.choice[match.me] >= 0)
            return;
        match.vote.choice[match.me] = static_cast<std::int8_t>(slot);
        ++match.vote.stamp;
        netSay(match, netEvent(NetEventType::NextPick, slot));
    }

    /// Whether the last minute of a time limit has just begun. (Not for a
    /// limit of a minute: that would be said as the match begins.)
    inline bool netLastMinute(const NetMatch &match)
    {
        return match.playing && !match.over && !match.warned && match.minuteLimit >= 2 &&
               match.playedMs >= (match.minuteLimit - 1) * 60000;
    }

    /// What a death says: who, whom, and with what, in `room` (at least 40)
    /// bytes. Each name is cut, not the words, so that it all fits a line.
    inline void netKillLine(char *line, int room, const char *killer, const char *victim, std::uint8_t weapon)
    {
        static constexpr const char *const kNames[kWeaponCount] = {
            "",       "crowbar",  "9mm",      "shotgun", "grenade", "MP5",  ".357", "crossbow",
            "satchel", "tripmine", "snark",   "RPG",     "gauss",   "egon", "hornet"};
        const char *with = weapon < kWeaponCount ? kNames[weapon] : "";
        if (with[0] != 0)
            std::snprintf(line, static_cast<std::size_t>(room), "%.10s killed %.10s (%s)", killer, victim, with);
        else
            std::snprintf(line, static_cast<std::size_t>(room), "%.11s killed %.11s", killer, victim);
    }

    inline void netTick(NetMatch &match, LogicWorld &w, PlayerState &player, std::uint16_t yaw, std::int32_t pitch,
                        std::uint32_t did, bool lamp, int ms)
    {
        if (match.role == NetMatch::Role::Off)
            return;
        if (!match.playing)
        {
            // (No map: only what is said in the lobby.)
            static LogicWorld none;
            static PlayerState nobody;
            netLobby(match, none, nobody);
            return;
        }
        ActorWorld &a = *w.actors;
        const bool host = match.role == NetMatch::Role::Host;
        detail::netPlace(match);

        // ---- what this console's player did
        if (match.playing)
        {
            // What their shots and blows took off the others: a claim each.
            for (int p = 0; p < kNetPlayers && match.firstActor >= 0; ++p)
            {
                ActorState &actor = a.states[match.firstActor + p];
                if (p == match.me || !actor.active || actor.kind != static_cast<std::uint8_t>(ActorKind::Player))
                    continue;
                const int hurt = kNetActorHealth - actor.health;
                actor.health = kNetActorHealth;
                // (Which of their lives it was: a hit on one that is over
                // by the time it arrives is not taken by the next.)
                // (Nor on a teammate, without friendly fire.)
                if (hurt > 0 && player.health > 0 && !match.over && !netTeammates(match, match.me, p))
                    netSay(match, netEvent(NetEventType::Hit, p, hurt > 30000 ? 30000 : hurt, netPlayer(match, p).spawns));
            }
            // What they struck of the map's.
            for (int n = 0; n < w.struckCount; ++n)
                netSay(match, netEvent(NetEventType::Fire, 1, w.struck[n * 2], w.struck[n * 2 + 1]));
            w.struckCount = 0;
            if (match.sprayWaitMs > 0)
                match.sprayWaitMs -= ms;
            // What they would pick up.
            if (a.wanted >= 0)
            {
                if (host)
                    detail::netGrant(match, w, player, a.wanted, match.me);
                else
                    netSay(match, netEvent(NetEventType::Take, 0, a.wanted));
                // (Asked once: it is asked again if they are still on it a
                // second from now and nobody has it.)
                a.wanted = -2;
            }
            if (did & kWeaponFired)
                netSay(match, netEvent(NetEventType::Shot, player.inv.current, yaw, pitch,
                                       (did & kWeaponSecondary) ? 1 : 0));
            // A grenade that was in flight and is not: it went off there.
            for (int s = 0; s < kMaxProjectiles; ++s)
            {
                const Projectile &shot = a.shots[s];
                const bool is = shot.active && (shot.kind == kShotGrenade || shot.kind == kShotContact ||
                                                  shot.kind == kShotRocket || shot.kind == kShotSatchel);
                if (match.shotWas[s] && !is)
                    netSay(match, netEvent(NetEventType::Burst, 0, match.shotAt[s][0] >> 10, match.shotAt[s][1] >> 10,
                                           match.shotAt[s][2] >> 10));
                match.shotWas[s] = is;
                for (int i = 0; is && i < 3; ++i)
                    match.shotAt[s][i] = shot.pos[i];
            }
            // Their death; and after a while, their return.
            if (match.blameMs > 0)
                match.blameMs -= ms;
            if (player.health <= 0)
            {
                if (match.wasAlive && !match.over)
                {
                    match.wasAlive = false;
                    match.deadMs = 0;
                    const int killer = match.blameMs > 0 ? match.blamed : kNetPlayers;
                    netSay(match, netEvent(NetEventType::Died, killer));
                    detail::netNotice(match, NetEventType::Died, match.me, killer);
                    detail::netCount(match, match.me, killer);
                    if (host)
                        detail::netScore(match, match.me, killer);
                }
                match.deadMs += ms;
                if (match.deadMs >= kNetRespawnMs && !match.over)
                    netSpawn(match, w, player);
            }
        }
        // (The pickup asked for a second ago and not given: ask again.)
        if (a.wanted == -2 && ((match.playedMs / 1000) != ((match.playedMs + ms) / 1000)))
            a.wanted = -1;

        // ---- where they are, for the next frame
        NetPlayer &mine = match.mine;
        for (int i = 0; i < 3; ++i)
        {
            const std::int32_t quarter = player.origin[i] >> 10;
            mine.pos[i] = static_cast<std::int16_t>(quarter < -32768 ? -32768 : (quarter > 32767 ? 32767 : quarter));
            const std::int32_t speed = fxToInt(player.velocity[i]) / 8;
            mine.velocity[i] = static_cast<std::int8_t>(speed < -127 ? -127 : (speed > 127 ? 127 : speed));
        }
        mine.yaw = yaw;
        mine.pitch = static_cast<std::int8_t>(pitch >> 8);
        mine.weapon = player.inv.current;
        mine.health = static_cast<std::uint8_t>(player.health < 0 ? 0 : (player.health > 255 ? 255 : player.health));
        mine.spawns = match.spawns;
        mine.flags = static_cast<std::uint8_t>(kNetHere | (match.playing ? kNetInGame : 0) |
                                               (match.playing && player.health > 0 ? kNetAlive : 0) |
                                               (player.ducked ? kNetCrouched : 0) | (player.onGround ? kNetOnGround : 0) |
                                               ((did & kWeaponFired) ? kNetFiring : 0) | (lamp ? kNetLamp : 0));

        // ---- what the others did
        detail::netEvents(match, w, player);
        detail::netShare(match);

        // ---- the others, here
        for (int p = 0; p < kNetPlayers && match.firstActor >= 0; ++p)
        {
            if (p == match.me)
                continue;
            ActorState &actor = a.states[match.firstActor + p];
            const NetPlayer &heard = netPlayer(match, p);
            const bool here = (heard.flags & kNetHere) && (heard.flags & kNetInGame) && match.model >= 0;
            if (!here)
            {
                if (actor.active)
                {
                    actor.active = false;
                    actorPlaceBox(a, match.firstActor + p);
                }
                match.glide[p].placed = false;
                match.pose[p] = 0;
                continue;
            }
            netGlide(match.glide[p], heard, ms);
            const bool alive = (heard.flags & kNetAlive) != 0;
            const bool crouched = (heard.flags & kNetCrouched) != 0;
            if (!actor.active || actor.kind != static_cast<std::uint8_t>(ActorKind::Player))
            {
                actor = ActorState();
                actor.active = true;
                actor.kind = static_cast<std::uint8_t>(ActorKind::Player);
                actor.model = static_cast<std::uint16_t>(match.model);
                actor.health = kNetActorHealth;
                match.pose[p] = 0;
            }
            actor.model = static_cast<std::uint16_t>(match.characterModel[p] != 0 ? match.characterModel[p] : match.model);
            // (Where their middle is: the players' model has its origin
            // there, as the game's player has -- not at their feet, as most
            // actors' do. HlActors.hpp's boxes for a Player know it.)
            actor.pos[0] = match.glide[p].pos[0];
            actor.pos[1] = match.glide[p].pos[1];
            actor.pos[2] = match.glide[p].pos[2];
            actor.yaw = heard.yaw;
            actor.mood = crouched ? 1 : 0;
            actor.flags = alive ? kActorSolid : 0;
            // (Dead, they are no longer shot at: what is dead is passed by.)
            actor.ai = alive ? 0 : kAiDead;
            // What they are doing, as a body does it.
            const int speed = (heard.velocity[0] < 0 ? -heard.velocity[0] : heard.velocity[0]) +
                              (heard.velocity[1] < 0 ? -heard.velocity[1] : heard.velocity[1]);
            const std::uint8_t pose = !alive ? kPoseDead
                                      : !(heard.flags & kNetOnGround) ? kPoseJump
                                      : crouched ? (speed > 2 ? kPoseCrawl : kPoseCrouch)
                                      : speed > 22 ? kPoseRun
                                      : speed > 2 ? kPoseWalk
                                                  : kPoseIdle;
            if (pose != match.pose[p])
            {
                static constexpr std::uint32_t kClips[] = {0,
                                                           nameHash("look_idle"),
                                                           nameHash("walk"),
                                                           nameHash("run"),
                                                           nameHash("crouch_idle"),
                                                           nameHash("crawl"),
                                                           nameHash("jump"),
                                                           nameHash("die_simple")};
                actorPlay(a, actor, kClips[pose], pose == kPoseDead || pose == kPoseJump);
                match.pose[p] = pose;
            }
            actorPlaceBox(a, match.firstActor + p);
            // ... and walking into the map's things here, as they do there.
            if (alive)
            {
                PlayerState ghost = detail::netGhost(match, w, p);
                logicGhostTouch(w, ghost);
            }
        }

        // ---- the host's own business
        match.playedMs += ms;
        if (netLastMinute(match))
            match.warned = match.warnDue = true;
        if (match.over)
            detail::netFinalWatch(match);
        if (!host)
            return;
        for (NetMatch::Taken &slot : match.taken)
        {
            if (slot.actor < 0 || (slot.ms -= ms) > 0)
                continue;
            a.states[slot.actor].active = true;
            actorPlaceBox(a, slot.actor);
            match.host.out.add(netEvent(NetEventType::Back, 0, slot.actor));
            slot.actor = -1;
        }
        if (!match.over)
        {
            bool done = match.minuteLimit > 0 && match.playedMs >= match.minuteLimit * 60000;
            // (In teamplay, a team's frags: the game's teamplay rules.)
            for (int p = 0; p < kNetPlayers && !match.rules.teams(); ++p)
                done = done || (match.fragLimit > 0 && match.frags[p] >= match.fragLimit);
            for (int t = 1; t <= 2 && match.rules.teams(); ++t)
                done = done || (match.fragLimit > 0 && match.teamFrags[t] >= match.fragLimit);
            if (done)
                netEndMatch(match);
        }
    }
} // namespace hldata
