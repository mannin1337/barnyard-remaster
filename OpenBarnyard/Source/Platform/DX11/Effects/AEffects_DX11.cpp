#include "pch.h"
#include "AEffects_DX11.h"
#include "AShadowMaps_DX11.h"
#include "AAmbientOcclusion_DX11.h"
#include "ABloom_DX11.h"
#include "ASunShafts_DX11.h"
#include "AVolumetricFog_DX11.h"

#include <Platform/DX11/TRenderInterface_DX11.h>
#include <Platform/DX11/TRenderContext_DX11.h>
#include <Platform/DX11/TConstantBuffer_DX11.h>

#include <PrimeDebug.h>
#include <GUI/T2GUIFontSDF.h>

#ifdef PRIMEDEBUG
#  include <Console/PCVar.h>
#endif

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

//-----------------------------------------------------------------------------
// Settings (the defaults of the plan, dx11_effektek_terv.md)
//-----------------------------------------------------------------------------
TINT   AEffectsSettings::ms_iMSAA                = 4;
TINT   AEffectsSettings::ms_iAnisotropy          = 16;
TINT   AEffectsSettings::ms_iAlphaCoverage       = 1;
TINT   AEffectsSettings::ms_iAO                  = 2;
TINT   AEffectsSettings::ms_iAOQuality           = 0;
TFLOAT AEffectsSettings::ms_fAORadius            = 0.7f;
TFLOAT AEffectsSettings::ms_fAOIntensity         = 1.0f;
TINT   AEffectsSettings::ms_iAODebug             = 0;
TINT   AEffectsSettings::ms_iBloom               = 1;
TFLOAT AEffectsSettings::ms_fBloomIntensity      = 0.715f;
TINT   AEffectsSettings::ms_iBloomLevels         = 3;
TINT   AEffectsSettings::ms_iDynamicLights       = 1;
TINT   AEffectsSettings::ms_iDynamicLightShadows = 2;
TINT   AEffectsSettings::ms_iDynamicLightFlicker = 1;
TINT   AEffectsSettings::ms_iSunShadows          = 2;
TFLOAT AEffectsSettings::ms_fSunShadowDistance   = 56.0f;
TFLOAT AEffectsSettings::ms_fSunShadowIntensity  = 0.8f;
TINT   AEffectsSettings::ms_iSunDirOverride      = 0;
TINT   AEffectsSettings::ms_iShadowOffscreen     = 1;
TINT   AEffectsSettings::ms_iSunShafts           = 0;
TFLOAT AEffectsSettings::ms_fSunShaftsIntensity  = 0.04f;
TINT   AEffectsSettings::ms_iVolumetricFog       = 0;
TINT   AEffectsSettings::ms_iHDFonts             = 1;
TINT   AEffectsSettings::ms_iFXTimings           = 0;

TBOOL     AEffectsShadowPass::ms_bActive = TFALSE;
TINT      AEffectsShadowPass::ms_iSlice  = 0;
TMatrix44 AEffectsShadowPass::ms_mLightProjection;

#ifdef PRIMEDEBUG
static PCVar s_aSettingVars[ 24 ];
static TINT  s_iNumSettingVars;

void AEffects::RegisterSettings()
{
	using S                        = AEffectsSettings;
	constexpr TUINT16 RANGE        = PCVar::FLAGS_VALIDATERANGE;
	constexpr TUINT16 RANGE_BOOL   = PCVar::FLAGS_VALIDATERANGE | PCVar::FLAGS_BOOL;
	constexpr TUINT16 RANGE_ENUM   = PCVar::FLAGS_VALIDATERANGE | PCVar::FLAGS_ENUM;
	PCVar*            pVar         = s_aSettingVars;

	( pVar++ )->CreateIntRefRange( S::MSAA, "Multisample anti-aliasing of the 3D scene: 1 (off), 2, 4 or 8 samples (the highest the GPU has up to it)", RANGE, &S::ms_iMSAA, TNULL, TNULL, 1, 8, 1 );
	( pVar++ )->CreateIntRefRange( S::ANISOTROPY, "Anisotropic texture filtering: 1 (off) to 16 samples", RANGE, &S::ms_iAnisotropy, TNULL, TNULL, 1, 16, 1 );
	( pVar++ )->CreateIntRefRange( S::ALPHA_COVERAGE, "Smooth MSAA edges of the alpha tested foliage, grass and fences (alpha to coverage)", RANGE_BOOL, &S::ms_iAlphaCoverage, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateIntRefRange( S::AO, "Ambient occlusion (0=Off,1=HBAO,2=GTAO)", RANGE_ENUM, &S::ms_iAO, TNULL, TNULL, 0, 2, 1 );
	( pVar++ )->CreateIntRefRange( S::AO_QUALITY, "Resolution of the ambient occlusion (0=Half,1=Full)", RANGE_ENUM, &S::ms_iAOQuality, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateFloatRefRange( S::AO_RADIUS, "Radius of the ambient occlusion in world units", RANGE, &S::ms_fAORadius, TNULL, TNULL, 0.1f, 4.0f, 0.05f );
	( pVar++ )->CreateFloatRefRange( S::AO_INTENSITY, "Strength of the ambient occlusion", RANGE, &S::ms_fAOIntensity, TNULL, TNULL, 0.0f, 4.0f, 0.05f );
	( pVar++ )->CreateIntRefRange( S::AO_DEBUG, "Shows the ambient occlusion alone", RANGE_BOOL, &S::ms_iAODebug, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateIntRefRange( S::BLOOM, "Bloom around glowing objects", RANGE_BOOL, &S::ms_iBloom, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateFloatRefRange( S::BLOOM_INTENSITY, "Strength of the bloom", RANGE, &S::ms_fBloomIntensity, TNULL, TNULL, 0.0f, 4.0f, 0.05f );
	( pVar++ )->CreateIntRefRange( S::BLOOM_LEVELS, "Blur levels of the bloom, from half the screen size down: 1 (tight) to 5 (wide)", RANGE, &S::ms_iBloomLevels, TNULL, TNULL, 1, 5, 1 );
	( pVar++ )->CreateIntRefRange( S::DYNAMIC_LIGHTS, "Dynamic night lights light the world, the bike headlight", RANGE_BOOL, &S::ms_iDynamicLights, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateIntRefRange( S::DYNAMIC_LIGHT_SHADOWS, "Shadows of the dynamic lights (0=Off,1=512,2=1024)", RANGE_ENUM, &S::ms_iDynamicLightShadows, TNULL, TNULL, 0, 2, 1 );
	( pVar++ )->CreateIntRefRange( S::DYNAMIC_LIGHT_FLICKER, "The dynamic lights flicker", RANGE_BOOL, &S::ms_iDynamicLightFlicker, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateIntRefRange( S::SUN_SHADOWS, "Shadows of the sun (0=Off,1=3x1024,2=3x2048,3=3x4096)", RANGE_ENUM, &S::ms_iSunShadows, TNULL, TNULL, 0, 3, 1 );
	( pVar++ )->CreateFloatRefRange( S::SUN_SHADOW_DISTANCE, "How far the sun shadows reach, world units", RANGE, &S::ms_fSunShadowDistance, TNULL, TNULL, 10.0f, 200.0f, 1.0f );
	( pVar++ )->CreateFloatRefRange( S::SUN_SHADOW_INTENSITY, "Darkness of the sun shadows: the part of the sunlight they take away", RANGE, &S::ms_fSunShadowIntensity, TNULL, TNULL, 0.0f, 1.0f, 0.005f );
	( pVar++ )->CreateIntRefRange( S::SUN_DIR_OVERRIDE, "The sun shadows use ERRender's fixed sun direction instead of the game's", RANGE_BOOL, &S::ms_iSunDirOverride, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateIntRefRange( S::SHADOW_OFFSCREEN, "The characters and objects outside the view cast their shadows into it", RANGE_BOOL, &S::ms_iShadowOffscreen, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateIntRefRange( S::SUN_SHAFTS, "Sun shafts: rays from the sun through the sky", RANGE_BOOL, &S::ms_iSunShafts, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateFloatRefRange( S::SUN_SHAFTS_INTENSITY, "Strength of the sun shafts", RANGE, &S::ms_fSunShaftsIntensity, TNULL, TNULL, 0.0f, 0.5f, 0.005f );
	( pVar++ )->CreateIntRefRange( S::VOLUMETRIC_FOG, "Volumetric fog lit by the sun, shadowed by the sun shadows when they're on (0=Off,1=Additive,2=Darken)", RANGE_ENUM, &S::ms_iVolumetricFog, TNULL, TNULL, 0, 2, 1 );
	// On by default when the TTF is there (it isn't shipped), without it the setting changes nothing
	S::ms_iHDFonts = T2GUIFontSDF::IsTTFThere() ? 1 : 0;
	( pVar++ )->CreateIntRefRange( S::HD_FONTS, "HD fonts: the comic font (Rekord26) drawn sharp at any resolution from Data\\Fonts\\CCThatsAllFolks.ttf, the glyphs the TTF doesn't have and Rekord18 from their atlases (1 when the file is there)", RANGE_BOOL, &S::ms_iHDFonts, TNULL, TNULL, 0, 1, 1 );
	( pVar++ )->CreateIntRefRange( S::FX_TIMINGS, "Logs the GPU time of the scene and of each effect every 120 frames", RANGE_BOOL, &S::ms_iFXTimings, TNULL, TNULL, 0, 1, 1 );

	s_iNumSettingVars = TINT( pVar - s_aSettingVars );
	TASSERT( s_iNumSettingVars <= TARRAYSIZE( s_aSettingVars ) );
}

void AEffects::UnregisterSettings()
{
	for ( TINT i = 0; i < s_iNumSettingVars; i++ )
		s_aSettingVars[ i ].Remove();

	s_iNumSettingVars = 0;
}
#else
void AEffects::RegisterSettings() {}
void AEffects::UnregisterSettings() {}
#endif // PRIMEDEBUG

//-----------------------------------------------------------------------------
// State of the frame
//-----------------------------------------------------------------------------
static TBOOL s_bInFrame;
static TBOOL s_bSceneTarget;     // the scene renders into the scene target this frame
static UINT  s_uiSampleCount = 1; // of what the scene renders into
static TBOOL s_bPostThisFrame;   // a post effect runs in EndFrame

static UINT  s_iAskedMSAA = 0;   // GetSupportedSampleCount of it, for this back buffer
static UINT  s_uiSupportedMSAA = 1;
static void* s_pMSAABackBuffer;

static TINT  s_iLoggedMSAA = -1; // what the log says last
static UINT  s_uiLoggedSamples;

// Glow target of the bloom for this frame
static ID3D11RenderTargetView*         s_pGlowRTV;
static TBOOL                           s_bGlowDrawn; // this frame
static ID3D11RenderTargetView*         s_pGlowSavedRTV;
static TRenderD3DInterface::STATEBLOCK s_oGlowStateBlock;
static TBOOL                           s_bInGlowDraw;
static TUINT                           s_uiGlowDraws;   // since the last timing line
static TUINT                           s_uiGlowRefused; // BeginGlowDraw while something else was bound

// Readable copies of the scene for the post passes, made once a frame when asked for
static constexpr TUINT UNUSED_FRAMES_TO_RELEASE = 120;

static AEffectsTarget     s_oResolvedDepth; // the first sample of the MSAA depth, R32F
static ID3D11PixelShader* s_pResolveDepthPS;
static TBOOL              s_bResolveDepthFailed;
static TBOOL              s_bDepthResolved; // this frame
static TUINT              s_uiDepthUnused;  // frames without GetDepthSRV
static ID3D11Texture2D*          s_pDepthCopy; // the depth buffer before the overlay pass, without MSAA
static ID3D11ShaderResourceView* s_pDepthCopySRV;
static TBOOL                     s_bDepthCopied; // this frame
static TUINT                     s_uiDepthCopyUnused;
static AEffectsTarget     s_oSceneColour;   // the back buffer after the resolve
static TBOOL              s_bSceneColourCopied;
static TUINT              s_uiSceneColourUnused;

// Fullscreen passes
static TConstantBuffer<AEffectsPostCB> s_oPostCB;
static ID3D11SamplerState*             s_pPointClamp;
static ID3D11SamplerState*             s_pLinearClamp;
static TRenderD3DInterface::STATEBLOCK s_oPostStateBlock;
static TUINT32                         s_uiPostInputs; // bits of the bound slots
static TBOOL                           s_bInPost;

//-----------------------------------------------------------------------------
// GPU timings: a ring of frames of timestamp queries, read once the GPU is done with them
//-----------------------------------------------------------------------------
static constexpr TUINT NUM_TIMING_FRAMES = 6;
static constexpr TUINT TIMING_LOG_FRAMES = 120;

struct TimingFrame
{
	ID3D11Query* pDisjoint;
	ID3D11Query* apBegin[ AEFFECTS_TIMING_NUMOF ];
	ID3D11Query* apEnd[ AEFFECTS_TIMING_NUMOF ];
	TUINT32      uiUsed; // timings that began and ended
	UINT         uiSamples; // of the scene
	TBOOL        bPending;
};

static TimingFrame s_aTimingFrames[ NUM_TIMING_FRAMES ];
static TUINT       s_uiTimingFrame;
static TBOOL       s_bTimingThisFrame;
static TUINT32     s_uiTimingBegun;
static double      s_adTimingSums[ AEFFECTS_TIMING_NUMOF ];
static TUINT       s_auiTimingCounts[ AEFFECTS_TIMING_NUMOF ];
static TUINT       s_uiTimingFramesRead;
static TUINT       s_uiTimingFramesLost;

static const TCHAR* s_aTimingNames[ AEFFECTS_TIMING_NUMOF ] = {
	"frame",
	"shadows",
	"resolve",
	"ao",
	"bloom",
	"sunshafts",
	"fog",
	"scene",
};

static void DestroyTimingQueries()
{
	for ( TUINT i = 0; i < NUM_TIMING_FRAMES; i++ )
	{
		TimingFrame& rFrame = s_aTimingFrames[ i ];

		if ( rFrame.pDisjoint ) rFrame.pDisjoint->Release();

		for ( TINT k = 0; k < AEFFECTS_TIMING_NUMOF; k++ )
		{
			if ( rFrame.apBegin[ k ] ) rFrame.apBegin[ k ]->Release();
			if ( rFrame.apEnd[ k ] ) rFrame.apEnd[ k ]->Release();
		}

		rFrame = {};
	}
}

static TBOOL CreateTimingQueries()
{
	if ( s_aTimingFrames[ 0 ].pDisjoint )
		return TTRUE;

	ID3D11Device* pDevice = TRenderD3DInterface::Interface()->GetDevice();

	D3D11_QUERY_DESC oDisjointDesc  = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
	D3D11_QUERY_DESC oTimestampDesc = { D3D11_QUERY_TIMESTAMP, 0 };

	for ( TUINT i = 0; i < NUM_TIMING_FRAMES; i++ )
	{
		TimingFrame& rFrame = s_aTimingFrames[ i ];

		TBOOL bOK = SUCCEEDED( pDevice->CreateQuery( &oDisjointDesc, &rFrame.pDisjoint ) );

		for ( TINT k = 0; bOK && k < AEFFECTS_TIMING_NUMOF; k++ )
		{
			bOK = SUCCEEDED( pDevice->CreateQuery( &oTimestampDesc, &rFrame.apBegin[ k ] ) ) &&
			    SUCCEEDED( pDevice->CreateQuery( &oTimestampDesc, &rFrame.apEnd[ k ] ) );
		}

		if ( !bOK )
		{
			TERROR( "[fx] couldn't create the timestamp queries\n" );
			DestroyTimingQueries();
			return TFALSE;
		}
	}

	return TTRUE;
}

// Adds the times of a frame the GPU is done with, TFALSE when it isn't yet
static void ReadTimingFrame( TimingFrame& a_rFrame )
{
	ID3D11DeviceContext* pContext = TRenderD3DInterface::Interface()->GetDeviceContext();
	a_rFrame.bPending             = TFALSE;

	D3D11_QUERY_DATA_TIMESTAMP_DISJOINT oDisjoint;

	if ( pContext->GetData( a_rFrame.pDisjoint, &oDisjoint, sizeof( oDisjoint ), D3D11_ASYNC_GETDATA_DONOTFLUSH ) != S_OK || oDisjoint.Disjoint || oDisjoint.Frequency == 0 )
	{
		s_uiTimingFramesLost++;
		return;
	}

	double adTimes[ AEFFECTS_TIMING_NUMOF ];

	for ( TINT k = 0; k < AEFFECTS_TIMING_NUMOF; k++ )
	{
		if ( !( a_rFrame.uiUsed & ( 1u << k ) ) )
			continue;

		UINT64 uiBegin = 0;
		UINT64 uiEnd   = 0;

		if ( pContext->GetData( a_rFrame.apBegin[ k ], &uiBegin, sizeof( uiBegin ), D3D11_ASYNC_GETDATA_DONOTFLUSH ) != S_OK ||
		     pContext->GetData( a_rFrame.apEnd[ k ], &uiEnd, sizeof( uiEnd ), D3D11_ASYNC_GETDATA_DONOTFLUSH ) != S_OK )
		{
			s_uiTimingFramesLost++;
			return;
		}

		adTimes[ k ] = double( uiEnd - uiBegin ) * 1000.0 / double( oDisjoint.Frequency );
	}

	for ( TINT k = 0; k < AEFFECTS_TIMING_NUMOF; k++ )
	{
		if ( a_rFrame.uiUsed & ( 1u << k ) )
		{
			s_adTimingSums[ k ] += adTimes[ k ];
			s_auiTimingCounts[ k ]++;
		}
	}

	s_uiTimingFramesRead++;

	if ( s_uiTimingFramesRead >= TIMING_LOG_FRAMES )
	{
		auto pRender = TRenderD3DInterface::Interface();

		TCHAR szLine[ 512 ];
		TINT  iLength = snprintf( szLine, sizeof( szLine ), "[fx] GPU ms, average of %u frames (%u lost), %ux%u, %u sample(s):", s_uiTimingFramesRead, s_uiTimingFramesLost, pRender->GetBackBufferWidth(), pRender->GetBackBufferHeight(), a_rFrame.uiSamples );

		for ( TINT k = 0; k < AEFFECTS_TIMING_NUMOF; k++ )
		{
			if ( s_auiTimingCounts[ k ] != 0 && iLength > 0 && iLength < TINT( sizeof( szLine ) ) )
				iLength += snprintf( szLine + iLength, sizeof( szLine ) - iLength, " %s %.3f", s_aTimingNames[ k ], s_adTimingSums[ k ] / s_auiTimingCounts[ k ] );
		}

		TINFO( "%s | glow draws %u (refused %u)\n", szLine, s_uiGlowDraws, s_uiGlowRefused );
		s_uiGlowDraws   = 0;
		s_uiGlowRefused = 0;

		TUtil::MemClear( s_adTimingSums, sizeof( s_adTimingSums ) );
		TUtil::MemClear( s_auiTimingCounts, sizeof( s_auiTimingCounts ) );
		s_uiTimingFramesRead = 0;
		s_uiTimingFramesLost = 0;
	}
}

static void BeginTimingFrame()
{
	s_bTimingThisFrame = AEffectsSettings::ms_iFXTimings != 0 && CreateTimingQueries();
	s_uiTimingBegun    = 0;

	if ( !s_bTimingThisFrame )
		return;

	TimingFrame& rFrame = s_aTimingFrames[ s_uiTimingFrame % NUM_TIMING_FRAMES ];

	// The oldest frame of the ring: three to five frames old, the GPU is normally done with it
	if ( rFrame.bPending )
		ReadTimingFrame( rFrame );

	rFrame.uiUsed = 0;
	TRenderD3DInterface::Interface()->GetDeviceContext()->Begin( rFrame.pDisjoint );
}

static void EndTimingFrame()
{
	if ( !s_bTimingThisFrame )
		return;

	TimingFrame& rFrame = s_aTimingFrames[ s_uiTimingFrame % NUM_TIMING_FRAMES ];
	TRenderD3DInterface::Interface()->GetDeviceContext()->End( rFrame.pDisjoint );
	rFrame.uiSamples = s_uiSampleCount;
	rFrame.bPending  = TTRUE;

	s_uiTimingFrame++;
	s_bTimingThisFrame = TFALSE;
}

void AEffects::BeginTiming( AEffectsTiming a_eTiming )
{
	if ( !s_bTimingThisFrame || ( s_uiTimingBegun & ( 1u << a_eTiming ) ) )
		return;

	s_uiTimingBegun |= 1u << a_eTiming;
	TRenderD3DInterface::Interface()->GetDeviceContext()->End( s_aTimingFrames[ s_uiTimingFrame % NUM_TIMING_FRAMES ].apBegin[ a_eTiming ] );
}

void AEffects::EndTiming( AEffectsTiming a_eTiming )
{
	TimingFrame& rFrame = s_aTimingFrames[ s_uiTimingFrame % NUM_TIMING_FRAMES ];

	if ( !s_bTimingThisFrame || !( s_uiTimingBegun & ( 1u << a_eTiming ) ) || ( rFrame.uiUsed & ( 1u << a_eTiming ) ) )
		return;

	rFrame.uiUsed |= 1u << a_eTiming;
	TRenderD3DInterface::Interface()->GetDeviceContext()->End( rFrame.apEnd[ a_eTiming ] );
}

//-----------------------------------------------------------------------------
// Targets
//-----------------------------------------------------------------------------
static UINT GetBytesPerPixel( DXGI_FORMAT a_eFormat )
{
	switch ( a_eFormat )
	{
		case DXGI_FORMAT_R8_UNORM:
			return 1;
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R8G8_UNORM:
			return 2;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return 8;
		default:
			return 4;
	}
}

TFLOAT AEffectsTarget::GetMegabytes() const
{
	return pTexture ? TFLOAT( double( uiWidth ) * uiHeight * uiSamples * GetBytesPerPixel( eFormat ) / 1000000.0 ) : 0.0f;
}

TBOOL AEffects::CreateTarget( AEffectsTarget& a_rTarget, const TCHAR* a_szName, UINT a_uiWidth, UINT a_uiHeight, DXGI_FORMAT a_eFormat, UINT a_uiSamples )
{
	DestroyTarget( a_rTarget );

	ID3D11Device* pDevice = TRenderD3DInterface::Interface()->GetDevice();

	D3D11_TEXTURE2D_DESC oDesc = {};
	oDesc.Width                = TMath::Max( a_uiWidth, 1u );
	oDesc.Height               = TMath::Max( a_uiHeight, 1u );
	oDesc.MipLevels            = 1;
	oDesc.ArraySize            = 1;
	oDesc.Format               = a_eFormat;
	oDesc.SampleDesc.Count     = TMath::Max( a_uiSamples, 1u );
	oDesc.SampleDesc.Quality   = 0;
	oDesc.Usage                = D3D11_USAGE_DEFAULT;
	oDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if ( FAILED( pDevice->CreateTexture2D( &oDesc, TNULL, &a_rTarget.pTexture ) ) ||
	     FAILED( pDevice->CreateRenderTargetView( a_rTarget.pTexture, TNULL, &a_rTarget.pRTV ) ) ||
	     FAILED( pDevice->CreateShaderResourceView( a_rTarget.pTexture, TNULL, &a_rTarget.pSRV ) ) )
	{
		TERROR( "[fx] couldn't create the %s target (%ux%u, format %d, %u sample(s))\n", a_szName, oDesc.Width, oDesc.Height, TINT( a_eFormat ), oDesc.SampleDesc.Count );
		DestroyTarget( a_rTarget );
		return TFALSE;
	}

	a_rTarget.uiWidth   = oDesc.Width;
	a_rTarget.uiHeight  = oDesc.Height;
	a_rTarget.uiSamples = oDesc.SampleDesc.Count;
	a_rTarget.eFormat   = a_eFormat;

	TINFO( "[fx] %s target %ux%u, %u sample(s): %.1f MB\n", a_szName, a_rTarget.uiWidth, a_rTarget.uiHeight, a_rTarget.uiSamples, a_rTarget.GetMegabytes() );
	return TTRUE;
}

void AEffects::DestroyTarget( AEffectsTarget& a_rTarget )
{
	if ( !a_rTarget.pTexture && !a_rTarget.pRTV && !a_rTarget.pSRV )
		return;

	auto pRender = TRenderD3DInterface::Interface();

	if ( a_rTarget.pSRV )
	{
		pRender->InvalidateShaderResource( a_rTarget.pSRV );
		a_rTarget.pSRV->Release();
	}

	if ( a_rTarget.pRTV )
	{
		if ( pRender->GetDesiredState().pRenderTargetView == a_rTarget.pRTV )
			pRender->SetRenderTargets( pRender->GetBackBufferRTView(), pRender->GetDepthStencilView() );

		a_rTarget.pRTV->Release();
	}

	if ( a_rTarget.pTexture )
		a_rTarget.pTexture->Release();

	a_rTarget = AEffectsTarget();

	// A new view can get the address of a released one that's still in the state cache
	pRender->InvalidateStateCache();
}

//-----------------------------------------------------------------------------
// Fullscreen passes
//-----------------------------------------------------------------------------
static const TCHAR s_szPostCommon[] = R"(
struct FULLSCREEN_PS_IN
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
};

cbuffer PostConstants : register( b3 )
{
	float4 g_avParams[ 32 ];
};

SamplerState g_PointClamp : register( s10 );
SamplerState g_LinearClamp : register( s11 );
)";

ID3D11PixelShader* AEffects::CompilePostShader( const TCHAR* a_szSource, LPCSTR a_szEntryPoint, const D3D_SHADER_MACRO* a_pDefines, LPCSTR a_szTarget )
{
	TString8 strSource = s_szPostCommon;
	strSource += a_szSource;

	ID3DBlob* pBlob = TRenderD3DInterface::CompileShader( strSource.GetString(), a_szEntryPoint, a_szTarget, a_pDefines );

	if ( !pBlob )
	{
		TERROR( "[fx] the pixel shader %s doesn't compile\n", a_szEntryPoint );
		return TNULL;
	}

	ID3D11PixelShader* pShader = TNULL;
	TRenderD3DInterface::CreatePixelShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), &pShader );
	pBlob->Release();

	return pShader;
}

static ID3D11SamplerState* CreateClampSampler( D3D11_FILTER a_eFilter )
{
	D3D11_SAMPLER_DESC oDesc = {};
	oDesc.Filter             = a_eFilter;
	oDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
	oDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
	oDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
	oDesc.MaxAnisotropy      = 1;
	oDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
	oDesc.MinLOD             = 0.0f;
	oDesc.MaxLOD             = D3D11_FLOAT32_MAX;

	ID3D11SamplerState* pSampler = TNULL;
	TRenderD3DInterface::Interface()->GetDevice()->CreateSamplerState( &oDesc, &pSampler );
	return pSampler;
}

static void SetViewport( UINT a_uiX, UINT a_uiY, UINT a_uiWidth, UINT a_uiHeight )
{
	D3D11_VIEWPORT oViewport = { TFLOAT( a_uiX ), TFLOAT( a_uiY ), TFLOAT( a_uiWidth ), TFLOAT( a_uiHeight ), 0.0f, 1.0f };
	TRenderD3DInterface::Interface()->GetDeviceContext()->RSSetViewports( 1, &oViewport );
}

// The viewport of a render context, like TViewport::BeginSKU sets it
static void SetContextViewport( TRenderContext* a_pContext )
{
	const TRenderContext::VIEWPORTPARAMS& rcParams = a_pContext->GetViewportParameters();
	SetViewport( UINT( rcParams.fX ), UINT( rcParams.fY ), UINT( rcParams.fWidth ), UINT( rcParams.fHeight ) );
}

static TBOOL BeginPostPasses()
{
	auto pRender = TRenderD3DInterface::Interface();

	if ( !pRender->IsFullscreenTriangleAvailable() || ( !s_oPostCB.IsCreated() && !s_oPostCB.Create() ) )
		return TFALSE;

	if ( !s_pPointClamp ) s_pPointClamp = CreateClampSampler( D3D11_FILTER_MIN_MAG_MIP_POINT );
	if ( !s_pLinearClamp ) s_pLinearClamp = CreateClampSampler( D3D11_FILTER_MIN_MAG_MIP_LINEAR );

	if ( !s_pPointClamp || !s_pLinearClamp )
		return TFALSE;

	pRender->FlushStripBatch();
	pRender->CaptureStateBlock( s_oPostStateBlock );

	pRender->SetZEnable( TFALSE );
	pRender->SetZWriteEnable( TFALSE );
	pRender->SetCullMode( D3D11_CULL_NONE );
	pRender->SetScissorEnable( TFALSE );
	pRender->SetAlphaTestEnable( TFALSE );
	pRender->SetFogEnable( TFALSE );
	pRender->SetAlphaBlendEnable( TFALSE );
	pRender->SetColorUpdate( TTRUE );
	pRender->SetAlphaUpdate( TFALSE );

	pRender->PSSetSampler( AEffectsSlots::S_POINT_CLAMP, s_pPointClamp );
	pRender->PSSetSampler( AEffectsSlots::S_LINEAR_CLAMP, s_pLinearClamp );
	pRender->SetPSConstantBuffer( AEffectsSlots::CB_POST, &s_oPostCB );

	s_uiPostInputs = 0;
	s_bInPost      = TTRUE;
	return TTRUE;
}

static void ClearPostInputs()
{
	auto pRender = TRenderD3DInterface::Interface();

	for ( UINT i = 0; s_uiPostInputs != 0; i++ )
	{
		if ( s_uiPostInputs & ( 1u << i ) )
		{
			pRender->PSSetShaderResource( i, TNULL );
			s_uiPostInputs &= ~( 1u << i );
		}
	}
}

static void EndPostPasses( TRenderContext* a_pMainContext )
{
	if ( !s_bInPost )
		return;

	auto pRender = TRenderD3DInterface::Interface();

	ClearPostInputs();
	pRender->SetPSConstantBuffer( AEffectsSlots::CB_POST, TNULL );
	pRender->ApplyStateBlock( s_oPostStateBlock );

	// What the scene renders into: the back buffer at the end of the frame, the scene target before the overlay pass
	pRender->SetRenderTargets( pRender->GetRenderTargetView(), pRender->GetDepthStencilView() );

	// The views of the passes are unbound before anything binds the depth buffer again
	pRender->ApplyDesiredState();
	SetContextViewport( a_pMainContext );

	s_bInPost = TFALSE;
}

void AEffects::BindPostTarget( ID3D11RenderTargetView* a_pRTV, UINT a_uiWidth, UINT a_uiHeight )
{
	TRenderD3DInterface::Interface()->SetRenderTargets( a_pRTV, TNULL );
	SetViewport( 0, 0, a_uiWidth, a_uiHeight );
}

void AEffects::SetPostBlend( POSTBLEND a_eBlend )
{
	auto pRender = TRenderD3DInterface::Interface();

	switch ( a_eBlend )
	{
		case POSTBLEND_ADD:
			pRender->SetAlphaBlendEnable( TTRUE );
			pRender->SetBlendOp( D3D11_BLEND_OP_ADD );
			pRender->SetSrcBlend( D3D11_BLEND_ONE );
			pRender->SetDestBlend( D3D11_BLEND_ONE );
			break;
		case POSTBLEND_MULTIPLY:
			pRender->SetAlphaBlendEnable( TTRUE );
			pRender->SetBlendOp( D3D11_BLEND_OP_ADD );
			pRender->SetSrcBlend( D3D11_BLEND_ZERO );
			pRender->SetDestBlend( D3D11_BLEND_SRC_COLOR );
			break;
		default:
			pRender->SetAlphaBlendEnable( TFALSE );
			break;
	}
}

void AEffects::SetPostInput( UINT a_uiSlot, ID3D11ShaderResourceView* a_pSRV )
{
	TASSERT( a_uiSlot >= AEffectsSlots::T_POST_0 && a_uiSlot <= AEffectsSlots::T_POST_3 );

	TRenderD3DInterface::Interface()->PSSetShaderResource( a_uiSlot, a_pSRV );

	if ( a_pSRV )
		s_uiPostInputs |= 1u << a_uiSlot;
}

void AEffects::SetPostConstants( const AEffectsPostCB& a_rcConstants )
{
	s_oPostCB.Set( a_rcConstants );
}

void AEffects::DrawPost( ID3D11PixelShader* a_pPixelShader )
{
	TRenderD3DInterface::Interface()->DrawFullscreenTriangle( a_pPixelShader );

	// Inputs are unbound after every pass, the next one may render into them
	ClearPostInputs();
}

UINT AEffects::GetWidth()
{
	return TRenderD3DInterface::Interface()->GetBackBufferWidth();
}

UINT AEffects::GetHeight()
{
	return TRenderD3DInterface::Interface()->GetBackBufferHeight();
}

ID3D11ShaderResourceView* AEffects::GetSceneDepthSRV()
{
	auto pRender = TRenderD3DInterface::Interface();
	return s_bSceneTarget ? pRender->GetSceneDepthTarget().pSRV : pRender->GetDepthStencilSRV();
}

// The first sample of the MSAA depth buffer (ResolveDepth of ERRender), ps_4_1: MSAA needs feature level 10_1 anyway
static const TCHAR s_szResolveDepthShader[] = R"(
Texture2DMS<float> g_DepthMS : register( t10 );

float ps_main( FULLSCREEN_PS_IN In ) : SV_TARGET
{
	return g_DepthMS.Load( int2( In.vPosition.xy ), 0 );
}
)";

ID3D11ShaderResourceView* AEffects::GetDepthSRV()
{
	if ( !s_bSceneTarget )
		return s_bDepthCopied ? s_pDepthCopySRV : GetSceneDepthSRV();

	if ( s_bDepthResolved )
		return s_oResolvedDepth.pSRV;

	if ( !s_bInPost || s_bResolveDepthFailed )
		return TNULL;

	if ( !s_pResolveDepthPS )
	{
		s_pResolveDepthPS = CompilePostShader( s_szResolveDepthShader, "ps_main", TNULL, "ps_4_1" );

		if ( !s_pResolveDepthPS )
		{
			TERROR( "[fx] the MSAA depth resolve shader doesn't compile, the effects reading the depth stay off with MSAA\n" );
			s_bResolveDepthFailed = TTRUE;
			return TNULL;
		}
	}

	const UINT uiWidth  = GetWidth();
	const UINT uiHeight = GetHeight();

	if ( !s_oResolvedDepth.Matches( uiWidth, uiHeight, DXGI_FORMAT_R32_FLOAT, 1 ) && !CreateTarget( s_oResolvedDepth, "resolved depth", uiWidth, uiHeight, DXGI_FORMAT_R32_FLOAT ) )
		return TNULL;

	// The pass's own blending is put back, the callers set theirs before they draw anyway
	BindPostTarget( s_oResolvedDepth.pRTV, uiWidth, uiHeight );
	SetPostBlend( POSTBLEND_NONE );
	SetPostInput( AEffectsSlots::T_POST_0, GetSceneDepthSRV() );
	DrawPost( s_pResolveDepthPS );

	s_bDepthResolved = TTRUE;
	s_uiDepthUnused  = 0;
	return s_oResolvedDepth.pSRV;
}

ID3D11ShaderResourceView* AEffects::GetSceneColourSRV()
{
	return s_bSceneColourCopied ? s_oSceneColour.pSRV : TNULL;
}

// The back buffer as it is now, into s_oSceneColour
static void CopySceneColour()
{
	auto pRender = TRenderD3DInterface::Interface();

	D3D11_TEXTURE2D_DESC oDesc;
	pRender->GetBackBuffer()->GetDesc( &oDesc );

	if ( !s_oSceneColour.Matches( oDesc.Width, oDesc.Height, oDesc.Format, 1 ) && !AEffects::CreateTarget( s_oSceneColour, "scene colour copy", oDesc.Width, oDesc.Height, oDesc.Format ) )
		return;

	pRender->GetDeviceContext()->CopyResource( s_oSceneColour.pTexture, pRender->GetBackBuffer() );
	s_bSceneColourCopied  = TTRUE;
	s_uiSceneColourUnused = 0;
}

static void DestroyDepthCopy()
{
	if ( s_pDepthCopySRV )
	{
		TRenderD3DInterface::Interface()->InvalidateShaderResource( s_pDepthCopySRV );
		s_pDepthCopySRV->Release();
		s_pDepthCopySRV = TNULL;
	}

	if ( s_pDepthCopy )
	{
		s_pDepthCopy->Release();
		s_pDepthCopy = TNULL;
	}
}

// The depth buffer (without MSAA) into s_pDepthCopy: a texture like it that only shaders read, with its view
static void CopyDepth()
{
	auto                      pRender   = TRenderD3DInterface::Interface();
	ID3D11ShaderResourceView* pDepthSRV = pRender->GetDepthStencilSRV();

	if ( !pDepthSRV )
		return;

	ID3D11Resource* pResource = TNULL;
	pDepthSRV->GetResource( &pResource );

	D3D11_TEXTURE2D_DESC oDesc;
	TSTATICCAST( ID3D11Texture2D, pResource )->GetDesc( &oDesc );

	D3D11_TEXTURE2D_DESC oCopyDesc = {};

	if ( s_pDepthCopy )
		s_pDepthCopy->GetDesc( &oCopyDesc );

	if ( !s_pDepthCopy || oCopyDesc.Width != oDesc.Width || oCopyDesc.Height != oDesc.Height || oCopyDesc.Format != oDesc.Format )
	{
		DestroyDepthCopy();

		D3D11_SHADER_RESOURCE_VIEW_DESC oViewDesc;
		pDepthSRV->GetDesc( &oViewDesc );

		oDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
		oDesc.CPUAccessFlags = 0;
		oDesc.MiscFlags      = 0;

		if ( FAILED( pRender->GetDevice()->CreateTexture2D( &oDesc, TNULL, &s_pDepthCopy ) ) ||
		     FAILED( pRender->GetDevice()->CreateShaderResourceView( s_pDepthCopy, &oViewDesc, &s_pDepthCopySRV ) ) )
		{
			TERROR( "[fx] couldn't create the depth copy\n" );
			DestroyDepthCopy();
			pResource->Release();
			return;
		}

		TINFO( "[fx] depth copy %ux%u: %.1f MB\n", oDesc.Width, oDesc.Height, oDesc.Width * oDesc.Height * 4 / 1000000.0f );
	}

	pRender->GetDeviceContext()->CopyResource( s_pDepthCopy, pResource );
	pResource->Release();

	s_bDepthCopied      = TTRUE;
	s_uiDepthCopyUnused = 0;
}

// The copies nobody asked for in a while go
static void ReleaseUnusedCopies()
{
	if ( s_pDepthCopy && !s_bDepthCopied && ++s_uiDepthCopyUnused >= UNUSED_FRAMES_TO_RELEASE )
		DestroyDepthCopy();

	if ( s_oResolvedDepth.IsCreated() && !s_bDepthResolved && ++s_uiDepthUnused >= UNUSED_FRAMES_TO_RELEASE )
		AEffects::DestroyTarget( s_oResolvedDepth );

	if ( s_oSceneColour.IsCreated() && !s_bSceneColourCopied && ++s_uiSceneColourUnused >= UNUSED_FRAMES_TO_RELEASE )
		AEffects::DestroyTarget( s_oSceneColour );
}

//-----------------------------------------------------------------------------
// Glow
//-----------------------------------------------------------------------------
TBOOL AEffects::IsGlowTargetActive()
{
	return s_pGlowRTV != TNULL;
}

TBOOL AEffects::BeginGlowDraw()
{
	if ( !s_pGlowRTV || s_bInGlowDraw )
		return TFALSE;

	auto        pRender = TRenderD3DInterface::Interface();
	const auto& rcState = pRender->GetDesiredState();

	// Only over the scene, not while something renders into a texture
	if ( rcState.pRenderTargetView != pRender->GetRenderTargetView() || rcState.pDepthStencilView != pRender->GetDepthStencilView() )
	{
		s_uiGlowRefused++;
		return TFALSE;
	}

	s_uiGlowDraws++;

	// A strip batch still open belongs to the scene
	pRender->FlushStripBatch();

	s_pGlowSavedRTV = rcState.pRenderTargetView;
	pRender->CaptureStateBlock( s_oGlowStateBlock );
	pRender->SetRenderTargets( s_pGlowRTV, rcState.pDepthStencilView );

	// The first draw wrote the depth already
	pRender->SetZWriteEnable( TFALSE );

	if ( s_oGlowStateBlock.oDepth.Parts.DepthFunc == D3D11_COMPARISON_LESS )
		pRender->SetZFunc( D3D11_COMPARISON_LESS_EQUAL );

	s_bInGlowDraw = TTRUE;
	s_bGlowDrawn  = TTRUE;
	return TTRUE;
}

void AEffects::EndGlowDraw()
{
	if ( !s_bInGlowDraw )
		return;

	auto pRender = TRenderD3DInterface::Interface();
	pRender->SetRenderTargets( s_pGlowSavedRTV, pRender->GetDesiredState().pDepthStencilView );
	pRender->ApplyStateBlock( s_oGlowStateBlock );

	s_bInGlowDraw = TFALSE;
}

//-----------------------------------------------------------------------------
// The frame
//-----------------------------------------------------------------------------
TBOOL AEffects::IsInFrame()
{
	return s_bInFrame;
}

UINT AEffects::GetSceneSampleCount()
{
	return s_uiSampleCount;
}

// The post effects that run this frame
static TBOOL IsAnyPostEffectOn()
{
	return AAmbientOcclusion::IsOn() || ABloom::IsOn() || ASunShafts::IsOn() || AVolumetricFog::IsOn();
}

void AEffects::BeginFrame( TRenderContext* a_pMainContext )
{
	auto pRender = TRenderD3DInterface::Interface();

	TASSERT( !s_bInFrame );
	const TBOOL bGlowDrawnLastFrame = s_bGlowDrawn;

	s_bInFrame           = TTRUE;
	s_bSceneTarget       = TFALSE;
	s_uiSampleCount      = 1;
	s_pGlowRTV           = TNULL;
	s_bGlowDrawn         = TFALSE;
	s_bDepthResolved     = TFALSE;
	s_bDepthCopied       = TFALSE;
	s_bSceneColourCopied = TFALSE;
	s_bPostThisFrame = IsAnyPostEffectOn();

	pRender->SetMaxAnisotropy( UINT( AEffectsSettings::ms_iAnisotropy ) );

	BeginTimingFrame();
	BeginTiming( AEFFECTS_TIMING_FRAME );

	// The highest sample count up to the asked one, found again when the back buffer changes
	if ( UINT( AEffectsSettings::ms_iMSAA ) != s_iAskedMSAA || pRender->GetBackBuffer() != s_pMSAABackBuffer )
	{
		s_iAskedMSAA      = UINT( AEffectsSettings::ms_iMSAA );
		s_pMSAABackBuffer = pRender->GetBackBuffer();
		s_uiSupportedMSAA = ( s_iAskedMSAA > 1 ) ? pRender->GetSupportedSampleCount( s_iAskedMSAA ) : 1;
	}

	if ( s_uiSupportedMSAA > 1 )
	{
		pRender->SetSceneTargetFormat( DXGI_FORMAT_UNKNOWN, s_uiSupportedMSAA );
		s_bSceneTarget = pRender->BeginSceneTarget();

		if ( s_bSceneTarget )
			s_uiSampleCount = pRender->GetSceneTargetSampleCount();
	}
	else if ( pRender->GetSceneTargetSampleCount() > 1 )
	{
		// MSAA went off: the MSAA scene target goes
		pRender->SetSceneTargetFormat( DXGI_FORMAT_UNKNOWN, 1 );
	}

	// The glow target has the scene's sample count, it shares its depth buffer
	s_pGlowRTV = ABloom::BeginFrame( s_uiSampleCount, bGlowDrawnLastFrame );

	if ( s_iLoggedMSAA != AEffectsSettings::ms_iMSAA || s_uiLoggedSamples != s_uiSampleCount )
	{
		s_iLoggedMSAA     = AEffectsSettings::ms_iMSAA;
		s_uiLoggedSamples = s_uiSampleCount;

		const TUINT uiWidth  = pRender->GetBackBufferWidth();
		const TUINT uiHeight = pRender->GetBackBufferHeight();
		const TFLOAT fMB     = s_bSceneTarget ? TFLOAT( double( uiWidth ) * uiHeight * s_uiSampleCount * 8 / 1000000.0 ) : 0.0f;
		TINFO( "[fx] MSAA %d asked, %u sample(s) used (%ux%u, scene target %.1f MB), anisotropy %u\n", AEffectsSettings::ms_iMSAA, s_uiSampleCount, uiWidth, uiHeight, fMB, pRender->GetMaxAnisotropy() );
	}
}

void AEffects::RenderShadowPass( TViewport* a_pViewport, TCameraObject* a_pCameraObject )
{
	if ( !s_bInFrame )
		return;

	auto            pRender      = TRenderD3DInterface::Interface();
	TRenderContext* pMainContext = a_pViewport->GetRenderContext();

	BeginTiming( AEFFECTS_TIMING_SHADOWS );
	AShadowMaps::Render( a_pViewport, a_pCameraObject );
	EndTiming( AEFFECTS_TIMING_SHADOWS );

	// The main pass continues where the viewport left it
	AEffectsShadowPass::ms_bActive = TFALSE;
	pRender->SetCurrentRenderContext( pMainContext );
	pRender->SetRenderTargets( pRender->GetRenderTargetView(), pRender->GetDepthStencilView() );
	SetContextViewport( pMainContext );

	BeginTiming( AEFFECTS_TIMING_SCENE );
}

void AEffects::CaptureDepth( TRenderContext* a_pMainContext )
{
	// Only for the effects that read the depth
	if ( !s_bInFrame || !s_bPostThisFrame || !( AAmbientOcclusion::IsOn() || ASunShafts::IsOn() || AVolumetricFog::IsOn() ) )
		return;

	auto pRender = TRenderD3DInterface::Interface();
	pRender->FlushStripBatch();

	if ( !s_bSceneTarget )
	{
		CopyDepth();
	}
	else if ( BeginPostPasses() )
	{
		// The first sample, resolved now; the scene target is bound again after
		GetDepthSRV();
		EndPostPasses( a_pMainContext );
	}
}

void AEffects::EndFrame( TRenderContext* a_pMainContext )
{
	if ( !s_bInFrame )
		return;

	auto pRender = TRenderD3DInterface::Interface();
	pRender->FlushStripBatch();
	EndTiming( AEFFECTS_TIMING_SCENE );

	if ( s_bSceneTarget )
	{
		// One resolve: the scene into the back buffer, the post effects work on it
		BeginTiming( AEFFECTS_TIMING_RESOLVE );

		pRender->EndSceneTarget();
		pRender->SetRenderTargets( TNULL, TNULL );
		pRender->ApplyDesiredState();

		D3D11_TEXTURE2D_DESC oDesc;
		pRender->GetBackBuffer()->GetDesc( &oDesc );
		pRender->GetDeviceContext()->ResolveSubresource( pRender->GetBackBuffer(), 0, pRender->GetSceneTexture(), 0, oDesc.Format );

		EndTiming( AEFFECTS_TIMING_RESOLVE );
		pRender->SetRenderTargets( pRender->GetBackBufferRTView(), pRender->GetDepthStencilView() );
	}

	s_pGlowRTV = TNULL;

	if ( !s_bPostThisFrame )
	{
		// Turned off: their memory goes (the bloom's goes in BeginFrame)
		AAmbientOcclusion::Render( a_pMainContext );
		AVolumetricFog::Render( a_pMainContext );
		ASunShafts::Render( a_pMainContext );
	}
	else if ( BeginPostPasses() )
	{
		// The sun shafts take the sky from the scene before the other effects change it
		if ( ASunShafts::Prepare( a_pMainContext ) )
			CopySceneColour();

		AAmbientOcclusion::Render( a_pMainContext );
		AVolumetricFog::Render( a_pMainContext );
		ABloom::Render( s_bGlowDrawn );
		ASunShafts::Render( a_pMainContext );
		EndPostPasses( a_pMainContext );
	}

	ReleaseUnusedCopies();

	s_pGlowRTV     = TNULL;
	s_bSceneTarget = TFALSE;
	s_bInFrame     = TFALSE;

	EndTiming( AEFFECTS_TIMING_FRAME );
	EndTimingFrame();
}

void AEffects::Destroy()
{
	AShadowMaps::Destroy();
	AAmbientOcclusion::Destroy();
	ABloom::Destroy();
	ASunShafts::Destroy();
	AVolumetricFog::Destroy();

	AEffects::DestroyTarget( s_oResolvedDepth );
	AEffects::DestroyTarget( s_oSceneColour );
	DestroyDepthCopy();

	if ( s_pResolveDepthPS )
	{
		s_pResolveDepthPS->Release();
		s_pResolveDepthPS = TNULL;
	}

	s_bResolveDepthFailed = TFALSE;

	if ( s_oPostCB.IsCreated() )
		s_oPostCB.Destroy();

	if ( s_pPointClamp )
	{
		s_pPointClamp->Release();
		s_pPointClamp = TNULL;
	}

	if ( s_pLinearClamp )
	{
		s_pLinearClamp->Release();
		s_pLinearClamp = TNULL;
	}

	DestroyTimingQueries();
}
