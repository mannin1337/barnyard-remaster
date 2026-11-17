#pragma once
#include "AEffects_DX11.h"

//-----------------------------------------------------------------------------
// Purpose: Screen space ambient occlusion (render.ao: HBAO or GTAO), ported from ERRender: the AO is
// computed from the depth of the scene at half (render.ao_quality 0) or full resolution, blurred
// with a depth aware separable blur and multiplied onto the back buffer. With MSAA the depth is resolved
// first (its first sample into an R32F target), without MSAA the depth buffer is read directly.
// It fades out with the linear fog of the main render context, the fogged distance keeps its colour.
//-----------------------------------------------------------------------------
class AAmbientOcclusion
{
public:
	static TBOOL IsOn();

	// Between the post passes of AEffects::EndFrame
	static void Render( Toshi::TRenderContext* a_pMainContext );

	// Releases the targets and shaders
	static void Destroy();
};
