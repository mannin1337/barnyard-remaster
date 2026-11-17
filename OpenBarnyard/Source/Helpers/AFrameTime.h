#pragma once
#include <math.h>

#ifdef BARNYARD_COMMUNITY_PATCH

//-----------------------------------------------------------------------------
// NOTE: community patch. The original runs its logic once per frame with the
// real frame time, but a few places change a value by a fixed amount or blend
// it by a fixed factor per update. They were tuned for 30 updates a second
// (the rate of its fixed steps, e.g. the TIME_STEP of the steering kinematics),
// so at 60 FPS they ran twice as fast and at a few hundred they ran wild. These
// give the result of the original at 30 FPS, and the same result per second at
// any frame rate.
//-----------------------------------------------------------------------------
namespace AFrameTime
{

// The update rate the per-update steps and factors of the original were tuned for
constexpr TFLOAT ORIGINAL_RATE = 30.0f;

// a_fStep per update at 30 FPS, for an update of a_fDeltaTime seconds
inline TFLOAT Step( TFLOAT a_fStep, TFLOAT a_fDeltaTime )
{
	return a_fStep * ( a_fDeltaTime * ORIGINAL_RATE );
}

// The factor for an update of a_fDeltaTime seconds of a blend (TMath::LERP( current, target, factor )) that moves
// a_fFactor of the way to its target per update at 30 FPS
inline TFLOAT BlendFactor( TFLOAT a_fFactor, TFLOAT a_fDeltaTime )
{
	return 1.0f - powf( 1.0f - a_fFactor, a_fDeltaTime * ORIGINAL_RATE );
}

} // namespace AFrameTime

#endif // BARNYARD_COMMUNITY_PATCH
