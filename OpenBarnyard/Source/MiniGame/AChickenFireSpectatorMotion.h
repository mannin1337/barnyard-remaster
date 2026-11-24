#pragma once
#include "ASpectatorBlendClock.h"

namespace ChickenSpectator
{
template<class Frame> class Playback
{
    Frame start{}, current{};
    unsigned lastSequence = 0, lastMatch = 0;
    ASpectatorBlendClock timing;

public:
    template<class Blend>
    bool Render(const Frame& incoming, unsigned sequence, unsigned match, float dt,
        Frame& shown, float& alpha, Blend blend)
    {
        const bool cut = !lastSequence || match != lastMatch;
        const bool fresh = cut || sequence != lastSequence;
        if (fresh)
        {
            if (!cut) start = current;
            lastSequence = sequence;
            lastMatch = match;
        }
        alpha = timing.Advance(dt, fresh, cut);
        // The game passes the same frame for incoming and shown. Initialize
        // from the newest sample before smoothing only its continuous fields.
        shown = incoming;
        if (alpha < 1.0f) blend(shown, start, alpha);
        current = shown;
        return fresh;
    }
};
}
