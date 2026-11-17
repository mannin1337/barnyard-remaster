#pragma once
#include "AEffectsCommon_DX11.h"

#include <Render/TViewport.h>
#include <Render/TCameraObject.h>

#include <d3d11.h>

//-----------------------------------------------------------------------------
// A render target of the effects: texture, render target view and shader view
//-----------------------------------------------------------------------------
struct AEffectsTarget
{
	ID3D11Texture2D*          pTexture  = TNULL;
	ID3D11RenderTargetView*   pRTV      = TNULL;
	ID3D11ShaderResourceView* pSRV      = TNULL; // a Texture2DMS view with MSAA
	UINT                      uiWidth   = 0;
	UINT                      uiHeight  = 0;
	UINT                      uiSamples = 0;
	DXGI_FORMAT               eFormat   = DXGI_FORMAT_UNKNOWN;

	TBOOL IsCreated() const { return pTexture != TNULL; }
	TBOOL Matches( UINT a_uiWidth, UINT a_uiHeight, DXGI_FORMAT a_eFormat, UINT a_uiSamples ) const { return pTexture && uiWidth == a_uiWidth && uiHeight == a_uiHeight && eFormat == a_eFormat && uiSamples == a_uiSamples; }
	TFLOAT GetMegabytes() const;
};

//-----------------------------------------------------------------------------
// Purpose: The frame of the DX11 effects (BARNYARD_COMMUNITY_PATCH).
// ARenderer::RenderMainScene calls BeginFrame before the main viewport begins, RenderShadowPass after
// the animation update and EndFrame after the overlay pass. With MSAA the scene renders into the scene
// target of the renderer; EndFrame resolves it into the back buffer once, then the post effects run on
// the back buffer (ambient occlusion and fog as multiply blends, bloom added), and the GUI comes after.
// While MSAA is 1 and every post effect is off nothing is allocated and nothing changes (no scene target).
// The effects read AEffectsSettings every frame.
//-----------------------------------------------------------------------------
class AEffects
{
public:
	// The console variables of AEffectsSettings (APrimeDebug::RegisterSettings / UnregisterSettings)
	static void RegisterSettings();
	static void UnregisterSettings();

	//-----------------------------------------------------------------------------
	// The frame
	//-----------------------------------------------------------------------------

	// Before the main viewport begins: the scene target (MSAA) becomes the render target, the glow target is cleared
	static void BeginFrame( Toshi::TRenderContext* a_pMainContext );

	// After the animation update, the main viewport has begun: the shadow maps (AShadowMaps), then the main
	// render target, depth buffer, viewport and render context are bound again
	static void RenderShadowPass( Toshi::TViewport* a_pViewport, Toshi::TCameraObject* a_pCameraObject );

	// After the main pass, before the overlay pass, which may clear the depth (the golf and pool minigames): the post
	// effects that read the depth (GetDepthSRV) get it as it is now
	static void CaptureDepth( Toshi::TRenderContext* a_pMainContext );

	// After the overlay pass, with the main render context current: resolve, post effects, back buffer bound again
	static void EndFrame( Toshi::TRenderContext* a_pMainContext );

	// Releases everything, ARenderer::OnDestroy
	static void Destroy();

	//-----------------------------------------------------------------------------
	// For the shader HALs
	//-----------------------------------------------------------------------------

	// Between BeginFrame and EndFrame
	static TBOOL IsInFrame();

	// Sample count of what the scene renders into: the scene target, or 1 for the back buffer
	static UINT GetSceneSampleCount();

	// Bloom: a glowing mesh (TMaterial::FLAGS_GLOW) is drawn a second time, into the glow target, between
	// BeginGlowDraw and EndGlowDraw with the same states (no depth write). BeginGlowDraw returns TFALSE when
	// it can't be done now (no glow target, or something else than the scene is bound, e.g. a render to
	// texture). Flush the strip batch before the first draw
	static TBOOL IsGlowTargetActive();
	static TBOOL BeginGlowDraw();
	static void  EndGlowDraw();

	// GPU times (render.fx_timings), a pass at most once a frame
	static void BeginTiming( AEffectsTiming a_eTiming );
	static void EndTiming( AEffectsTiming a_eTiming );

	//-----------------------------------------------------------------------------
	// For the post effects
	//-----------------------------------------------------------------------------

	enum POSTBLEND
	{
		POSTBLEND_NONE,     // replaces the target
		POSTBLEND_ADD,      // target + source
		POSTBLEND_MULTIPLY, // target * source
	};

	// The pixel shader of a fullscreen pass. a_szSource gets a header with FULLSCREEN_PS_IN (vPosition, vUV),
	// cbuffer PostConstants : register( b3 ) { float4 g_avParams[ 8 ]; } and the samplers g_PointClamp ( s10 ),
	// g_LinearClamp ( s11 ); its inputs go to t10-t13 (AEffectsSlots::T_POST_*). TNULL when it doesn't compile.
	// ps_4_1 (feature level 10_1) is needed to read an MSAA texture without a fixed sample count
	static ID3D11PixelShader* CompilePostShader( const TCHAR* a_szSource, LPCSTR a_szEntryPoint, const D3D_SHADER_MACRO* a_pDefines = TNULL, LPCSTR a_szTarget = "ps_4_0" );

	static TBOOL CreateTarget( AEffectsTarget& a_rTarget, const TCHAR* a_szName, UINT a_uiWidth, UINT a_uiHeight, DXGI_FORMAT a_eFormat, UINT a_uiSamples = 1 );
	static void  DestroyTarget( AEffectsTarget& a_rTarget );

	// Between BeginPostPasses and EndPostPasses (EndFrame calls them): render target without depth buffer
	// and a viewport of its size, blending, inputs (unbound by EndPostPasses), constants (b3), the draw
	static void BindPostTarget( ID3D11RenderTargetView* a_pRTV, UINT a_uiWidth, UINT a_uiHeight );
	static void SetPostBlend( POSTBLEND a_eBlend );
	static void SetPostInput( UINT a_uiSlot, ID3D11ShaderResourceView* a_pSRV );
	static void SetPostConstants( const AEffectsPostCB& a_rcConstants );
	static void DrawPost( ID3D11PixelShader* a_pPixelShader );

	// Size of the back buffer, the post effects work on it
	static UINT GetWidth();
	static UINT GetHeight();

	// The depth of the scene: the scene target's (a Texture2DMS view with MSAA) or the back buffer's
	static ID3D11ShaderResourceView* GetSceneDepthSRV();

	// The depth of the scene as a Texture2D any pass can sample, as CaptureDepth took it before the overlay pass: a
	// copy of the depth buffer without MSAA, with MSAA its first sample resolved into R32F (TNULL if that fails).
	// Without CaptureDepth this frame: the depth buffer itself, or the resolve on the first call
	static ID3D11ShaderResourceView* GetDepthSRV();

	// A copy of the back buffer made right after the resolve, before the post effects change it (TNULL when no
	// effect asked for it this frame, see ASunShafts::Prepare)
	static ID3D11ShaderResourceView* GetSceneColourSRV();
};
