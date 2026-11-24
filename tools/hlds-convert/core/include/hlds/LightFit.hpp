#pragma once

// The light each corner of a face is given: chosen to match the face's
// lightmap best, not read where the corner is.
//
// The console lights a face by its corners alone (LightBake.hpp): the
// hardware blends their colours across it. A corner read where it lies takes
// whatever the lightmap has there -- the dark strip of a door that goes into
// its frame, the edge of a pool of lamp light -- and spreads it over the
// whole face, a little differently from its neighbour's read of the same
// corner. Here every lightmap sample of a face that is seen (none inside the
// world's solid) is an equation over the corners it lies between, and the
// corners are solved for together: a corner the faces of one model on one
// plane share is one value, so they meet without a step. A sample far from
// what the others make of the face counts less, pass after pass (the odd
// dark strip is outvoted, not averaged in); each corner is held a little to
// its own read, so that a face with few samples stays as it was.
//
// Faces any light that changes falls on are not fitted: their corners are
// read, as LightPatch needs them to be (the console adds each style's light
// to that read).

#include <map>
#include <utility>
#include <vector>

#include "hlds/BspFile.hpp"
#include "hlds/LightBake.hpp"

namespace hlds
{
    class LightFit
    {
    public:
        /// `origins`: for each of the map's models, where its entity put it
        /// (the world's: 0 0 0), or null for a model whose faces are not
        /// lit (blended, a flat colour, not drawn).
        LightFit(const BspFile &bsp, const LightBake &light, const std::vector<const double *> &origins);

        /// The light chosen for corner `corner` of face `face`; false if
        /// the face was not fitted (it is read: LightBake::sample).
        bool corner(int face, int corner, Rgb &out) const;

    private:
        std::map<std::pair<int, int>, Rgb> chosen_;
    };
} // namespace hlds
