#include "pch.h"
#include "ABloom_DX11.h"

#include <Platform/DX11/TRenderInterface_DX11.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

//-----------------------------------------------------------------------------
// Shaders: DualKawaseDown, DualKawaseUp and GlowBloomComposite of ERRender (Resources/Shaders) on the
// fullscreen triangle of the renderer. g_avParams[ 0 ]: texel size of the source (xy), offset, intensity
//-----------------------------------------------------------------------------
static const TCHAR s_szKawaseShader[] = R"(
Texture2D g_Source : register( t10 );

float4 ps_down( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float2 o = g_avParams[ 0 ].xy * 0.5f * g_avParams[ 0 ].z;

	float4 color = g_Source.Sample( g_LinearClamp, i.vUV ) * 4.0f;
	color += g_Source.Sample( g_LinearClamp, i.vUV + float2( -o.x, -o.y ) );
	color += g_Source.Sample( g_LinearClamp, i.vUV + float2( o.x, -o.y ) );
	color += g_Source.Sample( g_LinearClamp, i.vUV + float2( -o.x, o.y ) );
	color += g_Source.Sample( g_LinearClamp, i.vUV + float2( o.x, o.y ) );

	return color / 8.0f;
}

float4 Up( float2 a_vUV )
{
	float2 o = g_avParams[ 0 ].xy * 0.5f * g_avParams[ 0 ].z;

	float4 color = 0.0f;
	color += g_Source.Sample( g_LinearClamp, a_vUV + float2( -o.x * 2.0f, 0.0f ) );
	color += g_Source.Sample( g_LinearClamp, a_vUV + float2( o.x * 2.0f, 0.0f ) );
	color += g_Source.Sample( g_LinearClamp, a_vUV + float2( 0.0f, -o.y * 2.0f ) );
	color += g_Source.Sample( g_LinearClamp, a_vUV + float2( 0.0f, o.y * 2.0f ) );
	color += g_Source.Sample( g_LinearClamp, a_vUV + float2( -o.x, o.y ) ) * 2.0f;
	color += g_Source.Sample( g_LinearClamp, a_vUV + float2( o.x, o.y ) ) * 2.0f;
	color += g_Source.Sample( g_LinearClamp, a_vUV + float2( -o.x, -o.y ) ) * 2.0f;
	color += g_Source.Sample( g_LinearClamp, a_vUV + float2( o.x, -o.y ) ) * 2.0f;

	return color / 12.0f;
}

float4 ps_up( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	return Up( i.vUV );
}

float4 ps_composite( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	return Up( i.vUV ) * g_avParams[ 0 ].w;
}
)";

// ERRender's defaults (ERRenderWrapper.cpp)
static constexpr TINT   MAX_LEVELS    = 5;
static constexpr TFLOAT KAWASE_OFFSET = 1.7f;

static ID3D11PixelShader* s_pDownPS;
static ID3D11PixelShader* s_pUpPS;
static ID3D11PixelShader* s_pCompositePS;
static TBOOL              s_bShadersFailed;

static AEffectsTarget s_oGlow;         // the scene's sample count
static AEffectsTarget s_oGlowResolved; // with MSAA
static AEffectsTarget s_aLevels[ MAX_LEVELS ];
static TBOOL          s_bGlowNew; // not cleared yet

static void ReleaseShader( ID3D11PixelShader*& a_rpShader )
{
	if ( a_rpShader )
	{
		a_rpShader->Release();
		a_rpShader = TNULL;
	}
}

static void ReleaseShaders()
{
	ReleaseShader( s_pDownPS );
	ReleaseShader( s_pUpPS );
	ReleaseShader( s_pCompositePS );
}

static void DestroyTargets()
{
	AEffects::DestroyTarget( s_oGlow );
	AEffects::DestroyTarget( s_oGlowResolved );

	for ( TINT i = 0; i < MAX_LEVELS; i++ )
		AEffects::DestroyTarget( s_aLevels[ i ] );
}

static TBOOL CreateShaders()
{
	if ( s_pCompositePS )
		return TTRUE;

	if ( s_bShadersFailed )
		return TFALSE;

	s_pDownPS      = AEffects::CompilePostShader( s_szKawaseShader, "ps_down" );
	s_pUpPS        = AEffects::CompilePostShader( s_szKawaseShader, "ps_up" );
	s_pCompositePS = AEffects::CompilePostShader( s_szKawaseShader, "ps_composite" );

	if ( !s_pDownPS || !s_pUpPS || !s_pCompositePS )
	{
		TERROR( "[fx] the bloom shaders don't compile, it stays off\n" );
		ReleaseShaders();
		s_bShadersFailed = TTRUE;
		return TFALSE;
	}

	return TTRUE;
}

TBOOL ABloom::IsOn()
{
	return AEffectsSettings::ms_iBloom != 0 && !s_bShadersFailed;
}

ID3D11RenderTargetView* ABloom::BeginFrame( UINT a_uiSamples, TBOOL a_bGlowDrawnLastFrame )
{
	if ( !IsOn() )
	{
		// Turned off: the memory goes
		if ( s_oGlow.IsCreated() )
			DestroyTargets();

		return TNULL;
	}

	if ( !CreateShaders() )
		return TNULL;

	const UINT uiWidth  = AEffects::GetWidth();
	const UINT uiHeight = AEffects::GetHeight();

	if ( !s_oGlow.Matches( uiWidth, uiHeight, DXGI_FORMAT_R8G8B8A8_UNORM, a_uiSamples ) )
	{
		if ( !AEffects::CreateTarget( s_oGlow, "glow", uiWidth, uiHeight, DXGI_FORMAT_R8G8B8A8_UNORM, a_uiSamples ) )
			return TNULL;

		s_bGlowNew = TTRUE;
	}

	// Nothing to clear while nothing glows
	if ( s_bGlowNew || a_bGlowDrawnLastFrame )
	{
		static constexpr FLOAT s_afBlack[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
		TRenderD3DInterface::Interface()->GetDeviceContext()->ClearRenderTargetView( s_oGlow.pRTV, s_afBlack );
		s_bGlowNew = TFALSE;
	}

	return s_oGlow.pRTV;
}

void ABloom::Render( TBOOL a_bGlowDrawn )
{
	if ( !a_bGlowDrawn || !s_oGlow.IsCreated() || !CreateShaders() )
		return;

	auto       pRender  = TRenderD3DInterface::Interface();
	const UINT uiWidth  = s_oGlow.uiWidth;
	const UINT uiHeight = s_oGlow.uiHeight;
	TINT       iLevels  = AEffectsSettings::ms_iBloomLevels;

	if ( iLevels < 1 ) iLevels = 1;
	if ( iLevels > MAX_LEVELS ) iLevels = MAX_LEVELS;

	// Levels at half the size, then halving. ERRender started at a quarter, its 2 levels are the size of 3 here
	for ( TINT i = 0; i < iLevels; i++ )
	{
		const UINT uiLevelW = TMath::Max( uiWidth >> ( i + 1 ), 1u );
		const UINT uiLevelH = TMath::Max( uiHeight >> ( i + 1 ), 1u );

		if ( !s_aLevels[ i ].Matches( uiLevelW, uiLevelH, DXGI_FORMAT_R11G11B10_FLOAT, 1 ) && !AEffects::CreateTarget( s_aLevels[ i ], "bloom level", uiLevelW, uiLevelH, DXGI_FORMAT_R11G11B10_FLOAT ) )
			return;
	}

	AEffects::BeginTiming( AEFFECTS_TIMING_BLOOM );

	ID3D11ShaderResourceView* pGlowSRV = s_oGlow.pSRV;

	if ( s_oGlow.uiSamples > 1 )
	{
		if ( !s_oGlowResolved.Matches( uiWidth, uiHeight, DXGI_FORMAT_R8G8B8A8_UNORM, 1 ) && !AEffects::CreateTarget( s_oGlowResolved, "glow resolve", uiWidth, uiHeight, DXGI_FORMAT_R8G8B8A8_UNORM ) )
		{
			AEffects::EndTiming( AEFFECTS_TIMING_BLOOM );
			return;
		}

		pRender->GetDeviceContext()->ResolveSubresource( s_oGlowResolved.pTexture, 0, s_oGlow.pTexture, 0, DXGI_FORMAT_R8G8B8A8_UNORM );
		pGlowSRV = s_oGlowResolved.pSRV;
	}
	else if ( s_oGlowResolved.IsCreated() )
	{
		AEffects::DestroyTarget( s_oGlowResolved );
	}

	auto fnPass = [ & ]( ID3D11RenderTargetView* a_pDst, UINT a_uiDstW, UINT a_uiDstH, ID3D11ShaderResourceView* a_pSrc, UINT a_uiSrcW, UINT a_uiSrcH, ID3D11PixelShader* a_pShader ) {
		AEffectsPostCB oConstants = {};
		oConstants.avParams[ 0 ]  = TVector4( 1.0f / a_uiSrcW, 1.0f / a_uiSrcH, KAWASE_OFFSET, AEffectsSettings::ms_fBloomIntensity );
		AEffects::SetPostConstants( oConstants );

		AEffects::BindPostTarget( a_pDst, a_uiDstW, a_uiDstH );
		AEffects::SetPostInput( AEffectsSlots::T_POST_0, a_pSrc );
		AEffects::DrawPost( a_pShader );
	};

	// Down the levels
	AEffects::SetPostBlend( AEffects::POSTBLEND_NONE );

	for ( TINT i = 0; i < iLevels; i++ )
	{
		const AEffectsTarget& rcDst = s_aLevels[ i ];

		if ( i == 0 )
			fnPass( rcDst.pRTV, rcDst.uiWidth, rcDst.uiHeight, pGlowSRV, uiWidth, uiHeight, s_pDownPS );
		else
			fnPass( rcDst.pRTV, rcDst.uiWidth, rcDst.uiHeight, s_aLevels[ i - 1 ].pSRV, s_aLevels[ i - 1 ].uiWidth, s_aLevels[ i - 1 ].uiHeight, s_pDownPS );
	}

	// Up again
	for ( TINT i = iLevels - 1; i > 0; i-- )
		fnPass( s_aLevels[ i - 1 ].pRTV, s_aLevels[ i - 1 ].uiWidth, s_aLevels[ i - 1 ].uiHeight, s_aLevels[ i ].pSRV, s_aLevels[ i ].uiWidth, s_aLevels[ i ].uiHeight, s_pUpPS );

	// Added onto the scene
	AEffects::SetPostBlend( AEffects::POSTBLEND_ADD );
	fnPass( pRender->GetBackBufferRTView(), AEffects::GetWidth(), AEffects::GetHeight(), s_aLevels[ 0 ].pSRV, s_aLevels[ 0 ].uiWidth, s_aLevels[ 0 ].uiHeight, s_pCompositePS );

	AEffects::EndTiming( AEFFECTS_TIMING_BLOOM );
}

TBOOL ABloom::GetKawaseShaders( ID3D11PixelShader*& a_rpDown, ID3D11PixelShader*& a_rpUp )
{
	if ( !CreateShaders() )
		return TFALSE;

	a_rpDown = s_pDownPS;
	a_rpUp   = s_pUpPS;
	return TTRUE;
}

void ABloom::Destroy()
{
	DestroyTargets();
	ReleaseShaders();
	s_bShadersFailed = TFALSE;
}
