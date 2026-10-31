#pragma once

// The card of a multiplayer map on the title's top screen, in the lobby and
// while looking for a game: its picture (HlPreview.hpp: taken the first time
// this console hosted a match on it), and beside it its name, how many it is
// for, whether a console without the game can be sent it.
//
// Two textures, made when the card is shown and let go when it is not: the
// picture in the console's own colours, and the words, drawn with the touch
// screen's fonts into a canvas off the screen and made a texture of the UI's
// colours. Drawn by HudDraw with the title's picture (menuFrame), over it.

#include <string>

#include "HlNetMaps.hpp"
#include "HlPreview.hpp"

namespace hlrt
{
    class MapCard
    {
    public:
        /// The card of `map` (null: none), and what to say if its facts are
        /// not known (`elsewhere`: a host's map this card has not got).
        /// Nothing is made again for the card already shown.
        void show(const hldata::NetMapInfo *map, const char *elsewhere = nullptr);
        void hide() { show(nullptr); }
        /// Kept, but not drawn: a page of the lobby's that has no use for it
        /// (to make it again would be seen).
        void setHidden(bool hidden) { hidden_ = hidden; }
        bool hidden() const { return hidden_; }
        /// A picture of `name` the host sent (a .hpv of either size:
        /// HlPreview.hpp), kept to show in place of the card's -- a console
        /// that was sent the game has no card. This takes `file` over. All of
        /// the host's are kept, so that its map changing shows at once.
        /// Shown now if that map's card is.
        void givePicture(const char *name, std::uint8_t *file, std::size_t size);
        /// ... and all let go (a match begins: its memory is the map's).
        void dropPicture();
        /// The name of the map of the host's with this hash, if a picture of
        /// it came; else null.
        const char *pictureName(std::uint32_t hash) const;
        /// What a console sent the game that is here has room for (KB, the
        /// least of them; 0: none, or not said): what the card says fits.
        void setRoom(std::uint32_t kb)
        {
            if (kb != room_)
            {
                room_ = kb;
                shownName_.clear(); // (said again)
            }
        }
        /// How the match is set up, said under the map's facts, or not (`stamp`:
        /// changes as that does, for the card to be made again).
        void setRules(bool show, unsigned stamp)
        {
            if (show != showRules_ || (show && stamp != rulesStamp_))
            {
                showRules_ = show;
                rulesStamp_ = stamp;
                shownName_.clear();
            }
        }
        bool shown() const { return text_ != nullptr; }
        /// Inside NEA_Process, after the picture behind it.
        void draw(void (*corner)(int sx, int sy, int s, int t));

    private:
        bool hidden_ = false;
        std::string shownName_;
        std::uint32_t room_ = 0;
        bool showRules_ = false;
        unsigned rulesStamp_ = 0;
        struct Given
        {
            std::uint8_t *data = nullptr; ///< a .hpv
            std::uint32_t size = 0;
            char name[24] = {};
        };
        static constexpr int kGiven = 24;
        Given given_[kGiven];
        int pictureW_ = hldata::kPreviewWidth, pictureH_ = hldata::kPreviewHeight;
        void *picture_ = nullptr; ///< (NEA_Material), when there is a picture
        void *text_ = nullptr;    ///< (NEA_Material)
        void *textPalette_ = nullptr;
        void release();
    };

    extern MapCard mapCard;

    /// Whether this console has a picture of `map` (on its card: a console
    /// without one has none).
    bool mapPictured(const std::string &map);
} // namespace hlrt
