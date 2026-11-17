#pragma once
#include "AEffects_DX11.h"

//-----------------------------------------------------------------------------
// Purpose: Sun shafts (render.sun_shafts), ported from ERRender: the sky (the pixels without depth, with
// their colour) at half resolution is blurred radially towards the sun on screen (96 samples), tone mapped,
// softened with a dual Kawase pass and added onto the back buffer. The sun is the one the game shows
// (ASunFlareEffect, following the time of the day), the shafts fade with its strength (none at night) and
// as it leaves the middle of the view; nothing runs while it's behind the camera.
//-----------------------------------------------------------------------------
class ASunShafts
{
public:
	static TBOOL IsOn();

	// At the start of the post passes: where the sun is this frame. TTRUE when the shafts run, the core then
	// copies the scene (AEffects::GetSceneColourSRV) before the other effects change it
	static TBOOL Prepare( Toshi::TRenderContext* a_pMainContext );

	// After the other post effects; releases the targets when render.sun_shafts is off
	static void Render( Toshi::TRenderContext* a_pMainContext );

	// Releases the targets and shaders
	static void Destroy();
};
