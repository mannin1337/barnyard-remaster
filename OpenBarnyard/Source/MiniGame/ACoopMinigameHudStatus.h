#pragma once
#include "Player/ACoopMinigameProtocol.h"
#include <algorithm>

namespace CoopMiniHud
{
template <class GetScreenMask>
inline bool ShowsMatchStatus(CoopMini::U32 phase, bool hasText, CoopMini::U32 elementMask, GetScreenMask getScreenMask)
{
    return phase != CoopMini::Idle && phase != CoopMini::Results && hasText && (getScreenMask() & elementMask) != 0;
}

inline float PartnerStatusAlpha(float previous, float dt, bool hasPartnerStatus, bool matchStatusVisible)
{
    // Both text boxes are anchored at the top. Yield before the scorecard draws its first frame.
    if (matchStatusVisible) return 0.0f;
    return std::clamp(previous + (hasPartnerStatus ? 4.0f : -4.0f) * dt, 0.0f, 1.0f);
}
}
