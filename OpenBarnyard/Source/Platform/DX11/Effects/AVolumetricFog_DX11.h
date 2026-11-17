#pragma once
#include "AEffects_DX11.h"

//-----------------------------------------------------------------------------
// Purpose: Volumetric fog lit by the sun (render.volumetric_fog), ported from ERRender: at a quarter of the
// resolution each pixel marches its view ray (up to 44 units) through the sun's cascaded shadow maps
// (AShadowMaps::GetSunShadowSRV), in-scattering a warm light where the sun reaches, and the result is
// upsampled depth aware onto the back buffer. 1 = additive light, 2 = darkening of the shadowed volume.
// Without the sun shadows the additive light is the same integral with nothing in the way, worked out in
// closed form (a warm haze growing with the distance); the darkening needs them and stays off.
// The light follows the strength of the visible sun (ASunFlareEffect): none at night.
//-----------------------------------------------------------------------------
class AVolumetricFog
{
public:
	static TBOOL IsOn();

	// Between the post passes of AEffects::EndFrame; releases the targets when render.volumetric_fog is 0
	static void Render( Toshi::TRenderContext* a_pMainContext );

	// Releases the targets and shaders
	static void Destroy();
};
