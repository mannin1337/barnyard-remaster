#pragma once
#include "AEffects_DX11.h"

//-----------------------------------------------------------------------------
// Purpose: Glow bloom (render.bloom), ported from ERRender: the glowing meshes (TMaterial::FLAGS_GLOW:
// lamps and windows of the terrain, the minigame objects of AAnimatableObject::SetGlow) are drawn a
// second time into the glow target (the scene's size and sample count, sharing its depth buffer), which
// is blurred with a dual Kawase chain on R11G11B10 levels (the first at half the size, then halving) and
// added onto the back buffer. The meshes themselves look as without it, the bloom adds the halo. Nothing
// runs in a frame without glow.
//-----------------------------------------------------------------------------
class ABloom
{
public:
	static TBOOL IsOn();

	// AEffects::BeginFrame: the glow target of this frame, TNULL without bloom. It's cleared when it's new or
	// a_bGlowDrawnLastFrame (a glowing mesh was drawn into it last frame)
	static ID3D11RenderTargetView* BeginFrame( UINT a_uiSamples, TBOOL a_bGlowDrawnLastFrame );

	// Between the post passes of AEffects::EndFrame. a_bGlowDrawn: a glowing mesh was drawn this frame
	static void Render( TBOOL a_bGlowDrawn );

	// Releases the targets and shaders
	static void Destroy();

	// The dual Kawase down and up shaders (g_avParams[ 0 ]: texel size of the source, offset; input t10), also
	// for the other effects that blur (ASunShafts). TFALSE when they don't compile
	static TBOOL GetKawaseShaders( ID3D11PixelShader*& a_rpDown, ID3D11PixelShader*& a_rpUp );
};
