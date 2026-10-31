#pragma once

#include "HlAirBank.hpp"
#include "EngineObject.hpp"

// A game between consoles, on this one: the radio (NetLink) joined to the
// match (shared/HlNetGame.hpp) and to the game.
//
// frame() is called once a frame whatever is on the screen: it reads what
// has come, keeps account of who is there, and -- while no map is loaded --
// says this console's part of the lobby. With a map loaded, the game calls
// tick() after its own tick instead: that is where what the player did goes
// out and what the others did comes in.

#include <cstdio>
#include <string>
#include <vector>

#include "HlNetAir.hpp"
#include "HlNetGame.hpp"
#include "HlNetMaps.hpp"

namespace hlrt
{
    class NetGame
    {
    public:
        enum class Phase
        {
            Off,
            Hosting,  ///< a host, in its lobby
            Serving,  ///< ... sending the match's map to the consoles it gave the game to, before it begins
            Scanning, ///< looking for hosts
            Joining,  ///< asking one to be let in
            Lobby,    ///< a client, in the host's lobby
            Playing,  ///< a map is loaded and the match is on
        };

        Phase phase() const { return phase_; }
        bool on() const { return phase_ != Phase::Off; }
        bool playing() const { return phase_ == Phase::Playing; }
        bool isHost() const { return match_.role == hldata::NetMatch::Role::Host; }
        const hldata::NetMatch &match() const { return match_; }
        hldata::NetMatch &match() { return match_; }

        /// The maps a match can be played on (the card's, by the manifest),
        /// with what is known of each.
        void setMaps(std::vector<hldata::NetMapInfo> maps)
        {
            maps_.clear();
            for (const hldata::NetMapInfo &map : maps)
                maps_.push_back(map.name);
            mapInfo_ = std::move(maps);
        }
        const std::vector<std::string> &maps() const { return maps_; }
        const std::vector<hldata::NetMapInfo> &mapInfo() const { return mapInfo_; }

        // ------------------------------------------------------------ lobby
        bool host();  ///< be a host; false if the radio would not start
        bool scan();  ///< look for hosts
        /// The hosts found so far: up to `most` of them, each a line to
        /// show ("name 2/4"), what its beacon says of the match, how strong
        /// its signal is, and what to give join().
        struct Found
        {
            char line[32];
            char name[16];
            int index;
            int players, most;
            /// What the host says of its match; `room.version` 0 if it says
            /// nothing (an older build), and another than kNetVersion if it
            /// is a build this one cannot play with.
            hldata::NetRoomInfo room;
            const char *map; ///< its map, if this card has it; else null
            int bars;        ///< 0..4
        };
        int found(Found *out, int most);
        bool join(int index);
        /// A console started by Download Play: the host is whoever started
        /// it.
        bool joinStarter();
        /// Out of it altogether: the others are told, the radio is silent.
        void leave();
        /// The host: the settings' match (its mode, its rules, its map) has
        /// changed -- to the others in the lobby, and to the beacon.
        void hostChanged();
        /// The profile was changed (PROFILE, from the lobby): said to the
        /// others again.
        void sayProfile();
        /// The team this console's player would rather be on (1, 2; 0:
        /// either). The host says which they are on.
        void askTeam(int team);
        /// How strong the link to player `p` is, in bars (0..4); -1 if it is
        /// not known (a client knows only its host's).
        int bars(int p) const;
        /// The map of the match being waited for: the host's own, or (a
        /// client) the one its beacon said, if this card has it; "" if not.
        const char *lobbyMap() const;

        /// The host: begin the match on `map`. (It then loads the map
        /// itself, as the clients do when they hear.) With consoles it gave
        /// the game to among them, it first sends them the map (Serving).
        void start(const std::string &map, int fragLimit, int minuteLimit, int skill);
        /// The host: what it is sending the consoles it gave the game to, if
        /// it is -- which stream ("the game": the common one; a group's
        /// base; a map), to how many, and how far the slowest has got.
        bool sending(const char *&what, int &consoles, int &percent) const;
        /// How many of its clients are consoles it gave the game to.
        int guests() const;
        /// The host: the least of how far up their memory the heaps of the
        /// consoles sent the game that are here reach (KB; 0: none has said),
        /// and one's -- with what of it was free in its lobby.
        std::uint32_t guestRoom() const;
        bool dsiTier(int p) const { return p >= 0 && p < hldata::kNetPlayers && tiers_[p] != 0; }
        std::uint32_t roomOf(int p) const;
        std::uint32_t freeOf(int p) const;
        /// A console sent the game: how far up its memory its heap reaches,
        /// and what of it is free (KB).
        void sayRoom(std::uint32_t top, std::uint32_t free);
        /// The radio's account of the last second, in a line (the host's
        /// STARTING page, while it sends): exchanges sent and done, done
        /// again, refused for a full queue.
        void radioLine(char *line, int room);
#ifdef HLDS_CHILD
        /// A console that was sent the game: a stream the host has begun
        /// sending it, to be taken now (GuestLoad's receiveAir, or a map
        /// loaded). True once per stream.
        bool airAsked(hldata::AirHead &head);
#endif
        /// The host: the match about to begin is not to (CANCEL): everyone
        /// back to the lobby.
        void cancelStart();
        /// The host, on its STARTING page: how player `p` stands, in a word
        /// into `line`. 0: nobody there; 1: on the way; 2: ready.
        int startStatus(int p, char *line, int room) const;
        /// Every console: its match's map is loaded (a client says it is
        /// Ready), and it waits for the word to begin: goAsked, true once --
        /// the host's when everyone has said it (or long enough has gone by).
        void loadedMap();
        bool goAsked();
        /// The host gave up the match being started (true once): the map
        /// loaded for it is let go, and it is the lobby again.
        bool cancelled();
        /// A match is being begun (from START to the word to go).
        bool starting() const { return starting_; }
        /// Whether a match has been started and its map is to be loaded:
        /// which map, and how hard the host made it (kSkill*). True once per
        /// start, on every console.
        bool startAsked(std::string &map, int &skill);
        /// The map is loaded: the match begins here.
        void mapLoaded();
        /// The match is over and its scores have been shown: the map is to
        /// be left for the host's lobby, where the host may start another.
        /// True once per match.
        bool lobbyAsked();
        /// ... and it has been left (the map is gone): the lobby again, with
        /// whoever is still there.
        void toLobby();
        /// The map coming next, as it is known ("": none).
        const char *nextName() const;
        /// The match over, and the time for the vote: its page is up.
        bool votePage() const;
        /// How long it has to go (as well as it is known here).
        int voteSecondsLeft() const;
        /// This console's player chooses the vote's map `slot`, for good.
        void vote(int slot);
        /// The vote's page was drawn afresh: its pictures are to be had again.
        void voteRedrawn();
        /// Another map in slot `slot` of the list: its picture is to be drawn.
        void voteSlotChanged(int slot) { voteRowDrawn_[slot] = -1; }
        /// Its page is on the screen (what has come may be drawn on it).
        void setVoteShown(bool shown) { voteShown_ = shown; }
        /// It is over: its pictures let go of, and their colours off the screen.
        void voteLetGo();
        /// A vote before the first match is going on (in the lobby).
        bool voteOn() const;
#ifndef HLDS_CHILD
        /// The host, from the end of a match: everyone back to its lobby now,
        /// whatever was to come next.
        void sendAllToLobby();
        /// The host: START with the next map to be voted for.
        void startFirstVote(int skill);
        /// The host, back in its lobby: the map chosen when the match ended,
        /// once the others have had time to leave theirs. It stays due until
        /// startNext or dropNext.
        bool nextDue(std::string &map);
        void startNext(const std::string &map, int fragLimit, int minuteLimit);
        void dropNext();
        /// No map could follow the match (true once): the host is to choose.
        bool nextNone();
#endif
        /// The host: player `p` let go, told why (and, a moment on, the
        /// radio's link cut); not let back in while this game lasts.
        /// Who a player chose to be, here to be shown (a console sent the game
        /// asks the host when it is wanted).
        bool personHere(int id) const;
        void wantPerson(int id);
        /// A map's load is about to begin: what the lobby asked for goes.
        void forMapLoad();
        /// Something of the match's map, by the hash of its name; delivered
        /// to TextureStream. fetchBusy: one at a time.
        void fetchMap(std::uint32_t name);
        bool fetchBusy() const { return takingOn_ || fileCount_ > 0; }
        /// A file of the match's map the console in DSi mode has not (its
        /// bank's, by path): put with the others when it comes (air::). False
        /// if it is not to be had, or there is no room to ask; `first`: ahead
        /// of the rest. Those of a texture wait until these are done.
        bool fetchFile(const char *path, bool first = false);
        void kick(int p);
        /// The host, its map loaded and not begun: its lobby again.
        void toLobbyFromLoad();
        /// The host: the match over now, as if its limit had been reached
        /// (END MATCH).
        void endMatch();
        /// The host: the match given up, everyone back in its lobby -- those
        /// still loading it too.
        void abortMatch();
        /// The host: everyone told it is off to send the game, and back soon.
        void sayBackSoon();
        /// Told so by the host: its name, whose game to look for and join
        /// again ("": none, or no longer). Then it is let go of.
        const char *backSoon();
        void backSoonDone() { backSoon_[0] = 0; }

        // ---------------------------------------------------------- frames
        void frame();
        /// After the game's tick (see HlNetGame.hpp's netTick).
        void tick(std::uint16_t yaw, std::int32_t pitch, std::uint32_t did, bool lamp, int ms);
        /// The local player used the map's record `index`.
        void used(int index);
        /// The local player sprays (the game's impulse 201): true if it went
        /// on a wall.
        bool spray();

        /// Something to say on the screen, if there is: a line of it into
        /// `line`. False when there is nothing more.
        bool notice(char *line, int room);
        /// Why this console is out of the game, if it is and did not ask to
        /// be: the host left, the link was lost. Empty otherwise; cleared
        /// by reading.
        const char *ended();
        /// The radio's own account of itself, for the debug readout.
        void status(char *line, int room) const;

    private:
        Phase phase_ = Phase::Off;
        hldata::NetMatch match_;
        std::vector<std::string> maps_;
        std::vector<hldata::NetMapInfo> mapInfo_;
        std::string lobbyMap_; ///< (a client: the host's map, as its beacon said when it was joined)
        std::vector<hldata::NetSpawn> spawns_;
        unsigned clientsWere_ = 0;
        int lostFrames_ = 0;
        const char *ended_ = "";
        char backSoon_[hldata::kNetTextBytes] = {};
        std::uint32_t backSoonAt_ = 0;
        std::uint32_t kickAt_[hldata::kNetPlayers] = {};
        int joinFrames_ = 0;
        bool fullGuests_ = true;
        bool fullTried_ = false;
        bool characterFullDone_ = false; ///< (Serving) this character has gone to those in DSi mode
        unsigned fullSent_ = 0; ///< (Serving) those sent the map at full quality
        int silentFrames_ = 0;
        std::uint32_t heardWas_ = 0;
        int serveCharacter_ = 1, serveCharacters_ = 0;
        unsigned answered_ = 0; ///< (a client's) heard_ when it last answered
        std::string pendingMap_;
        int pendingSkill_ = 2; ///< (kSkillMedium)
        bool startPending_ = false;
        std::uint32_t sent_ = 0, heard_ = 0;
        std::uint32_t profilesSeen_ = 0; ///< the match's profileStamp, when the colours were last set
        std::uint32_t ticks_ = 0;
        int overMs_ = 0;      ///< how long the match has been over
        bool lobbyAsked_ = false;
        // Beginning a match: who has said Ready (the host's: bit n for
        // client n), and the words heard (a client's).
        bool starting_ = false, goAsked_ = false, cancelled_ = false;
        // A client's: the picture of the host's map, asked for in the lobby
        // by a console that has none of it (a stream of its own, whole in
        // one window), and the map it is of.
        // What it has asked for (HlAirBank.hpp).
        hldata::FetchTaking taking_;
        std::uint8_t *takingRoom_ = nullptr;
        bool takingOn_ = false;
        std::uint32_t takingAskedAt_ = 0, takingSaidAt_ = 0;
        int takingTries_ = 0;
        int pictureNext_ = 0;   ///< the next of the host's pictures to ask for; -1: no more
        int personWanted_ = 0;  ///< who is to be read next (0: none)
        int personStage_ = 0;   ///< (0: its model, 1: its skin)
        int personsHad_[2] = {0, 0};
        void fetchAsk(hldata::FetchFrom from, std::uint32_t what);
        void fetchSayHave();
        void fetchDrop();
        static constexpr int kFileAsks = 12;
        static constexpr int kFilePath = 56;
        char fileAsks_[kFileAsks][kFilePath] = {};
        int fileCount_ = 0;
        char filePath_[kFilePath] = {}; ///< the one on its way ('\0': none, or a texture)
        std::uint32_t fileNone_[64] = {};
        int fileNoneCount_ = 0;
        void fetchGaveUp(bool none = false);
        void fetchFrame(const hldata::FetchHead &head);
        void fetchDeliver();
        void fetchTick();
        void dropPeople();
#ifndef HLDS_CHILD
        hldata::FetchHost fetch_;
        std::uint8_t *fetchData_[hldata::FetchHost::kSending] = {};
        bool fetchFind(const hldata::FetchHost::Asked &asked, std::uint8_t *&data, std::uint32_t &packed, std::uint32_t &raw,
                       std::uint32_t &name);
        void fetchHostFrame();
        void fetchHostStop();
#endif
        // The vote's pictures: had as they are wanted, a few kept.
        std::uint8_t *voteData_[hldata::NetVotePictures::kPlaces] = {};
        std::uint32_t voteBytes_[hldata::NetVotePictures::kPlaces] = {};
        hldata::NetVotePictures votePictures_;
        std::uint32_t voteNone_ = 0;     ///< bit: the host has no picture of that map
        int voteRowDrawn_[hldata::kNetVoteRows] = {-1, -1, -1};
        bool voteShown_ = false;
        int voteSent_ = 0;               ///< (the host's) of the maps' names
        int voteMs_ = 0, voteOpenedAt_ = 0, voteDecidedAt_ = 0;
        int voteLastsMs() const;
        void voteKeep(int map, std::uint8_t *data, std::uint32_t bytes);
        void voteFrame(int ms);
        unsigned ready_ = 0;
        std::uint32_t rooms_[hldata::kNetPlayers] = {}; ///< (the host's: KB, by client; 0 not said)
        std::uint32_t frees_[hldata::kNetPlayers] = {};
        std::uint8_t tiers_[hldata::kNetPlayers] = {}; ///< (the host's: 1, in DSi mode)
        std::uint32_t loadedAt_ = 0;
        /// How long the host waits for those not ready once it is.
        static constexpr std::uint32_t kReadyVblanks = 30 * 60;
        void dropLate(int p); ///< (not let go for good, as kick: it never said it was ready)
#ifndef HLDS_CHILD
        // (The host's.) What it sends the consoles it gave the game to.
        hldata::AirSender air_;
        // What comes after the match.
        std::string nextMap_;
        bool nextChosen_ = false, nextNone_ = false;
        std::uint32_t nextAt_ = 0;
        /// How long the host waits, back in its lobby, for the others to have
        /// left the map too.
        static constexpr std::uint32_t kNextWaitVblanks = 3 * 60;
        void chooseNext();
        void startVote(int chooser); ///< (-1: everyone votes)
        int turn_ = -1;              ///< whose turn it was to choose
        void decideVote();
        void sayNext(const std::string &map, int slot);
        void goBackToLobby();
        std::string voteMaps_[hldata::NetVote::kMost];
        unsigned guests_ = 0;        ///< bit n: client n has asked for a stream
        std::FILE *airFile_ = nullptr;
        std::uint16_t serves_ = 0;   ///< sendings made, for each one's number
        int serveStage_ = 0;         ///< (Serving: 0 waiting, 1 the base, 2 the map, 3 sent)
        int serveSkill_ = 2;
        bool airBegin(hldata::AirStream stream, const std::string &name, const std::string &path, unsigned mask,
                      int skill = 0);
        void airEnd();
        int airSend(int frames);
        void airAsk(int from, const std::uint8_t *bytes, int size);
        void serveNext();
#else
        hldata::AirHead airAsked_;
        bool airPending_ = false;
        std::uint8_t airReply_[hldata::kNetReplyBytes];
        int airReplySize_ = 0;
#endif

        void send(bool always = false);
        void sayRules();
        void lost(const char *why);
        void forgetGuest(); ///< out of a game: nothing kept of what the last host said
    };

    HL_ENGINE_OBJECT(NetGame, netGame);
} // namespace hlrt
