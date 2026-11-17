#pragma once
#include "AEffectsCommon_DX11.h"

#include <Render/TViewport.h>
#include <Render/TCameraObject.h>

//-----------------------------------------------------------------------------
// Purpose: The shadow maps of the sun (CSM) and of the dynamic lights (plan section 3 d, e), ported from
// ERRender (CSMManager, DynamicGlowLights): private use only, ERRender has no licence.
//
// AEffects::RenderShadowPass calls Render after the animation update of ARenderer::RenderMainScene (the
// main viewport has begun and cleared), times it (AEFFECTS_TIMING_SHADOWS) and binds the main render
// target, depth buffer, viewport and render context again afterwards. Render draws the casters with the
// same calls as the main pass (models, terrain, gates, trees, instances, regrowth) from the light camera
// while AEffectsShadowPass::ms_bActive is set: the world, skin and grass HALs then draw depth only.
//
// The sun: render.sun_shadows 1/2/3 = 3 cascades of 1024/2048/4096, fitted to the main camera up to
// render.sun_shadow_distance, darkness render.sun_shadow_intensity, faded out at night.
// The receivers (world, skin and grass HALs, SUN_SHADOWS permutation) read the cascades from t8 / s8 / b1
// (AEffectsSlots). Those stay bound from Render until the next frame's Render, so the post passes (the
// volumetric fog) can read them too: GetSunShadowSRV, GetSunShadowConstants.
//-----------------------------------------------------------------------------
class AShadowMaps
{
public:
	static void Render( Toshi::TViewport* a_pViewport, Toshi::TCameraObject* a_pCameraObject );

	// Releases the shadow maps, AEffects::Destroy
	static void Destroy();

	// Feature level 10_1 (ps_4_1 compares samples of texture arrays): below it the shadows and lights are off
	static TBOOL IsSupported();

	//-----------------------------------------------------------------------------
	// For the receivers
	//-----------------------------------------------------------------------------

	// TRUE while what is drawn now receives the sun shadows: the shadow maps were rendered this frame, the main
	// pass of the frame runs (AEffects::IsInFrame), with the main render context, outside the shadow pass
	static TBOOL IsSunShadowReceiver();

	// HLSL put before the source of the receivers. With SUN_SHADOWS defined it declares cbuffer b1, t8 and s8
	// and float GetSunShadow( float3 a_vWorldPos, float a_fViewDepth ): how much of the sun reaches the
	// point, 1 lit, 1 - intensity in a full shadow (night fade included). The receivers scale the directional
	// part of their lighting with it (the vertex lighting of the world and the grass, the diffuse light of
	// the skins), so the dynamic shadows have the colour of the baked ones
	static const TCHAR* GetReceiverShaderSource();

	// Compiles a_szVSEntry and a_szPSEntry (TNULL: no pixel shader, depth only) of GetReceiverShaderSource() +
	// a_szSource with a_pDefines (TRenderD3DInterface::CompileShader, vs_4_1 / ps_4_1) and creates the input
	// layout of the vertex shader. Returns TFALSE and creates nothing when one of them fails or IsSupported is TFALSE
	static TBOOL CreateShaders( const TCHAR* a_szSource, LPCSTR a_szVSEntry, LPCSTR a_szPSEntry, const D3D_SHADER_MACRO* a_pDefines, const D3D11_INPUT_ELEMENT_DESC* a_pLayout, UINT a_uiNumElements, ID3D11VertexShader** a_ppVS, ID3D11PixelShader** a_ppPS, ID3D11InputLayout** a_ppLayout );
	static void  ReleaseShaders( ID3D11VertexShader** a_ppVS, ID3D11PixelShader** a_ppPS, ID3D11InputLayout** a_ppLayout );

	// The defines of a receiver permutation: bit 0 SUN_SHADOWS, bit 1 DYNAMIC_LIGHTS, bit 2 ALPHA_COVERAGE
	static const D3D_SHADER_MACRO* GetReceiverDefines( TUINT a_uiPermutation );

	// TRUE in the main pass while alpha to coverage is on (render.alpha_coverage with an MSAA scene target): the HALs
	// draw their packets without blending with the ALPHA_COVERAGE permutation. With ALPHA_COVERAGE the receiver
	// source declares uint AlphaTestCoverage( float a_fAlpha, float4 a_vAlphaTestParams, float2 a_vPixel ), the
	// SV_Coverage of the pixel instead of AlphaTest (it discards the pixels that cover no sample)
	static TBOOL IsCoveragePass();

	// TRUE in the main pass of a frame that had a shadow pass (AEffects::IsInFrame, with the main render
	// context, outside the shadow pass): the receivers may use the effects of the frame
	static TBOOL IsReceiverPass();

	// The cascades of this frame (TNULL when the sun shadows are off), for the post passes
	static ID3D11ShaderResourceView*   GetSunShadowSRV();
	static ID3D11SamplerState*         GetSunShadowSampler();
	static const AEffectsSunShadowsCB& GetSunShadowConstants();

	// The direction the sunlight travels (from the sky to the ground), as the shadows use it
	static const Toshi::TVector3& GetSunDirection();

	// 0 at night to 1 by day: the sun shadows go down over the dusk and come up over the dawn (AGameTimeManager)
	static TFLOAT GetSunFade();
};
