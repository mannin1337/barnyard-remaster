#include "pch.h"
#include "ASunShafts_DX11.h"
#include "ABloom_DX11.h"
#include "Render/ASunFlareEffect.h"

#include <Platform/DX11/TRenderInterface_DX11.h>
#include <Platform/DX11/TRenderContext_DX11.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

//-----------------------------------------------------------------------------
// Shaders: SkyMask, SunShafts (with Tonemap.hlsli) and CopyTexture of ERRender (Resources/Shaders) on the
// fullscreen triangle of the renderer. The blur is the bloom's dual Kawase (ABloom::GetKawaseShaders)
//-----------------------------------------------------------------------------

// The sky: what has no depth keeps its colour. t10 depth (AEffects::GetDepthSRV), t11 the scene
static const TCHAR s_szSkyMaskShader[] = R"(
Texture2D g_Depth : register( t10 );
Texture2D g_Scene : register( t11 );

float4 ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float fDepth = g_Depth.SampleLevel( g_PointClamp, i.vUV, 0 ).r;

	if ( fDepth < 0.99999f )
		return float4( 0.0f, 0.0f, 0.0f, 0.0f );

	return float4( g_Scene.SampleLevel( g_LinearClamp, i.vUV, 0 ).rgb, 1.0f );
}
)";

// g_avParams[ 0 ]: sun position (UV), alpha, length of the rays; [ 1 ]: tint. t10 the sky mask
static const TCHAR s_szShaftsShader[] = R"(
Texture2D g_SkyMask : register( t10 );

static const int   SAMPLE_COUNT     = 96;
static const float INV_SAMPLE_COUNT = 1.0f / (float)SAMPLE_COUNT;

float3 Uncharted2Tonemap( float3 x )
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	return ( ( x * ( A * x + C * B ) + D * E ) / ( x * ( A * x + B ) + D * F ) ) - E / F;
}

float4 ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float2 uv    = i.vUV;
	float2 uvDir = ( uv - g_avParams[ 0 ].xy ) * ( g_avParams[ 0 ].w * INV_SAMPLE_COUNT );

	float3 rayColor = (float3)0.0f;

	[loop] for ( int s = 1; s <= SAMPLE_COUNT; ++s )
	{
		float  sampleFrac     = (float)s * INV_SAMPLE_COUNT;
		float  fadeTowardsSky = 1.0f - sqrt( sampleFrac );
		float3 maskSample     = g_SkyMask.SampleLevel( g_LinearClamp, uv, 0 ).rgb;

		// Samples near the border of the screen fade, so the edge doesn't cut the rays
		float2 borderDist = min( uv, 1.0f - uv );
		float  borderFade = saturate( min( borderDist.x, borderDist.y ) / 0.05f );

		rayColor += maskSample * fadeTowardsSky * borderFade;
		uv -= uvDir;
	}

	rayColor = Uncharted2Tonemap( rayColor * g_avParams[ 1 ].rgb * g_avParams[ 0 ].z );
	return float4( rayColor, 1.0f );
}
)";

// Added onto the scene, t10 the shafts
static const TCHAR s_szCopyShader[] = R"(
Texture2D g_Source : register( t10 );

float4 ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	return float4( g_Source.SampleLevel( g_LinearClamp, i.vUV, 0 ).rgb, 1.0f );
}
)";

// ERRender's defaults (ERRenderWrapper.cpp)
static constexpr TFLOAT RAYS_LENGTH       = 0.3f;
static constexpr TFLOAT TINT_R            = 1.0f;
static constexpr TFLOAT TINT_G            = 0.91f;
static constexpr TFLOAT TINT_B            = 0.8f;
static constexpr TFLOAT KAWASE_OFFSET     = 1.0f;
static constexpr TUINT  LOG_INTERVAL      = 120; // frames between the [fx] lines with render.fx_timings

static ID3D11PixelShader* s_pSkyMaskPS;
static ID3D11PixelShader* s_pShaftsPS;
static ID3D11PixelShader* s_pCopyPS;
static TBOOL              s_bShadersFailed;

static AEffectsTarget s_oSkyMask; // half resolution
static AEffectsTarget s_oShafts;  // half resolution
static AEffectsTarget s_oLevel;   // a quarter, for the blur

// This frame, from Prepare
static TBOOL    s_bRunThisFrame;
static TVector4 s_vSunUV; // xy
static TFLOAT   s_fAlpha;
static TUINT    s_uiLogCounter;

static void ReleaseShader( ID3D11PixelShader*& a_rpShader )
{
	if ( a_rpShader )
	{
		a_rpShader->Release();
		a_rpShader = TNULL;
	}
}

static void DestroyTargets()
{
	AEffects::DestroyTarget( s_oSkyMask );
	AEffects::DestroyTarget( s_oShafts );
	AEffects::DestroyTarget( s_oLevel );
}

static TBOOL CreateShaders()
{
	if ( s_pCopyPS )
		return TTRUE;

	if ( s_bShadersFailed )
		return TFALSE;

	s_pSkyMaskPS = AEffects::CompilePostShader( s_szSkyMaskShader, "ps_main" );
	s_pShaftsPS  = AEffects::CompilePostShader( s_szShaftsShader, "ps_main" );
	s_pCopyPS    = AEffects::CompilePostShader( s_szCopyShader, "ps_main" );

	if ( !s_pSkyMaskPS || !s_pShaftsPS || !s_pCopyPS )
	{
		TERROR( "[fx] the sun shaft shaders don't compile, they stay off\n" );
		ReleaseShader( s_pSkyMaskPS );
		ReleaseShader( s_pShaftsPS );
		ReleaseShader( s_pCopyPS );
		s_bShadersFailed = TTRUE;
		return TFALSE;
	}

	return TTRUE;
}

TBOOL ASunShafts::IsOn()
{
	return AEffectsSettings::ms_iSunShafts != 0 && !s_bShadersFailed;
}

TBOOL ASunShafts::Prepare( TRenderContext* a_pMainContext )
{
	s_bRunThisFrame = TFALSE;

	if ( !IsOn() || !ASunFlareEffect::IsSingletonCreated() )
		return TFALSE;

	ASunFlareEffect* pSun      = ASunFlareEffect::GetSingleton();
	TFLOAT           fStrength = pSun->GetSunIntensity();

	if ( fStrength > 1.0f ) fStrength = 1.0f;
	if ( !pSun->IsEnabled() || fStrength <= 0.01f )
		return TFALSE;

	// The direction towards the sun in view space (z forward, y down)
	TVector4 vSunDir = pSun->GetSunDirection();
	vSunDir.w        = 0.0f;

	TVector4 vSunView;
	TMatrix44::RotateVector( vSunView, a_pMainContext->GetWorldViewMatrix(), vSunDir );

	const TFLOAT fLength = TMath::Sqrt( vSunView.x * vSunView.x + vSunView.y * vSunView.y + vSunView.z * vSunView.z );

	if ( fLength <= 0.0001f )
		return TFALSE;

	const TFLOAT fFacing = vSunView.z / fLength;

	// ERRender's fade: none below 0.1 (about 84 degrees off the view), smooth up to 0.75
	TFLOAT fAngleFade = ( fFacing - 0.10f ) / 0.65f;
	TMath::Clip( fAngleFade, 0.0f, 1.0f );
	fAngleFade = fAngleFade * fAngleFade * ( 3.0f - 2.0f * fAngleFade );
	fAngleFade *= fAngleFade;

	const TFLOAT fAlpha = AEffectsSettings::ms_fSunShaftsIntensity * fAngleFade * fStrength;

	// Onto the screen with the projection of the main pass, as ASunFlareEffect places the sun
	const TMatrix44& rcProjection = TRenderContextD3D::Upcast( a_pMainContext )->GetProjectionMatrix();
	const TFLOAT     fInvZ        = 1.0f / TMath::Max( vSunView.z, 0.0001f );
	const TFLOAT     fNDCX        = vSunView.x * fInvZ * rcProjection.m_f11 + rcProjection.m_f31;
	const TFLOAT     fNDCY        = vSunView.y * fInvZ * rcProjection.m_f22 + rcProjection.m_f32;

	if ( AEffectsSettings::ms_iFXTimings && ++s_uiLogCounter >= LOG_INTERVAL )
	{
		s_uiLogCounter = 0;
		TINFO( "[fx] sun shafts: sun facing %.2f at (%.2f, %.2f), strength %.2f, alpha %.4f\n", fFacing, fNDCX * 0.5f + 0.5f, 0.5f - fNDCY * 0.5f, fStrength, fAlpha );
	}

	if ( fAlpha <= 0.0001f )
		return TFALSE;

	s_vSunUV        = TVector4( fNDCX * 0.5f + 0.5f, 0.5f - fNDCY * 0.5f, 0.0f, 0.0f );
	s_fAlpha        = fAlpha;
	s_bRunThisFrame = TTRUE;
	return TTRUE;
}

void ASunShafts::Render( TRenderContext* a_pMainContext )
{
	if ( !IsOn() )
	{
		// Turned off: the memory goes
		if ( s_oSkyMask.IsCreated() || s_oShafts.IsCreated() || s_oLevel.IsCreated() )
			DestroyTargets();

		return;
	}

	if ( !s_bRunThisFrame )
		return;

	s_bRunThisFrame = TFALSE;

	ID3D11PixelShader* pKawaseDownPS = TNULL;
	ID3D11PixelShader* pKawaseUpPS   = TNULL;

	if ( !CreateShaders() || !ABloom::GetKawaseShaders( pKawaseDownPS, pKawaseUpPS ) )
		return;

	const UINT uiWidth   = AEffects::GetWidth();
	const UINT uiHeight  = AEffects::GetHeight();
	const UINT uiHalfW   = TMath::Max( uiWidth >> 1, 1u );
	const UINT uiHalfH   = TMath::Max( uiHeight >> 1, 1u );
	const UINT uiQuarterW = TMath::Max( uiWidth >> 2, 1u );
	const UINT uiQuarterH = TMath::Max( uiHeight >> 2, 1u );

	if ( ( !s_oSkyMask.Matches( uiHalfW, uiHalfH, DXGI_FORMAT_R8G8B8A8_UNORM, 1 ) && !AEffects::CreateTarget( s_oSkyMask, "sky mask", uiHalfW, uiHalfH, DXGI_FORMAT_R8G8B8A8_UNORM ) ) ||
	     ( !s_oShafts.Matches( uiHalfW, uiHalfH, DXGI_FORMAT_R8G8B8A8_UNORM, 1 ) && !AEffects::CreateTarget( s_oShafts, "sun shafts", uiHalfW, uiHalfH, DXGI_FORMAT_R8G8B8A8_UNORM ) ) ||
	     ( !s_oLevel.Matches( uiQuarterW, uiQuarterH, DXGI_FORMAT_R11G11B10_FLOAT, 1 ) && !AEffects::CreateTarget( s_oLevel, "sun shafts blur", uiQuarterW, uiQuarterH, DXGI_FORMAT_R11G11B10_FLOAT ) ) )
		return;

	AEffects::BeginTiming( AEFFECTS_TIMING_SUNSHAFTS );

	ID3D11ShaderResourceView* pDepthSRV = AEffects::GetDepthSRV();
	ID3D11ShaderResourceView* pSceneSRV = AEffects::GetSceneColourSRV();

	if ( !pDepthSRV || !pSceneSRV )
	{
		AEffects::EndTiming( AEFFECTS_TIMING_SUNSHAFTS );
		return;
	}

	// The sky
	AEffects::BindPostTarget( s_oSkyMask.pRTV, uiHalfW, uiHalfH );
	AEffects::SetPostBlend( AEffects::POSTBLEND_NONE );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, pDepthSRV );
	AEffects::SetPostInput( AEffectsSlots::T_POST_1, pSceneSRV );
	AEffects::DrawPost( s_pSkyMaskPS );

	// The rays
	AEffectsPostCB oConstants = {};
	oConstants.avParams[ 0 ]  = TVector4( s_vSunUV.x, s_vSunUV.y, s_fAlpha, RAYS_LENGTH );
	oConstants.avParams[ 1 ]  = TVector4( TINT_R, TINT_G, TINT_B, 0.0f );
	AEffects::SetPostConstants( oConstants );

	AEffects::BindPostTarget( s_oShafts.pRTV, uiHalfW, uiHalfH );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, s_oSkyMask.pSRV );
	AEffects::DrawPost( s_pShaftsPS );

	// Softened: down to a quarter and up again (one level of ERRender's Kawase)
	AEffectsPostCB oBlur = {};
	oBlur.avParams[ 0 ]  = TVector4( 1.0f / uiHalfW, 1.0f / uiHalfH, KAWASE_OFFSET, 0.0f );
	AEffects::SetPostConstants( oBlur );
	AEffects::BindPostTarget( s_oLevel.pRTV, uiQuarterW, uiQuarterH );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, s_oShafts.pSRV );
	AEffects::DrawPost( pKawaseDownPS );

	oBlur.avParams[ 0 ] = TVector4( 1.0f / uiQuarterW, 1.0f / uiQuarterH, KAWASE_OFFSET, 0.0f );
	AEffects::SetPostConstants( oBlur );
	AEffects::BindPostTarget( s_oShafts.pRTV, uiHalfW, uiHalfH );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, s_oLevel.pSRV );
	AEffects::DrawPost( pKawaseUpPS );

	// Added onto the scene
	AEffects::BindPostTarget( TRenderD3DInterface::Interface()->GetBackBufferRTView(), uiWidth, uiHeight );
	AEffects::SetPostBlend( AEffects::POSTBLEND_ADD );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, s_oShafts.pSRV );
	AEffects::DrawPost( s_pCopyPS );

	AEffects::EndTiming( AEFFECTS_TIMING_SUNSHAFTS );
}

void ASunShafts::Destroy()
{
	DestroyTargets();
	ReleaseShader( s_pSkyMaskPS );
	ReleaseShader( s_pShaftsPS );
	ReleaseShader( s_pCopyPS );
	s_bShadersFailed = TFALSE;
	s_bRunThisFrame  = TFALSE;
}
