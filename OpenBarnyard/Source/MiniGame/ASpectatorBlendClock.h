#pragma once

#include <cmath>

// Blend toward the newest pose immediately, for at most one normal 30 Hz
// interval. Faster sources need less time. After a long gap, show the new
// pose directly rather than playing through stale motion.
struct ASpectatorBlendClock
{
    float gap = 0, elapsed = 0, duration = 0;

    bool IsBlending() const { return elapsed < duration; }

    float Advance(float dt, bool fresh, bool cut)
    {
        if (!std::isfinite(dt) || dt < 0) dt = 0;
        gap += dt;
        if (fresh)
        {
            duration = cut || gap >= 0.2f ? 0.0f : std::fmin(gap, 1.0f / 30.0f);
            gap = elapsed = 0;
        }
        elapsed += dt;
        return IsBlending() ? elapsed / duration : 1.0f;
    }
};
