#pragma once

namespace CoopMini
{
// Keep fractional frame time at mixed frame rates. After a stall only the
// latest pose is useful; sending missed poses back-to-back adds queueing.
inline bool ViewSendDue(float& remaining, float elapsed, float interval = 1.0f / 30.0f)
{
    remaining -= elapsed > 0.0f ? elapsed : 0.0f;
    if (remaining > 0.000001f) return false;
    remaining += interval;
    if (remaining <= 0.000001f) remaining = interval;
    return true;
}
}
