#pragma once

namespace CoopMini
{
// Keep fractional frame time: resetting a 50 ms timer loses its remainder and
// falls to 15 snapshots/s on a 30 fps host. Never send a burst after a stall.
inline bool ViewSendDue(float& remaining, float elapsed)
{
    constexpr float interval = 1.0f / 30.0f;
    remaining -= elapsed > 0.0f ? elapsed : 0.0f;
    if (remaining > 0.000001f) return false;
    remaining += interval;
    if (remaining <= 0.000001f) remaining = interval;
    return true;
}
}
