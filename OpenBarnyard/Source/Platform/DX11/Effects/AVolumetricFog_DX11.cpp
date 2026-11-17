#include "pch.h"
#include "AVolumetricFog_DX11.h"
#include "AShadowMaps_DX11.h"
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
// Shaders: VolumetricFog and VolumetricFogComposite of ERRender (Resources/Shaders) on the fullscreen triangle
// of the renderer. Not ported: the dynamic lights in the fog, and the temporal pass (a plain copy in ERRender);
// the ray jitter doesn't change between frames. Added: SHADOWS 0, the closed form without the sun shadows.
//
// g_avParams: [ 0 - 11 ] the light view projection of each cascade (AEffectsSunShadowsCB, transposed);
// [ 12 ] the cascade splits; [ 13 ] x the shadow bias; [ 14 - 17 ] view to world (transposed);
// [ 18 ] projection m11, m22, m31, m32; [ 19 ] near, far; [ 20 ] the direction towards the sun in view space;
// [ 21 ] the light colour; [ 22 ] density, anisotropy, max distance, intensity.
// t10 the depth (AEffects::GetDepthSRV), t12 the cascades, s8 their comparison sampler
//-----------------------------------------------------------------------------
static const TCHAR s_szFogShader[] = R"(
Texture2D g_Depth : register( t10 );

#if SHADOWS
Texture2DArray         g_ShadowMaps    : register( t12 );
SamplerComparisonState g_ShadowSampler : register( s8 );
#endif

static const float PI                        = 3.14159265f;
static const int   MAX_LIGHT_STEPS           = 384;
static const int   MAX_TRANSMITTANCE_STEPS   = 64;
static const float LIGHT_STEP_LENGTH         = 0.25f;
static const float TRANSMITTANCE_STEP_LENGTH = 0.5f;
static const float LIGHT_EXTINCTION_SCALE    = 0.05f;
static const float SHADOW_EXTINCTION_SCALE   = 0.25f;

// The registers hold matrices transposed: a dot product with each row is the row vector product
float4 Transform( float4 v, int iBase )
{
	return float4( dot( g_avParams[ iBase ], v ), dot( g_avParams[ iBase + 1 ], v ), dot( g_avParams[ iBase + 2 ], v ), dot( g_avParams[ iBase + 3 ], v ) );
}

float LinearizeDepth( float hwDepth )
{
	float invNear = 1.0f / max( g_avParams[ 19 ].x, 0.00001f );
	float invFar  = 1.0f / max( g_avParams[ 19 ].y, g_avParams[ 19 ].x + 0.00001f );
	return 1.0f / ( hwDepth * ( invFar - invNear ) + invNear );
}

// The view ray through a pixel (view space of the game: z forward, y down), z = 1
float3 UVToViewRay( float2 uv )
{
	float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
	return float3( ( ndc.x - g_avParams[ 18 ].z ) / g_avParams[ 18 ].x, ( ndc.y - g_avParams[ 18 ].w ) / g_avParams[ 18 ].y, 1.0f );
}

float HenyeyGreenstein( float cosTheta, float g )
{
	float g2    = g * g;
	float denom = max( 1.0f + g2 - 2.0f * g * cosTheta, 0.0001f );
	return ( 1.0f - g2 ) / ( 4.0f * PI * pow( denom, 1.5f ) );
}

float InterleavedGradientNoise( float2 pixel )
{
	return frac( 52.9829189f * frac( dot( pixel, float2( 0.06711056f, 0.00583715f ) ) ) );
}

float GetSceneViewDepth( float2 uv )
{
	float hwDepth = g_Depth.SampleLevel( g_PointClamp, uv, 0 ).r;
	return ( hwDepth >= 0.99999f ) ? g_avParams[ 19 ].y : LinearizeDepth( hwDepth );
}

#if SHADOWS
float SampleShadow( float3 worldPos, float viewDepth )
{
	int cascade = 2;
	if ( viewDepth < g_avParams[ 12 ].x ) cascade = 0;
	else if ( viewDepth < g_avParams[ 12 ].y ) cascade = 1;

	float4 shadowPos = Transform( float4( worldPos, 1.0f ), cascade * 4 );
	shadowPos.xyz /= shadowPos.w;

	float2 shadowUV = shadowPos.xy * float2( 0.5f, -0.5f ) + 0.5f;
	float  shadowZ  = shadowPos.z - g_avParams[ 13 ].x;

	if ( any( shadowUV < 0.0f ) || any( shadowUV > 1.0f ) || shadowPos.z < 0.0f || shadowPos.z > 1.0f )
		return 1.0f;

	return g_ShadowMaps.SampleCmpLevelZero( g_ShadowSampler, float3( shadowUV, (float)cascade ), shadowZ );
}
#endif

float3 IntegrateLightRay( float2 uv, float2 pixel )
{
	float  sceneViewDepth = GetSceneViewDepth( uv );
	float3 rayToSceneVS   = UVToViewRay( uv ) * sceneViewDepth;
	float  sceneDistance  = length( rayToSceneVS );
	float  rayDistance    = min( sceneDistance, g_avParams[ 22 ].z );

	if ( rayDistance <= 0.0001f )
		return 0.0f;

	float density = max( g_avParams[ 22 ].x, 0.0f );
	float g       = clamp( g_avParams[ 22 ].y, -0.95f, 0.95f );

	// The phase is tied to the camera, so a shaft doesn't fade as it moves towards the edge of the screen
	float3 cameraForwardVS = normalize( UVToViewRay( float2( 0.5f, 0.5f ) ) );
	float  cosTheta        = dot( cameraForwardVS, normalize( g_avParams[ 20 ].xyz ) );
	float  phase           = HenyeyGreenstein( cosTheta, g ) * ( 4.0f * PI );

	float  extinction = density * LIGHT_EXTINCTION_SCALE;
	float3 light      = phase * g_avParams[ 21 ].rgb * g_avParams[ 22 ].w;

#if SHADOWS
	float3 marchDirVS = rayToSceneVS / max( sceneDistance, 0.0001f );
	int    stepCount  = min( MAX_LIGHT_STEPS, max( 8, (int)ceil( rayDistance / LIGHT_STEP_LENGTH ) ) );
	float  stepSize   = rayDistance / (float)stepCount;
	float  jitter     = InterleavedGradientNoise( pixel );

	float stepTransmittance = exp( -extinction * stepSize );
	float stepScatter       = ( extinction > 0.00001f ) ? ( density / extinction ) * ( 1.0f - stepTransmittance ) : density * stepSize;

	float  transmittance = 1.0f;
	float3 radiance      = 0.0f;

	[loop] for ( int step = 0; step < MAX_LIGHT_STEPS; step++ )
	{
		if ( step >= stepCount )
			break;

		float  rayT     = ( (float)step + jitter ) * stepSize;
		float3 viewPos  = marchDirVS * rayT;
		float3 worldPos = Transform( float4( viewPos, 1.0f ), 14 ).xyz;

		radiance += transmittance * SampleShadow( worldPos, viewPos.z ) * light * stepScatter;
		transmittance *= stepTransmittance;

		if ( transmittance < 0.001f )
			break;
	}

	return radiance;
#else
	// Nothing in the way: the sum of the march in closed form
	float scatter = ( extinction > 0.00001f ) ? ( density / extinction ) * ( 1.0f - exp( -extinction * rayDistance ) ) : density * rayDistance;
	return light * scatter;
#endif
}

float4 ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	return float4( IntegrateLightRay( i.vUV, i.vPosition.xy ), 1.0f );
}

#if SHADOWS
// How much of the scene shows through the shadowed fog
float4 ps_visibility( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float  sceneViewDepth = GetSceneViewDepth( i.vUV );
	float3 rayToSceneVS   = UVToViewRay( i.vUV ) * sceneViewDepth;
	float  sceneDistance  = length( rayToSceneVS );
	float  rayDistance    = min( sceneDistance, g_avParams[ 22 ].z );

	if ( rayDistance <= 0.0001f )
		return float4( 1.0f, 1.0f, 1.0f, 1.0f );

	float3 marchDirVS = rayToSceneVS / max( sceneDistance, 0.0001f );
	float  density    = max( g_avParams[ 22 ].x, 0.0f );
	float  amount     = saturate( g_avParams[ 22 ].w );

	int   stepCount = min( MAX_TRANSMITTANCE_STEPS, max( 8, (int)ceil( rayDistance / TRANSMITTANCE_STEP_LENGTH ) ) );
	float stepSize  = rayDistance / (float)stepCount;
	float jitter    = InterleavedGradientNoise( i.vPosition.xy );

	float shadowedOpticalDepth = 0.0f;

	[loop] for ( int step = 0; step < MAX_TRANSMITTANCE_STEPS; step++ )
	{
		if ( step >= stepCount )
			break;

		float  rayT     = ( (float)step + jitter ) * stepSize;
		float3 viewPos  = marchDirVS * rayT;
		float3 worldPos = Transform( float4( viewPos, 1.0f ), 14 ).xyz;

		shadowedOpticalDepth += ( 1.0f - SampleShadow( worldPos, viewPos.z ) ) * density * SHADOW_EXTINCTION_SCALE * stepSize;
	}

	float transmittance = exp( -shadowedOpticalDepth * amount );
	return float4( transmittance, transmittance, transmittance, 1.0f );
}
#endif
)";

// Depth aware upsampling onto the scene. t10 the fog, t11 the depth; g_avParams[ 0 ]: near, far, depth sharpness, darken
static const TCHAR s_szCompositeShader[] = R"(
Texture2D g_Fog : register( t10 );
Texture2D g_Depth : register( t11 );

float LinearizeDepth( float hwDepth )
{
	float invNear = 1.0f / max( g_avParams[ 0 ].x, 0.00001f );
	float invFar  = 1.0f / max( g_avParams[ 0 ].y, g_avParams[ 0 ].x + 0.00001f );
	return 1.0f / ( hwDepth * ( invFar - invNear ) + invNear );
}

float3 SampleDepthAwareFog( float2 uv )
{
	uint width, height;
	g_Fog.GetDimensions( width, height );

	float2 fogSize = float2( (float)width, (float)height );
	float2 fogPos  = uv * fogSize - 0.5f;
	float2 basePos = floor( fogPos );
	float2 fracPos = fogPos - basePos;

	float centerDepth = LinearizeDepth( g_Depth.SampleLevel( g_PointClamp, uv, 0 ).r );

	float3 fogSum    = 0.0f;
	float  weightSum = 0.0f;

	[unroll] for ( int y = 0; y < 2; y++ )
	{
		[unroll] for ( int x = 0; x < 2; x++ )
		{
			float2 tapPos = basePos + float2( (float)x, (float)y );
			float2 tapUV  = saturate( ( tapPos + 0.5f ) / fogSize );

			float2 bilinearWeights = lerp( 1.0f - fracPos, fracPos, float2( (float)x, (float)y ) );
			float  bilinearWeight  = bilinearWeights.x * bilinearWeights.y;

			float tapDepth    = LinearizeDepth( g_Depth.SampleLevel( g_PointClamp, tapUV, 0 ).r );
			float depthWeight = exp( -abs( tapDepth - centerDepth ) * g_avParams[ 0 ].z / max( centerDepth, 1.0f ) );
			float weight      = bilinearWeight * depthWeight;

			fogSum += g_Fog.SampleLevel( g_PointClamp, tapUV, 0 ).rgb * weight;
			weightSum += weight;
		}
	}

	return fogSum / max( weightSum, 0.0001f );
}

float4 ps_main( FULLSCREEN_PS_IN i ) : SV_TARGET
{
	float3 fog = SampleDepthAwareFog( i.vUV );

	// Darken: multiplied onto the scene, otherwise added
	if ( g_avParams[ 0 ].w != 0.0f )
		return float4( fog.rrr, 1.0f );

	return float4( fog, 1.0f );
}
)";

// ERRender's defaults (ERRenderWrapper.cpp)
static constexpr TFLOAT FOG_DENSITY         = 0.019f;
static constexpr TFLOAT FOG_ANISOTROPY      = 0.0f;
static constexpr TFLOAT FOG_MAX_DISTANCE    = 44.0f;
static constexpr TFLOAT FOG_INTENSITY       = 0.16f;
static constexpr TFLOAT FOG_COLOUR[ 3 ]     = { 0.937f, 0.8f, 0.5254f };
static constexpr TFLOAT FOG_DEPTH_SHARPNESS = 24.0f;

enum SHADER
{
	SHADER_LIGHT,              // additive, no shadows
	SHADER_LIGHT_SHADOWS,      // additive through the cascades
	SHADER_VISIBILITY_SHADOWS, // darken
	SHADER_NUMOF
};

static ID3D11PixelShader* s_apShaders[ SHADER_NUMOF ];
static TBOOL              s_abShaderFailed[ SHADER_NUMOF ];
static ID3D11PixelShader* s_pCompositePS;
static TBOOL              s_bCompositeFailed;
static TBOOL              s_bLoggedNoDarken;

static AEffectsTarget s_oFog; // a quarter of the resolution

static ID3D11PixelShader* GetShader( SHADER a_eShader )
{
	if ( s_apShaders[ a_eShader ] || s_abShaderFailed[ a_eShader ] )
		return s_apShaders[ a_eShader ];

	static const D3D_SHADER_MACRO s_aNoShadows[] = { { "SHADOWS", "0" }, { TNULL, TNULL } };
	static const D3D_SHADER_MACRO s_aShadows[]   = { { "SHADOWS", "1" }, { TNULL, TNULL } };

	// The cascades are a texture array read with comparisons: ps_4_1, like the receivers of the sun shadows
	switch ( a_eShader )
	{
		case SHADER_LIGHT:
			s_apShaders[ a_eShader ] = AEffects::CompilePostShader( s_szFogShader, "ps_main", s_aNoShadows );
			break;
		case SHADER_LIGHT_SHADOWS:
			s_apShaders[ a_eShader ] = AEffects::CompilePostShader( s_szFogShader, "ps_main", s_aShadows, "ps_4_1" );
			break;
		default:
			s_apShaders[ a_eShader ] = AEffects::CompilePostShader( s_szFogShader, "ps_visibility", s_aShadows, "ps_4_1" );
			break;
	}

	if ( !s_apShaders[ a_eShader ] )
	{
		TERROR( "[fx] a volumetric fog shader doesn't compile (%d)\n", TINT( a_eShader ) );
		s_abShaderFailed[ a_eShader ] = TTRUE;
	}

	return s_apShaders[ a_eShader ];
}

static ID3D11PixelShader* GetCompositeShader()
{
	if ( !s_pCompositePS && !s_bCompositeFailed )
	{
		s_pCompositePS = AEffects::CompilePostShader( s_szCompositeShader, "ps_main" );

		if ( !s_pCompositePS )
		{
			TERROR( "[fx] the volumetric fog composite shader doesn't compile\n" );
			s_bCompositeFailed = TTRUE;
		}
	}

	return s_pCompositePS;
}

TBOOL AVolumetricFog::IsOn()
{
	return AEffectsSettings::ms_iVolumetricFog != 0 && !s_bCompositeFailed;
}

void AVolumetricFog::Render( TRenderContext* a_pMainContext )
{
	if ( !IsOn() )
	{
		// Turned off: the memory goes
		if ( s_oFog.IsCreated() )
			AEffects::DestroyTarget( s_oFog );

		return;
	}

	const TBOOL                bDarken        = AEffectsSettings::ms_iVolumetricFog == 2;
	ID3D11ShaderResourceView* pShadowSRV     = AShadowMaps::GetSunShadowSRV();
	ID3D11SamplerState*       pShadowSampler = AShadowMaps::GetSunShadowSampler();
	const TBOOL                bShadows       = pShadowSRV && pShadowSampler;

	// The darkening is the shadow in the fog, there's nothing to darken without the sun shadows
	if ( bDarken && !bShadows )
	{
		if ( !s_bLoggedNoDarken )
		{
			TINFO( "[fx] volumetric fog 2 (darken) needs the sun shadows (render.sun_shadows), nothing to do without them\n" );
			s_bLoggedNoDarken = TTRUE;
		}

		return;
	}

	// The light of the visible sun: none at night
	TFLOAT   fStrength = 0.0f;
	TVector4 vSunDir( 0.0f, -1.0f, 0.0f, 0.0f );

	if ( ASunFlareEffect::IsSingletonCreated() && ASunFlareEffect::GetSingleton()->IsEnabled() )
	{
		fStrength = ASunFlareEffect::GetSingleton()->GetSunIntensity();
		vSunDir   = ASunFlareEffect::GetSingleton()->GetSunDirection();
	}

	if ( fStrength > 1.0f ) fStrength = 1.0f;
	if ( fStrength <= 0.01f )
		return;

	ID3D11PixelShader* pShader          = GetShader( bDarken ? SHADER_VISIBILITY_SHADOWS : ( bShadows ? SHADER_LIGHT_SHADOWS : SHADER_LIGHT ) );
	ID3D11PixelShader* pCompositeShader = GetCompositeShader();

	if ( !pShader || !pCompositeShader )
		return;

	const UINT uiWidth    = AEffects::GetWidth();
	const UINT uiHeight   = AEffects::GetHeight();
	const UINT uiQuarterW = TMath::Max( uiWidth >> 2, 1u );
	const UINT uiQuarterH = TMath::Max( uiHeight >> 2, 1u );

	if ( !s_oFog.Matches( uiQuarterW, uiQuarterH, DXGI_FORMAT_R11G11B10_FLOAT, 1 ) && !AEffects::CreateTarget( s_oFog, "volumetric fog", uiQuarterW, uiQuarterH, DXGI_FORMAT_R11G11B10_FLOAT ) )
		return;

	AEffects::BeginTiming( AEFFECTS_TIMING_FOG );

	ID3D11ShaderResourceView* pDepthSRV = AEffects::GetDepthSRV();

	if ( !pDepthSRV )
	{
		AEffects::EndTiming( AEFFECTS_TIMING_FOG );
		return;
	}

	const TMatrix44&                        rcProjection = TRenderContextD3D::Upcast( a_pMainContext )->GetProjectionMatrix();
	const TRenderContext::PROJECTIONPARAMS& rcProjParams = a_pMainContext->GetProjectionParams();

	// The direction towards the sun in view space
	vSunDir.w = 0.0f;
	TVector4 vSunView;
	TMatrix44::RotateVector( vSunView, a_pMainContext->GetWorldViewMatrix(), vSunDir );
	const TFLOAT fSunLength = TMath::Sqrt( vSunView.x * vSunView.x + vSunView.y * vSunView.y + vSunView.z * vSunView.z );
	if ( fSunLength > 0.0001f ) vSunView.Multiply( 1.0f / fSunLength );

	TMatrix44 oViewWorldTransposed;
	oViewWorldTransposed.Transpose( a_pMainContext->GetViewWorldMatrix() );

	AEffectsPostCB oConstants = {};

	if ( bShadows )
	{
		const AEffectsSunShadowsCB& rcShadows = AShadowMaps::GetSunShadowConstants();

		for ( TINT i = 0; i < AEffectsSunShadowsCB::NUM_CASCADES; i++ )
			TUtil::MemCopy( &oConstants.avParams[ i * 4 ], &rcShadows.amLightViewProjection[ i ], sizeof( TMatrix44 ) );

		oConstants.avParams[ 12 ] = rcShadows.vCascadeSplits;
		oConstants.avParams[ 13 ] = rcShadows.vShadowParams;
	}

	TUtil::MemCopy( &oConstants.avParams[ 14 ], &oViewWorldTransposed, sizeof( TMatrix44 ) );
	oConstants.avParams[ 18 ] = TVector4( rcProjection.m_f11, rcProjection.m_f22, rcProjection.m_f31, rcProjection.m_f32 );
	oConstants.avParams[ 19 ] = TVector4( rcProjParams.m_fNearClip, rcProjParams.m_fFarClip, 0.0f, 0.0f );
	oConstants.avParams[ 20 ] = TVector4( vSunView.x, vSunView.y, vSunView.z, 0.0f );
	oConstants.avParams[ 21 ] = TVector4( FOG_COLOUR[ 0 ] * fStrength, FOG_COLOUR[ 1 ] * fStrength, FOG_COLOUR[ 2 ] * fStrength, 1.0f );
	oConstants.avParams[ 22 ] = TVector4( FOG_DENSITY, FOG_ANISOTROPY, FOG_MAX_DISTANCE, bDarken ? FOG_INTENSITY * fStrength : FOG_INTENSITY );
	AEffects::SetPostConstants( oConstants );

	// The fog at a quarter of the resolution
	AEffects::BindPostTarget( s_oFog.pRTV, uiQuarterW, uiQuarterH );
	AEffects::SetPostBlend( AEffects::POSTBLEND_NONE );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, pDepthSRV );

	if ( bShadows )
	{
		AEffects::SetPostInput( AEffectsSlots::T_POST_2, pShadowSRV );
		TRenderD3DInterface::Interface()->PSSetSampler( AEffectsSlots::S_SUN_SHADOW, pShadowSampler );
	}

	AEffects::DrawPost( pShader );

	// Onto the scene, upsampled along the depth
	AEffectsPostCB oComposite = {};
	oComposite.avParams[ 0 ]  = TVector4( rcProjParams.m_fNearClip, rcProjParams.m_fFarClip, FOG_DEPTH_SHARPNESS, bDarken ? 1.0f : 0.0f );
	AEffects::SetPostConstants( oComposite );

	AEffects::BindPostTarget( TRenderD3DInterface::Interface()->GetBackBufferRTView(), uiWidth, uiHeight );
	AEffects::SetPostBlend( bDarken ? AEffects::POSTBLEND_MULTIPLY : AEffects::POSTBLEND_ADD );
	AEffects::SetPostInput( AEffectsSlots::T_POST_0, s_oFog.pSRV );
	AEffects::SetPostInput( AEffectsSlots::T_POST_1, pDepthSRV );
	AEffects::DrawPost( pCompositeShader );

	AEffects::EndTiming( AEFFECTS_TIMING_FOG );
}

void AVolumetricFog::Destroy()
{
	AEffects::DestroyTarget( s_oFog );

	for ( TINT i = 0; i < SHADER_NUMOF; i++ )
	{
		if ( s_apShaders[ i ] )
		{
			s_apShaders[ i ]->Release();
			s_apShaders[ i ] = TNULL;
		}

		s_abShaderFailed[ i ] = TFALSE;
	}

	if ( s_pCompositePS )
	{
		s_pCompositePS->Release();
		s_pCompositePS = TNULL;
	}

	s_bCompositeFailed = TFALSE;
	s_bLoggedNoDarken  = TFALSE;
}
