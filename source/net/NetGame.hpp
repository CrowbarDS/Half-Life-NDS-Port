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

#include "HlDemo.hpp"
#include "HlDemoSave.hpp"
#include "HlNetAir.hpp"
#include "HlNetGame.hpp"
#include "HlNetMaps.hpp"
#include "net/MapShare.hpp"

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
        /// A match online whose host lets its players talk (TALK, held).
#ifdef HLDS_CHILD
        bool talkAllowed() const { return false; } ///< (a console sent the game is never online)
#else
        bool talkAllowed() const { return playing() && online() && match_.rules.has(hldata::kRuleVoice); }
#endif
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
        /// Over the Internet through the relay (NetRelay.hpp), or over the
        /// local wireless: for the next host() or scan(). The lobby and the
        /// match are the same either way.
        void setOnline(bool on);
#ifdef HLDS_CHILD
        bool online() const { return false; } ///< (a console sent the game never is)
#else
        bool online() const;
#endif
        /// Why going online could not be (null if it can), and why it failed
        /// last time.
        const char *onlineUnavailable() const;
        const char *onlineWhy() const;
        /// Online once already: the local wireless needs the game started
        /// again (NetRelay.hpp, holdsWifi).
        bool localBlocked() const;
        /// Going online, a step a frame (NetRelay.hpp: connectBegin...): 0
        /// on the way, 1 there (host() and scan() may be asked), -1 failed
        /// (onlineWhy says why). `step`: what it is doing, in words.
        void connectBegin();
        int connectStep(const char *&step);
        void connectCancel();
        bool onlineReady() const;
        /// The host: one asking to come in (online: the host decides), its
        /// ticket and name (kNetTextBytes); the same until answered.
        bool knock(int &ticket, char *name);
        void answerKnock(int ticket, bool yes);
        /// Whether consoles without the game are offered it: the setting's
        /// word, and never online (nothing is sent by Download Play there).
        bool guestsOffered() const;
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
        /// The host's map, sent to a client without it when the match
        /// begins (net/MapShare.hpp): a client's line for its lobby -- that
        /// it will be sent, how far it has come, or why it cannot be (false:
        /// it has it). (The host's: sending(), as for consoles given the game.)
        bool mapLine(char *line, int room) const;
#ifndef HLDS_CHILD
        /// CHAT: `text` said to everyone. 0: said; 1: too soon after the
        /// last (two seconds); 2: nothing to say, or no room for it now.
        int chat(const char *text);
        /// The last `most` things said (up to NetMatch::kChatLines), oldest
        /// first, a line each ("NAME: text"), into `out`; how many. `stamp`:
        /// match().chatStamp as they were taken (a new one: more to show).
        int chatLines(char *out, int room, int most, std::uint32_t &stamp) const;
        /// How many things have been said (0: none, or none kept here -- a DS).
        std::uint32_t chatStamp() const { return match_.chatLog != nullptr ? match_.chatLog->stamp : 0; }
#endif
        /// A client, a match begun and this card without the host's map: it
        /// is being sent -- how far, how big, how fast (KB a second), and its
        /// name (kMapCardHeadBytes) -- for the screen a console sent the game
        /// shows (main.cpp).
        bool mapReceiving(int &percent, char *step, char *note) const;
        /// A match's map is to be loaded now (startAsked will say which).
        bool startDue() const { return startPending_; }
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
        /// A demo being served, or played from it (HlDemo.hpp); 0: a game.
        int demo() const { return match_.rules.demo; }
#ifndef HLDS_CHILD
        /// Whether all of a demo's streams are on this card.
        bool demoThere(int group);
        /// The host: serve a demo, and nothing else. False if the radio would
        /// not start.
        bool demoHost(int group);
        /// A receiver's line, for the Serving page: who, what, how far. `row`-th
        /// of those here; false: no such. Its signal, 0..4.
        bool demoRow(int row, char *line, int room, int &bars);
#else
        /// A console in DSi mode playing a demo: this map wanted, from this piece
        /// (`track`: not a map, the music's track of that number).
        void demoAsk(std::uint32_t map, bool now, std::uint32_t from, int track = 0);
        /// The room the demo's tracks need (0: it has none).
        std::uint32_t demoMusicBytes() const { return static_cast<std::uint32_t>(match_.rules.demoMusic) * 65536u; }
        /// Who this console is, to the host (which says whether it holds its
        /// progress: demoKnown); or that its progress is let go of.
        void demoHello(bool letGo);
        /// -1 not told yet, 0 nothing held, 1 held.
        int demoKnown() const { return demoKnown_; }
        /// 0 not chosen yet, 1 from the start, 2 from where it was left.
        int demoChoice() const { return demoChoice_; }
        void setDemoChoice(int choice) { demoChoice_ = choice; }
        /// A file of its progress asked for: guest::demoFileCame.
        void demoFile(std::uint32_t what);
        std::uint32_t demoLongest() const { return static_cast<std::uint32_t>(match_.rules.demoLongest) * 16384u; }
#endif
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
        int demoKnown_ = -1, demoChoice_ = 0;
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
        /// What is asked for, sent: `most` pieces this frame (0: as the air
        /// has room for). How many were.
        int fetchHostFrame(int most = 0);
        void fetchHostStop();
        // A map's archive sent from the card (FetchFrom::MapCard), read
        // from its file a piece at a time.
        std::FILE *fetchFile_[hldata::FetchHost::kSending] = {};
        char fetchName_[hldata::FetchHost::kSending][hldata::kMapCardHeadBytes] = {};
        bool fetchPiece(int k, std::uint32_t at, std::uint8_t *to, int count);

        // The host's map, as MapShare.hpp has it. The host: what it is
        // found to come to (said by MapSum), and what each client says it
        // has of it.
        MapSum mapSum_;
        std::uint32_t mapHash_ = 0, mapCrc_ = 0, mapBytes_ = 0;
        bool mapKnown_ = false;
        unsigned mapSaidTo_ = 0;
        std::uint32_t mapSumAt_ = 0; ///< (when MapSum was last said)
        std::uint8_t mapHave_[hldata::kNetPlayers] = {};
        std::uint16_t mapPermille_[hldata::kNetPlayers] = {};
        std::uint32_t mapHeardAt_[hldata::kNetPlayers] = {};
        void mapHostFrame();
        // A client: what the host said of its map, and what this console
        // has of it -- its own, or one sent (mapName_, kept at mapFile_).
        enum class MapStage : std::uint8_t
        {
            None,     ///< nothing said, or nothing to do
            Own,      ///< finding what its own maps/<map>.npac comes to
            Want,     ///< to be asked for (the match begun)
            Index,    ///< its index, being sent
            Build,    ///< what this card has of it, found and copied (MapBuild)
            Getting,  ///< the rest, being sent
            Check,    ///< all of it here: what it comes to
        };
        MapBuild mapBuild_;
        bool mapWhole_ = false; ///< (what was copied was found wrong: all of it from the host)
        MapStage mapStage_ = MapStage::None;
        std::uint8_t mapSays_ = hldata::kMapHaveYes;
        char mapName_[hldata::kMapCardHeadBytes] = {};
        bool mapSent_ = false; ///< (mapName_ is one sent, not the card's own)
        int mapTries_ = 0;
        std::uint32_t mapOffer_[3] = {}; ///< (the last MapSum heard: hash, crc, bytes)
        bool mapOfferNew_ = false;
        bool mapStartWait_ = false; ///< (a match begun before this had the host's map)
        std::uint32_t mapSaidAt_ = 0;
        void mapOffered(std::uint32_t hash, std::uint32_t crc, std::uint32_t bytes);
        void mapClientFrame();
        void mapSay(std::uint8_t state);
        void mapFetchFrame(const hldata::FetchHead &head);
        void mapForget();
        bool mapLineTwl(char *line, int room) const;
        bool mapServe(int who, const hldata::FetchHost::Asked &asked);
        void mapHeard(const hldata::NetEvent &event);
        int mapForStart(std::uint32_t hash);
        void mapNeed();
        bool mapWaiting(int &consoles, int &percent) const;
        void serveCards();
        bool mapReceivingTwl(int &percent, char *step, char *note) const;
        std::uint32_t mapGotAt_ = 0;
        std::uint32_t chatSaidAt_ = 0; ///< (CHAT: when this console last said something)
        void mapIndexCame(std::uint8_t *data, std::uint32_t bytes);
        bool mapIndexFind(const hldata::FetchHost::Asked &asked, std::uint8_t *&data, std::uint32_t &packed, std::uint32_t &raw,
                          std::uint32_t &name);
        void mapAsk();
        void mapHaveHeard(int from, const std::uint8_t *bytes, int size);
        int mapHaveReply(std::uint8_t *reply);
        void mapCheck();
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
        // Serving a demo: who is sent what, each stream's file, and what is
        // known of each map's.
        hldata::DemoServer demoServer_;
        // (A map's stream is two files, its .dsi.hgs then, at demoBankAt, its
        // .dsi.bank; a track's, one: demoBank_ null.)
        std::FILE *demoFile_[hldata::DemoServer::kStreams] = {};
        std::FILE *demoBank_[hldata::DemoServer::kStreams] = {};
        std::uint32_t demoFileMap_[hldata::DemoServer::kStreams] = {};
        std::uint32_t demoFileGuest_[hldata::DemoServer::kStreams] = {}; ///< the first file's length
        std::uint32_t demoLength_[hldata::kDemoMapsMost] = {};
        std::uint32_t demoGuest_[hldata::kDemoMapsMost] = {}; ///< its .dsi.hgs's length
        std::uint32_t demoCrc_[hldata::kDemoMapsMost] = {};
        struct DemoTrack
        {
            int track = 0;
            std::uint32_t length = 0, crc = 0;
        };
        static constexpr int kDemoTracks = 8;
        DemoTrack demoTracks_[kDemoTracks];
        bool demoRead(int stream, std::uint32_t at, std::uint8_t *to, int count);
        void demoClose(int stream);
        bool demoLiveNow_ = false;    ///< a stream someone is waiting for went out this frame
        std::uint32_t demoIds_[hldata::kNetPlayers] = {}; ///< who they are, from one day to the next
        hldata::DemoUpTaker demoTakers_[hldata::kNetPlayers];
        void demoHello(const hldata::NetEvent &event);
        void demoUpHeard(int from, const std::uint8_t *bytes, int size);
        std::string demoFolder(int c) const;
        std::string demoFilePath(int c, std::uint32_t what) const;
        int fetchWho_ = 0;            ///< (who asked for what fetchFind is finding)
        std::uint16_t demoTokens_[hldata::kNetPlayers] = {};
        std::uint8_t demoMapOf_[hldata::kNetPlayers] = {}; ///< (its place among the demo's maps, + 1)
        int demoFrame();
        void demoHeard(const hldata::NetEvent &event);
        void demoStop();
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
