#include "pch.h"
#include "AAmbientOcclusion_DX11.h"

#include <Platform/DX11/TRenderInterface_DX11.h>
#include <Platform/DX11/TRenderContext_DX11.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

//-----------------------------------------------------------------------------
// Shaders: HBAOPlus, XeGTAO, HBAOBlur and HBAOComposite of ERRender (Resources/Shaders), on the
// fullscreen triangle of the renderer with the registers of AEffectsSlots. Added: the fade with the fog.
// The depth comes from AEffects::GetDepthSRV (with MSAA its first sample, resolved by the core)
//-----------------------------------------------------------------------------

// g_avParams: [ 0 ] projection m11, m22, m31, m32; [ 1 ] m33, m43, near, far; [ 2 ] radius, bias (HBAO) or falloff
// range (GTAO), intensity, power; [ 3 ] width, height, 1 / width, 1 / height of the AO target;
// [ 4 ] GTAO radius multiplier, sample distribution power, thin occluder compensation; [ 5 ] fog on, start, end
static const TCHAR s_szAOCommon[] = R"(
Texture2D g_Depth : register( t10 );

#define cb_Projection  g_avParams[ 0 ]
#define cb_DepthParams g_avParams[ 1 ]
#define cb_Params      g_avParams[ 2 ]
#define cb_BufferSize  g_avParams[ 3 ]
#define cb_XeParams    g_avParams[ 4 ]
#define cb_Fog         g_avParams[ 5 ]

float LinearizeDepth( float hardwareDepth )
{
	float invNear = 1.0f / max( cb_DepthParams.z, 0.00001f );
	float invFar  = 1.0f / max( cb_DepthParams.w, cb_DepthParams.z + 0.00001f );
	return 1.0f / ( hardwareDepth * ( invFar - invNear ) + invNear );
}

float InterleavedGradientNoise( float2 pixel )
{
	return frac( 52.9829189f * frac( dot( pixel, float2( 0.06711056f, 0.00583715f ) ) ) );
}

// The linear fog of the game hides the occlusion in the distance
float FogFade( float a_fAO, float a_fViewDepth )
{
	if ( cb_Fog.x == 0.0f )
		return a_fAO;

	float fVisible = saturate( ( cb_Fog.z - a_fViewDepth ) / max( cb_Fog.z - cb_Fog.y, 0.0001f ) );
	return lerp( 1.0f, a_fAO, fVisible );
}
)";

static const TCHAR s_szHBAOShader[] = R"(
static const int NUM_DIRECTIONS = 8;
static const int NUM_STEPS      = 4;

static const float2 BASE_DIRS[ 8 ] = {
	float2( 1.00000f, 0.00000f ),
	float2( 0.70711f, 0.70711f ),
	float2( 0.00000f, 1.00000f ),
	float2( -0.70711f, 0.70711f ),
	float2( -1.00000f, 0.00000f ),
	float2( -0.70711f, -0.70711f ),
	float2( 0.00000f, -1.00000f ),
	float2( 0.70711f, -0.70711f ),
};

float3 UVToViewFromDepth( float2 uv, float hardwareDepth )
{
	float  viewDepth = LinearizeDepth( hardwareDepth );
	float2 ndc       = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );

	float focalX = max( abs( cb_Projection.x ), 0.0001f );
	float focalY = max( abs( cb_Projection.y ), 0.0001f );
	float viewX  = ( ndc.x - cb_Projection.z ) * viewDepth / focalX;
	float viewY  = ( ndc.y - cb_Projection.w ) * viewDepth / focalY;
	return float3( viewX, viewY, viewDepth );
}

float3 UVToView( float2 uv )
{
	return UVToViewFromDepth( uv, g_Depth.SampleLevel( g_PointClamp, uv, 0 ).r );
}

float3 ReconstructNormal( float2 uv, float3 center )
{
	float2 texel = cb_BufferSize.zw;
	float3 pr    = UVToView( uv + float2( texel.x, 0.0f ) );
	float3 pt    = UVToView( uv + float2( 0.0f, texel.y ) );
	return normalize( cross( pr - center, pt - center ) );
}

float ComputeSampleAO( float3 center, float3 normal, float3 samplePos, float radius, float bias )
{
	float3 v       = samplePos - center;
	float  distSq  = dot( v, v );
	float  invDist = rsqrt( max( distSq, 0.00001f ) );
	float  nDotV   = dot( normal, v ) * invDist;
	float  falloff = saturate( 1.0f - distSq / ( radius * radius ) );
	return saturate( nDotV - bias ) * falloff;
}

float ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float hardwareDepth = g_Depth.SampleLevel( g_PointClamp, i.vUV, 0 ).r;
	if ( hardwareDepth >= 0.99999f )
		return 1.0f;

	float3 center = UVToViewFromDepth( i.vUV, hardwareDepth );
	float3 normal = ReconstructNormal( i.vUV, center );

	float radius       = max( cb_Params.x, 0.0001f );
	float bias         = cb_Params.y;
	float focalY       = max( abs( cb_Projection.y ), 0.0001f );
	float radiusPixels = clamp( ( radius * focalY ) / max( center.z, 0.0001f ) * cb_BufferSize.y * 0.5f, 2.0f, 384.0f );
	float stepPixels   = radiusPixels / (float)( NUM_STEPS + 1 );
	float rotation     = InterleavedGradientNoise( i.vPosition.xy ) * 6.28318530718f;
	float stepJitter   = InterleavedGradientNoise( i.vPosition.xy + float2( 0.0f, 31.41f ) );

	float sinR, cosR;
	sincos( rotation, sinR, cosR );

	float smallScaleAO = 0.0f;
	float largeScaleAO = 0.0f;

	[unroll] for ( int dirIndex = 0; dirIndex < NUM_DIRECTIONS; dirIndex++ )
	{
		float2 base = BASE_DIRS[ dirIndex ];
		float2 dir  = float2( cosR * base.x - sinR * base.y, sinR * base.x + cosR * base.y );

		[unroll] for ( int stepIndex = 1; stepIndex <= NUM_STEPS; stepIndex++ )
		{
			float  samplePixels = 1.0f + ( (float)stepIndex - 0.5f + stepJitter ) * stepPixels;
			float2 sampleUV     = clamp( i.vUV + dir * samplePixels * cb_BufferSize.zw, 0.0f, 1.0f );
			float  sampleAO     = ComputeSampleAO( center, normal, UVToView( sampleUV ), radius, bias );

			if ( stepIndex == 1 )
				smallScaleAO += sampleAO;
			else
				largeScaleAO += sampleAO;
		}
	}

	float amountScale = cb_Params.z / max( 1.0f - bias, 0.0001f );
	float ao          = smallScaleAO * ( amountScale * 2.0f ) + largeScaleAO * amountScale;
	ao /= (float)( NUM_DIRECTIONS * ( NUM_STEPS + 1 ) );
	ao = saturate( 1.0f - ao );
	ao = pow( ao, cb_Params.w );

	return FogFade( ao, center.z );
}
)";

static const TCHAR s_szGTAOShader[] = R"(
static const int   XE_GTAO_SLICE_COUNT     = 3;
static const int   XE_GTAO_STEPS_PER_SLICE = 3;
static const float XE_GTAO_PIXEL_TOO_CLOSE = 1.3f;

float3 UVToView( float2 uv )
{
	float  hardwareDepth = g_Depth.SampleLevel( g_PointClamp, uv, 0 ).r;
	float  viewDepth     = LinearizeDepth( hardwareDepth );
	float  focalX        = max( abs( cb_Projection.x ), 0.0001f );
	float  focalY        = max( abs( cb_Projection.y ), 0.0001f );
	float2 ndcToViewMul  = float2( 2.0f / focalX, -2.0f / focalY );
	float2 ndcToViewAdd  = float2( -1.0f / focalX, 1.0f / focalY );
	float2 viewXY        = ( uv * ndcToViewMul + ndcToViewAdd ) * viewDepth;
	return float3( viewXY, viewDepth );
}

float3 MinDiff( float3 center, float3 posA, float3 posB )
{
	float3 a = posA - center;
	float3 b = center - posB;
	return dot( a, a ) < dot( b, b ) ? a : b;
}

float3 ReconstructNormal( float2 uv, float3 center )
{
	float2 texel = cb_BufferSize.zw;
	float3 pr    = UVToView( uv + float2( texel.x, 0.0f ) );
	float3 pl    = UVToView( uv - float2( texel.x, 0.0f ) );
	float3 pt    = UVToView( uv + float2( 0.0f, texel.y ) );
	float3 pb    = UVToView( uv - float2( 0.0f, texel.y ) );

	float3 normal = normalize( cross( MinDiff( center, pr, pl ), MinDiff( center, pt, pb ) ) );
	return dot( normal, -center ) < 0.0f ? -normal : normal;
}

float IntegrateSample( float3 center, float3 normal, float3 samplePos, float falloffMul, float falloffAdd )
{
	float3 sampleDelta = samplePos - center;
	float  sampleDist  = length( sampleDelta );
	if ( sampleDist <= 0.0001f )
		return 0.0f;

	float horizon = saturate( dot( normal, sampleDelta / sampleDist ) );
	float weight  = saturate( sampleDist * falloffMul + falloffAdd );
	return horizon * weight;
}

float ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float hardwareDepth = g_Depth.SampleLevel( g_PointClamp, i.vUV, 0 ).r;
	if ( hardwareDepth >= 0.99999f )
		return 1.0f;

	float3 center = UVToView( i.vUV );
	float3 normal = ReconstructNormal( i.vUV, center );

	float radius       = max( cb_Params.x * cb_XeParams.x, 0.0001f );
	float falloffRange = max( saturate( cb_Params.y ) * radius, 0.0001f );

	// weight = 1 within radius, fades to 0 at radius + falloffRange
	float falloffMul = -1.0f / falloffRange;
	float falloffAdd = radius / falloffRange + 1.0f;

	float sampleDistributionPower = max( cb_XeParams.y, 1.0f );

	float focalY       = max( abs( cb_Projection.y ), 0.0001f );
	float radiusPixels = clamp( ( radius * focalY ) / max( center.z, 0.0001f ) * cb_BufferSize.y * 0.5f, 2.0f, 384.0f );
	float minS         = XE_GTAO_PIXEL_TOO_CLOSE / radiusPixels;

	float noise = InterleavedGradientNoise( i.vPosition.xy );

	float visibility  = 0.0f;
	int   sampleCount = 0;

	[unroll] for ( int slice = 0; slice < XE_GTAO_SLICE_COUNT; slice++ )
	{
		float  sliceAngle = ( (float)slice + noise ) * ( 3.14159265359f / (float)XE_GTAO_SLICE_COUNT );
		float2 dir        = float2( cos( sliceAngle ), sin( sliceAngle ) );

		[unroll] for ( int stepIndex = 0; stepIndex < XE_GTAO_STEPS_PER_SLICE; stepIndex++ )
		{
			float s = ( (float)stepIndex + 0.5f ) / (float)XE_GTAO_STEPS_PER_SLICE;
			s       = pow( s + minS, sampleDistributionPower );

			float2 sampleOffset = round( dir * ( s * radiusPixels ) ) * cb_BufferSize.zw;
			float2 uv0          = i.vUV + sampleOffset;
			float2 uv1          = i.vUV - sampleOffset;

			if ( !any( uv0 < 0.0f ) && !any( uv0 > 1.0f ) )
			{
				visibility += IntegrateSample( center, normal, UVToView( uv0 ), falloffMul, falloffAdd );
				sampleCount++;
			}

			if ( !any( uv1 < 0.0f ) && !any( uv1 > 1.0f ) )
			{
				visibility += IntegrateSample( center, normal, UVToView( uv1 ), falloffMul, falloffAdd );
				sampleCount++;
			}
		}
	}

	float occlusion = sampleCount > 0 ? saturate( visibility / (float)sampleCount ) : 0.0f;
	float ao        = saturate( 1.0f - occlusion * cb_Params.z );
	ao              = pow( ao, cb_Params.w );

	return FogFade( ao, center.z );
}
)";

// g_avParams: [ 0 ] 1 / width, 1 / height, direction x, y; [ 1 ] near, far, sharpness
static const TCHAR s_szBlurShader[] = R"(
Texture2D g_AO : register( t10 );
Texture2D g_Depth : register( t11 );

float LinearizeDepth( float hardwareDepth )
{
	float invNear = 1.0f / max( g_avParams[ 1 ].x, 0.00001f );
	float invFar  = 1.0f / max( g_avParams[ 1 ].y, g_avParams[ 1 ].x + 0.00001f );
	return 1.0f / ( hardwareDepth * ( invFar - invNear ) + invNear );
}

float ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float  centerDepth = LinearizeDepth( g_Depth.SampleLevel( g_PointClamp, i.vUV, 0 ).r );
	float2 delta       = g_avParams[ 0 ].xy * g_avParams[ 0 ].zw;

	float totalAO     = g_AO.SampleLevel( g_PointClamp, i.vUV, 0 ).r;
	float totalWeight = 1.0f;

	[unroll] for ( int r = 1; r <= 4; r++ )
	{
		float kernel = exp2( -(float)( r * r ) / 8.0f );

		[unroll] for ( int side = -1; side <= 1; side += 2 )
		{
			float2 sampleUV    = i.vUV + delta * (float)( r * side );
			float  sampleDepth = LinearizeDepth( g_Depth.SampleLevel( g_PointClamp, sampleUV, 0 ).r );
			float  depthWeight = exp2( -abs( sampleDepth - centerDepth ) * g_avParams[ 1 ].z );
			float  weight      = kernel * depthWeight;

			totalAO += g_AO.SampleLevel( g_LinearClamp, sampleUV, 0 ).r * weight;
			totalWeight += weight;
		}
	}

	return totalAO / totalWeight;
}
)";

// Multiplied onto the scene (or shown alone)
static const TCHAR s_szCompositeShader[] = R"(
Texture2D g_AO : register( t10 );

float4 ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float fAO = g_AO.SampleLevel( g_LinearClamp, i.vUV, 0 ).r;
	return float4( fAO, fAO, fAO, 1.0f );
}
)";

// ERRender's defaults (ERRenderWrapper.cpp)
static constexpr TFLOAT HBAO_BIAS                    = 0.10f;
static constexpr TFLOAT AO_POWER                     = 1.0f;
static constexpr TFLOAT AO_BLUR_SHARPNESS            = 4.0f;
static constexpr TFLOAT GTAO_RADIUS_MULTIPLIER       = 1.457f;
static constexpr TFLOAT GTAO_FALLOFF_RANGE           = 0.9f;
static constexpr TFLOAT GTAO_DISTRIBUTION_POWER      = 2.8f;
static constexpr TFLOAT GTAO_THIN_OCCLUDER_COMPENSATE = 0.5f;

static ID3D11PixelShader* s_pHBAOPS;
static ID3D11PixelShader* s_pGTAOPS;
static ID3D11PixelShader* s_pBlurPS;
static ID3D11PixelShader* s_pCompositePS;
static TBOOL              s_bShadersFailed;

static AEffectsTarget s_oAO;
static AEffectsTarget s_oAOBlur;

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
	ReleaseShader( s_pHBAOPS );
	ReleaseShader( s_pGTAOPS );
	ReleaseShader( s_pBlurPS );
	ReleaseShader( s_pCompositePS );
}

static void DestroyTargets()
{
	AEffects::DestroyTarget( s_oAO );
	AEffects::DestroyTarget( s_oAOBlur );
}

static TBOOL CreateShaders()
{
	if ( s_pCompositePS )
		return TTRUE;

	if ( s_bShadersFailed )
		return TFALSE;

	TString8 strHBAO = s_szAOCommon;
	strHBAO += s_szHBAOShader;

	TString8 strGTAO = s_szAOCommon;
	strGTAO += s_szGTAOShader;

	s_pHBAOPS         = AEffects::CompilePostShader( strHBAO.GetString(), "ps_main" );
	s_pGTAOPS         = AEffects::CompilePostShader( strGTAO.GetString(), "ps_main" );
	s_pBlurPS         = AEffects::CompilePostShader( s_szBlurShader, "ps_main" );
	s_pCompositePS    = AEffects::CompilePostShader( s_szCompositeShader, "ps_main" );

	if ( !s_pHBAOPS || !s_pGTAOPS || !s_pBlurPS || !s_pCompositePS )
	{
		TERROR( "[fx] the ambient occlusion shaders don't compile, it stays off\n" );
		ReleaseShaders();
		s_bShadersFailed = TTRUE;
		return TFALSE;
	}

	return TTRUE;
}

TBOOL AAmbientOcclusion::IsOn()
{
	return AEffectsSettings::ms_iAO != 0 && !s_bShadersFailed;
}

void AAmbientOcclusion::Render( TRenderContext* a_pMainContext )
{
	if ( !IsOn() )
	{
		// Turned off: the memory goes
		if ( s_oAO.IsCreated() || s_oAOBlur.IsCreated() )
			DestroyTargets();

		return;
	}

	if ( !CreateShaders() )
		return;

	const UINT uiWidth   = AEffects::GetWidth();
	const UINT uiHeight  = AEffects::GetHeight();
	const UINT uiDivisor = AEffectsSettings::ms_iAOQuality ? 1 : 2;
	const UINT uiAOW     = TMath::Max( uiWidth / uiDivisor, 1u );
	const UINT uiAOH     = TMath::Max( uiHeight / uiDivisor, 1u );

	if ( ( !s_oAO.Matches( uiAOW, uiAOH, DXGI_FORMAT_R16_FLOAT, 1 ) && !AEffects::CreateTarget( s_oAO, "AO", uiAOW, uiAOH, DXGI_FORMAT_R16_FLOAT ) ) ||
	     ( !s_oAOBlur.Matches( uiAOW, uiAOH, DXGI_FORMAT_R16_FLOAT, 1 ) && !AEffects::CreateTarget( s_oAOBlur, "AO blur", uiAOW, uiAOH, DXGI_FORMAT_R16_FLOAT ) ) )
		return;

	AEffects::BeginTiming( AEFFECTS_TIMING_AO );

	// The depth: with MSAA its first sample, resolved by the core (the first effect asking pays for it)
	ID3D11ShaderResourceView* pDepthSRV = AEffects::GetDepthSRV();

	if ( !pDepthSRV )
	{
		AEffects::EndTiming( AEFFECTS_TIMING_AO );
		return;
	}

	const TMatrix44&                          rcProjection = TRenderContextD3D::Upcast( a_pMainContext )->GetProjectionMatrix();
	const TRenderContext::PROJECTIONPARAMS& rcProjParams = a_pMainContext->GetProjectionParams();
	const TBOOL                               bGTAO        = AEffectsSettings::ms_iAO == 2;

	// The occlusion
	AEffectsPostCB oConstants = {};
	oConstants.avParams[ 0 ]  = TVector4( rcProjection.m_f11, rcProjection.m_f22, rcProjection.m_f31, rcProjection.m_f32 );
	oConstants.avParams[ 1 ]  = TVector4( rcProjection.m_f33, rcProjection.m_f43, rcProjParams.m_fNearClip, rcProjParams.m_fFarClip );
	oConstants.avParams[ 2 ]  = TVector4( AEffectsSettings::ms_fAORadius, bGTAO ? GTAO_FALLOFF_RANGE : HBAO_BIAS, AEffectsSettings::ms_fAOIntensity, AO_POWER );
	oConstants.avParams[ 3 ]  = TVector4( TFLOAT( uiAOW ), TFLOAT( uiAOH ), 1.0f / uiAOW, 1.0f / uiAOH );
	oConstants.avParams[ 4 ]  = TVector4( GTAO_RADIUS_MULTIPLIER, GTAO_DISTRIBUTION_POWER, GTAO_THIN_OCCLUDER_COMPENSATE, 0.0f );
	oConstants.avParams[ 5 ]  = TVector4( a_pMainContext->IsFogEnabled() ? 1.0f : 0.0f, a_pMainContext->GetFogDistanceStart(), a_pMainContext->GetFogDistanceEnd(), 0.0f );
	AEffects::SetPostConstants( oConstants );

	AEffects::BindPostTarget( s_oAO.pRTV, uiAOW, uiAOH );
	AEffects::SetPostBlend( AEffects::POSTBLEND_NONE );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, pDepthSRV );
	AEffects::DrawPost( bGTAO ? s_pGTAOPS : s_pHBAOPS );

	// Depth aware blur, horizontal then vertical
	auto fnBlur = [ & ]( AEffectsTarget& a_rDst, AEffectsTarget& a_rSrc, TFLOAT a_fDirX, TFLOAT a_fDirY ) {
		AEffectsPostCB oBlur = {};
		oBlur.avParams[ 0 ]  = TVector4( 1.0f / uiAOW, 1.0f / uiAOH, a_fDirX, a_fDirY );
		oBlur.avParams[ 1 ]  = TVector4( rcProjParams.m_fNearClip, rcProjParams.m_fFarClip, AO_BLUR_SHARPNESS, 0.0f );
		AEffects::SetPostConstants( oBlur );

		AEffects::BindPostTarget( a_rDst.pRTV, uiAOW, uiAOH );
		AEffects::SetPostInput( AEffectsSlots::T_POST_0, a_rSrc.pSRV );
		AEffects::SetPostInput( AEffectsSlots::T_POST_1, pDepthSRV );
		AEffects::DrawPost( s_pBlurPS );
	};

	fnBlur( s_oAOBlur, s_oAO, 1.0f, 0.0f );
	fnBlur( s_oAO, s_oAOBlur, 0.0f, 1.0f );

	// Onto the scene
	auto pRender = TRenderD3DInterface::Interface();
	AEffects::BindPostTarget( pRender->GetBackBufferRTView(), uiWidth, uiHeight );
	AEffects::SetPostBlend( AEffectsSettings::ms_iAODebug ? AEffects::POSTBLEND_NONE : AEffects::POSTBLEND_MULTIPLY );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, s_oAO.pSRV );
	AEffects::DrawPost( s_pCompositePS );

	AEffects::EndTiming( AEFFECTS_TIMING_AO );
}

void AAmbientOcclusion::Destroy()
{
	DestroyTargets();
	ReleaseShaders();
	s_bShadersFailed = TFALSE;
}
