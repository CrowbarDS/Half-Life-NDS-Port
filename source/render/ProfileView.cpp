// PROFILE's picture's way in: its gates (source/Overlay.hpp). The work is in
// the front end's overlay (ProfileViewBody.cpp), in the memory a map's
// archive index has while a map is mounted; it is read in when the picture
// is opened, and let go of, picture and all, before a map takes its place
// (hlrt::loadGame's leave checks).

#include "render/ProfileView.hpp"

#include "Overlay.hpp"

namespace hlrt
{
    ProfileView profileView;

    HL_OVL_GATED bool ProfileView::open(int top, int bottom, int character)
    {
#if HL_OVL_COUNT > 0
        // (A map still mounted has the memory: no picture beside it -- as
        // none is drawn beside one.)
        if (!ovl::tryLoad(ovl::Id::Front))
            return false;
#endif
        HL_OVL_GATE(Front);
        return openBody(top, bottom, character);
    }

    HL_OVL_GATED void ProfileView::setColours(int top, int bottom)
    {
        if (data_ == nullptr)
            return;
        HL_OVL_GATE(Front);
        setColoursBody(top, bottom);
    }

    HL_OVL_GATED void ProfileView::close()
    {
#if HL_OVL_COUNT > 0
        // (Without its overlay nothing of it is held: it was let go of before
        // the overlay left, and is read only with it there.)
        if (!ovl::loaded(ovl::Id::Front))
            return;
#endif
        HL_OVL_GATE(Front);
        closeBody();
    }

    HL_OVL_GATED void ProfileView::draw()
    {
        if (data_ == nullptr || hidden_)
            return;
        HL_OVL_GATE(Front);
        drawBody();
    }
} // namespace hlrt
