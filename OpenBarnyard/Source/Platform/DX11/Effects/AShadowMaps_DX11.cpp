#include "pch.h"
#include "AShadowMaps_DX11.h"
#include "AEffects_DX11.h"
#include "ADynamicLights_DX11.h"

#include "Animation/AModelRepos.h"
#include "Terrain/ATerrainInterface.h"
#include "Helpers/ATreeManager.h"
#include "Helpers/AInstanceManager.h"
#include "Render/AShadowManager.h"
#include "Helpers/AGameTimeManager.h"
#include "Platform/DX11/AWorldShader/AWorldShader_DX11.h"

#include <Toshi/TSystem.h>
#include <Render/TRenderInterface.h>
#include <Platform/DX11/TRenderInterface_DX11.h>
#include <Platform/DX11/TRenderContext_DX11.h>
#include <Platform/DX11/TConstantBuffer_DX11.h>

// NOTE: the physics headers pull in Opcode's PI macro, keep them last
#include "Helpers/AGateManager.h"
#include "Helpers/ARegrowthManager.h"

#include <cfloat>
#include <cmath>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

//-----------------------------------------------------------------------------
// Constants of ERRender's CSMManager.h / .cpp
//-----------------------------------------------------------------------------
static constexpr TINT   NUM_CASCADES             = AEffectsSunShadowsCB::NUM_CASCADES;
static constexpr TFLOAT CSM_SPLIT_LAMBDA         = 0.5f;
static constexpr TFLOAT CSM_DEPTH_BIAS_SLOPE     = 2.0f;
static constexpr TINT   CSM_DEPTH_BIAS_UNITS     = 100;
static constexpr TFLOAT CSM_MIN_SLOPE_DEPTH_BIAS = 0.25f;
static constexpr TFLOAT CSM_RECEIVER_BIAS        = 0.0005f;
static constexpr TFLOAT CSM_RECEIVER_PLANE_BIAS  = 1.0f;
static constexpr TFLOAT CSM_PCF_RADIUS           = 1.45f;
static constexpr TFLOAT CSM_CASCADE_BLEND        = 0.1f;
static constexpr TFLOAT CSM_CASTER_PADDING       = 3.0f;
static constexpr TFLOAT CSM_CASCADE_PADDING      = 8.0f;
static constexpr TFLOAT CSM_SUN_AZIMUTH          = 9.0f;   // render.sun_dir_override, degrees
static constexpr TFLOAT CSM_SUN_ELEVATION        = 139.0f; // degrees

// The spot lights of ERRender's DynamicGlowLights.cpp
static constexpr TINT   LIGHT_SHADOW_SLICES      = ADynamicLights::MAX_SHADOWS;
static constexpr TFLOAT LIGHT_SHADOW_DISTANCE    = 45.0f; // the lights farther from the camera have no shadow map
static constexpr TFLOAT LIGHT_DEPTH_BIAS_SLOPE   = 2.0f;

//-----------------------------------------------------------------------------
// The receivers: ERRender's ShadowSampling.hlsli (062903d^) with the registers of AEffectsSlots
//-----------------------------------------------------------------------------
static const TCHAR s_szReceiverSource[] = R"(
#ifdef SUN_SHADOWS
cbuffer SunShadows : register( b1 )
{
	float4x4 g_amSunLightVP[ 3 ];      // world to the clip space of each cascade
	float4   g_vSunCascadeSplits;      // view depth where the cascades end
	float4   g_vSunShadowParams;       // receiver bias, texel size, debug cascade + 1, intensity (night fade included)
	float4   g_vSunShadowFilterParams; // PCF radius (texels), receiver plane bias, debug mask, cascade blend
};

Texture2DArray         g_SunShadowMap     : register( t8 );
SamplerComparisonState g_SunShadowSampler : register( s8 );

float2 SunReceiverPlaneDepthBias( float3 a_vShadowCoord, float a_fTexelSize )
{
	float3 vDDX = ddx( a_vShadowCoord );
	float3 vDDY = ddy( a_vShadowCoord );

	float fDeterminant = vDDX.x * vDDY.y - vDDX.y * vDDY.x;

	if ( abs( fDeterminant ) < 0.000001f )
		return float2( 0.0f, 0.0f );

	float    fInvDeterminant = 1.0f / fDeterminant;
	float2x2 mShadowToScreen = float2x2( vDDY.y * fInvDeterminant, -vDDX.y * fInvDeterminant, -vDDY.x * fInvDeterminant, vDDX.x * fInvDeterminant );

	float2 vRightRatio = mul( float2( a_fTexelSize, 0.0f ), mShadowToScreen );
	float2 vUpRatio    = mul( float2( 0.0f, a_fTexelSize ), mShadowToScreen );
	float2 vDepthDeriv = float2( vDDX.z, vDDY.z );

	return float2( dot( vRightRatio, vDepthDeriv ), dot( vUpRatio, vDepthDeriv ) );
}

// The gradient dependent part, in uniform control flow (ddx / ddy)
struct SunShadowSetup
{
	float2 vUV;
	float  fZ;
	float2 vBiasPerTexel;
	bool   bOutside;
};

SunShadowSetup GetSunShadowSetup( float3 a_vWorldPos, int a_iCascade )
{
	float4 vShadowPos = mul( float4( a_vWorldPos, 1.0f ), g_amSunLightVP[ a_iCascade ] );
	vShadowPos.xyz /= vShadowPos.w;

	SunShadowSetup oSetup;
	oSetup.vUV           = vShadowPos.xy * float2( 0.5f, -0.5f ) + 0.5f;
	oSetup.fZ            = vShadowPos.z - g_vSunShadowParams.x;
	oSetup.bOutside      = any( oSetup.vUV < 0.0f ) || any( oSetup.vUV > 1.0f ) || vShadowPos.z < 0.0f || vShadowPos.z > 1.0f;
	oSetup.vBiasPerTexel = SunReceiverPlaneDepthBias( float3( oSetup.vUV, oSetup.fZ ), g_vSunShadowParams.y ) * g_vSunShadowFilterParams.y;
	return oSetup;
}

float SampleSunShadowPCF( SunShadowSetup a_oSetup, int a_iCascade )
{
	if ( a_oSetup.bOutside )
		return 1.0f;

	float fShadow    = 0.0f;
	float fTexelSize = g_vSunShadowParams.y;

	// Widen the footprint of the same nine weighted comparisons. The receiver-plane correction follows
	// the actual sample offsets to keep the shadow attached on sloped ground.
	[unroll] for ( int iY = -1; iY <= 1; iY++ )
	{
		[unroll] for ( int iX = -1; iX <= 1; iX++ )
		{
			float2 vOffset = float2( iX, iY ) * g_vSunShadowFilterParams.x;
			float  fWeight = ( 2.0f - abs( (float)iX ) ) * ( 2.0f - abs( (float)iY ) );

			fShadow += g_SunShadowMap.SampleCmpLevelZero( g_SunShadowSampler, float3( a_oSetup.vUV + vOffset * fTexelSize, a_iCascade ), a_oSetup.fZ + dot( a_oSetup.vBiasPerTexel, vOffset ) ) * fWeight;
		}
	}

	return fShadow * ( 1.0f / 16.0f );
}

// 1 lit, 0 in the shadow of the sun
float GetSunShadowVisibility( float3 a_vWorldPos, float a_fViewDepth )
{
	int iCascade = 2;

	if ( a_fViewDepth < g_vSunCascadeSplits.x ) iCascade = 0;
	else if ( a_fViewDepth < g_vSunCascadeSplits.y ) iCascade = 1;

	// Debug: one cascade only, optionally nothing outside of it
	bool bMasked = false;

	if ( g_vSunShadowParams.z > 0.5f )
	{
		int iForced = (int)g_vSunShadowParams.z - 1;
		bMasked     = g_vSunShadowFilterParams.z > 0.5f && iCascade != iForced;
		iCascade    = iForced;
	}

	int            iNext      = min( iCascade + 1, 2 );
	SunShadowSetup oSetup     = GetSunShadowSetup( a_vWorldPos, iCascade );
	SunShadowSetup oNextSetup = GetSunShadowSetup( a_vWorldPos, iNext );

	float fShadow = SampleSunShadowPCF( oSetup, iCascade );

	// Cross fade into the next cascade at the end of one
	float fBlendFraction = g_vSunShadowFilterParams.w;
	float fSplit         = ( iCascade == 0 ) ? g_vSunCascadeSplits.x : g_vSunCascadeSplits.y;
	float fPrevSplit     = ( iCascade == 0 ) ? 0.0f : g_vSunCascadeSplits.x;
	float fBlendBand     = max( ( fSplit - fPrevSplit ) * fBlendFraction, 1e-4f );
	float fFadeStart     = fSplit - fBlendBand;

	if ( fBlendFraction > 0.0f && iCascade < 2 && a_fViewDepth > fFadeStart && g_vSunShadowParams.z < 0.5f )
		fShadow = lerp( fShadow, SampleSunShadowPCF( oNextSetup, iNext ), saturate( ( a_fViewDepth - fFadeStart ) / fBlendBand ) );

	if ( bMasked )
		fShadow = 1.0f;

	// Not in ERRender: the shadows fade out over the last tenth of the distance instead of ending at once
	float fEnd = g_vSunCascadeSplits.z;
	return lerp( fShadow, 1.0f, saturate( ( a_fViewDepth - fEnd * 0.9f ) / max( fEnd * 0.1f, 1e-4f ) ) );
}

// How much of the sun reaches the point: 1 lit, 1 - intensity in a full shadow
float GetSunShadow( float3 a_vWorldPos, float a_fViewDepth )
{
	return 1.0f - g_vSunShadowParams.w * ( 1.0f - GetSunShadowVisibility( a_vWorldPos, a_fViewDepth ) );
}
#endif // SUN_SHADOWS

#ifdef DYNAMIC_LIGHTS
// ADynamicLights_DX11: ERRender's DynamicLights.hlsli and ShaderUtils.hlsli with the registers of AEffectsSlots
cbuffer DynamicLights : register( b2 )
{
	float4   g_avLightPositionRadius[ 4 ]; // xyz world position, w radius
	float4   g_avLightDirectionCone[ 4 ];  // xyz direction (from the light to the scene), w cos of the outer cone
	float4x4 g_amLightVP[ 4 ];             // world to the clip space of the light
	float4   g_avLightShadowParams[ 4 ];   // x shadow map slice (< 0: none), y texel size, z bias, w strength
	float4   g_avLightColour[ 4 ];         // xyz colour, w volumetric intensity
	float4   g_avLightIntensity[ 4 ];      // x surface intensity, y bump scale, z cos of the inner cone
	float4   g_vLightParams;               // x number of lights
};

Texture2DArray         g_LightShadowMaps    : register( t9 );
SamplerComparisonState g_LightShadowSampler : register( s9 );

float4 GetLightProjectedPosition( float3 a_vWorldPos, int a_iLight )
{
	float4 vPos = mul( float4( a_vWorldPos, 1.0f ), g_amLightVP[ a_iLight ] );
	vPos.xyz /= vPos.w;
	return vPos;
}

float SampleLightShadow( int a_iLight, float4 a_vShadowPos )
{
	float4 vParams = g_avLightShadowParams[ a_iLight ];

	if ( vParams.x < 0.0f || vParams.w <= 0.0f )
		return 1.0f;

	float2 vUV = a_vShadowPos.xy * float2( 0.5f, -0.5f ) + 0.5f;
	float  fZ  = a_vShadowPos.z - vParams.z;

	if ( any( vUV < 0.0f ) || any( vUV > 1.0f ) || a_vShadowPos.z < 0.0f || a_vShadowPos.z > 1.0f )
		return 1.0f;

	float fShadow = 0.0f;

	[unroll] for ( int iY = -1; iY <= 1; iY++ )
	{
		[unroll] for ( int iX = -1; iX <= 1; iX++ )
			fShadow += g_LightShadowMaps.SampleCmpLevelZero( g_LightShadowSampler, float3( vUV + float2( iX, iY ) * vParams.y, vParams.x ), fZ );
	}

	return lerp( 1.0f, fShadow * ( 1.0f / 9.0f ), vParams.w );
}

// A normal from the luminance gradient of the albedo, with the TBN from screen space derivatives
float3 ComputeDerivedWorldNormal( float3 a_vWorldPos, float3 a_vWorldNormal, float2 a_vUV, Texture2D a_Texture, SamplerState a_Sampler, float a_fBumpScale )
{
	float3   vN      = normalize( a_vWorldNormal );
	float3   vDP1    = ddx( a_vWorldPos ), vDP2 = ddy( a_vWorldPos );
	float2   vDU1    = ddx( a_vUV ), vDU2 = ddy( a_vUV );
	float3   vDP2Perp = cross( vDP2, vN ), vDP1Perp = cross( vN, vDP1 );
	float3   vT      = vDP2Perp * vDU1.x + vDP1Perp * vDU2.x;
	float3   vB      = vDP2Perp * vDU1.y + vDP1Perp * vDU2.y;
	float    fInvMax = rsqrt( max( max( dot( vT, vT ), dot( vB, vB ) ), 1e-20f ) );
	float3x3 mTBN    = float3x3( vT * fInvMax, vB * fInvMax, vN );

	float  fStepU = length( float2( ddx( a_vUV.x ), ddy( a_vUV.x ) ) );
	float  fStepV = length( float2( ddx( a_vUV.y ), ddy( a_vUV.y ) ) );
	float3 vLuma  = float3( 0.299f, 0.587f, 0.114f );
	float  fL     = dot( a_Texture.Sample( a_Sampler, a_vUV - float2( fStepU, 0.0f ) ).rgb, vLuma );
	float  fR     = dot( a_Texture.Sample( a_Sampler, a_vUV + float2( fStepU, 0.0f ) ).rgb, vLuma );
	float  fD     = dot( a_Texture.Sample( a_Sampler, a_vUV - float2( 0.0f, fStepV ) ).rgb, vLuma );
	float  fU     = dot( a_Texture.Sample( a_Sampler, a_vUV + float2( 0.0f, fStepV ) ).rgb, vLuma );

	float3 vTangentNormal = normalize( float3( ( fL - fR ) * a_fBumpScale, ( fU - fD ) * a_fBumpScale, 1.0f ) );
	return normalize( mul( vTangentNormal, mTBN ) );
}

float3 SampleDynamicLight( float3 a_vWorldPos, float3 a_vNormal, int a_iLight, float a_fIntensity )
{
	float4 vPositionRadius = g_avLightPositionRadius[ a_iLight ];
	float4 vDirectionCone  = g_avLightDirectionCone[ a_iLight ];
	float4 vProjected      = GetLightProjectedPosition( a_vWorldPos, a_iLight );
	float2 vLightUV        = vProjected.xy * float2( 0.5f, -0.5f ) + 0.5f;

	if ( any( vLightUV < 0.0f ) || any( vLightUV > 1.0f ) || vProjected.z < 0.0f || vProjected.z > 1.0f )
		return 0.0f;

	float3 vToPixel = a_vWorldPos - vPositionRadius.xyz;
	float  fDist    = length( vToPixel );
	float  fRadius  = max( vPositionRadius.w, 0.001f );

	float fD           = saturate( fDist / fRadius );
	float fSmoothCut   = saturate( 1.0f - fD * fD );
	fSmoothCut        *= fSmoothCut;
	float fAttenuation = fSmoothCut * ( 1.75f / ( 1.0f + 8.0f * fD * fD ) );

	float3 vDir       = vToPixel / max( fDist, 0.001f );
	float  fNdotL     = ( dot( a_vNormal, a_vNormal ) > 0.25f ) ? saturate( dot( a_vNormal, -vDir ) ) : 1.0f;
	float  fSpot      = smoothstep( vDirectionCone.w, g_avLightIntensity[ a_iLight ].z, dot( vDir, vDirectionCone.xyz ) );
	float2 vEdgeFade  = saturate( min( vLightUV, 1.0f - vLightUV ) * 8.0f );
	float  fShadow    = SampleLightShadow( a_iLight, vProjected );

	return g_avLightColour[ a_iLight ].rgb * ( fAttenuation * fSpot * vEdgeFade.x * vEdgeFade.y * fShadow * fNdotL * a_fIntensity );
}

// The light on a surface
float3 GetDynamicLights( float3 a_vWorldPos, float3 a_vNormal )
{
	float3 vGlow   = 0.0f;
	int    iLights = (int)g_vLightParams.x;

	if ( iLights > 0 ) vGlow += SampleDynamicLight( a_vWorldPos, a_vNormal, 0, g_avLightIntensity[ 0 ].x );
	if ( iLights > 1 ) vGlow += SampleDynamicLight( a_vWorldPos, a_vNormal, 1, g_avLightIntensity[ 1 ].x );
	if ( iLights > 2 ) vGlow += SampleDynamicLight( a_vWorldPos, a_vNormal, 2, g_avLightIntensity[ 2 ].x );
	if ( iLights > 3 ) vGlow += SampleDynamicLight( a_vWorldPos, a_vNormal, 3, g_avLightIntensity[ 3 ].x );

	return vGlow;
}

// The light in the air (volumetric fog): the volumetric intensity, no surface
float3 GetDynamicLightsVolumetric( float3 a_vWorldPos )
{
	float3 vGlow   = 0.0f;
	int    iLights = (int)g_vLightParams.x;

	if ( iLights > 0 ) vGlow += SampleDynamicLight( a_vWorldPos, 0.0f, 0, g_avLightColour[ 0 ].w );
	if ( iLights > 1 ) vGlow += SampleDynamicLight( a_vWorldPos, 0.0f, 1, g_avLightColour[ 1 ].w );
	if ( iLights > 2 ) vGlow += SampleDynamicLight( a_vWorldPos, 0.0f, 2, g_avLightColour[ 2 ].w );
	if ( iLights > 3 ) vGlow += SampleDynamicLight( a_vWorldPos, 0.0f, 3, g_avLightColour[ 3 ].w );

	return vGlow;
}

// The surface keeps its hue: white light on red grass stays red, only brighter
float3 ApplyDynamicLights( float3 a_vColour, float3 a_vGlow )
{
	return a_vColour * ( 1.0f + a_vGlow );
}

// How much the lights take a shadow away (1: fully lit by them)
float GetDynamicLightsShadowCancel( float3 a_vGlow )
{
	return saturate( max( a_vGlow.r, max( a_vGlow.g, a_vGlow.b ) ) );
}
#endif // DYNAMIC_LIGHTS

#ifdef ALPHA_COVERAGE
// Alpha to coverage for the alpha test of the game (TRenderD3DInterface::GetAlphaTestParams, render.alpha_coverage):
// under MSAA the samples a pixel covers follow its alpha across one pixel around the reference (SV_Coverage of
// ps_4_1), so the resolve smooths the edges of the cut out textures: foliage, grass, fences. The edge stays where the
// alpha test puts it. Without MSAA, and for other functions than GREATER / GREATEREQUAL, it is the alpha test
uint AlphaTestCoverage( float a_fAlpha, float4 a_vParams, float2 a_vPixel )
{
	// The gradient first, in uniform control flow
	float fWidth    = max( fwidth( a_fAlpha ), 1.0f / 512.0f );
	uint  uiSamples = GetRenderTargetSampleCount();
	int   iFunc     = int( a_vParams.z );
	uint  uiResult  = 0xFFFFFFFF;

	if ( a_vParams.x == 0.0f || uiSamples <= 1 || uiSamples > 16 || ( iFunc != 5 && iFunc != 7 ) )
	{
		AlphaTest( a_fAlpha, a_vParams );
	}
	else
	{
		float fCoverage = saturate( ( a_fAlpha - a_vParams.y ) / fWidth + 0.5f );
		uint  uiCount   = uint( fCoverage * uiSamples + 0.5f );

		if ( uiCount == 0 )
			discard;

		if ( uiCount < uiSamples )
		{
			// The covered samples turn with the pixel: the same samples everywhere would band
			uint uiMask   = ( 1u << uiCount ) - 1u;
			uint uiRotate = ( uint( a_vPixel.x ) + 2u * uint( a_vPixel.y ) ) % uiSamples;
			uiResult      = ( ( uiMask << uiRotate ) | ( uiMask >> ( uiSamples - uiRotate ) ) ) & ( ( 1u << uiSamples ) - 1u );
		}
	}

	return uiResult;
}
#endif // ALPHA_COVERAGE
)";

//-----------------------------------------------------------------------------
// State
//-----------------------------------------------------------------------------
struct ShadowMapArray
{
	ID3D11Texture2D*          pTexture = TNULL;
	ID3D11ShaderResourceView* pSRV     = TNULL;
	ID3D11DepthStencilView*   apDSV[ 4 ] = {};
	UINT                      uiSize   = 0;
	UINT                      uiSlices = 0;

	TBOOL IsCreated() const { return pTexture != TNULL; }
	TFLOAT GetMegabytes() const { return TFLOAT( double( uiSize ) * uiSize * uiSlices * 4 / 1000000.0 ); }
};

static ShadowMapArray                        s_oSunMaps;
static ShadowMapArray                        s_oLightMaps;
static ID3D11SamplerState*                   s_pComparisonSampler;
static TConstantBuffer<AEffectsSunShadowsCB> s_oSunCB;
static TUINT32                               s_uiFrame = TUINT32( -1 ); // of the last shadow pass
static TBOOL                                 s_bSunThisFrame;           // the receivers read the cascades
static TRenderContext*                       s_pMainContext;
static TVector3                              s_vSunDirection( 0.0f, -1.0f, 0.0f );
static TFLOAT                                s_fSunFade = 1.0f;
static TINT                                  s_iLogCountdown;

static void DestroyMapArray( ShadowMapArray& a_rMaps )
{
	auto pRender = TRenderD3DInterface::Interface();

	if ( a_rMaps.pSRV )
	{
		pRender->InvalidateShaderResource( a_rMaps.pSRV );
		a_rMaps.pSRV->Release();
	}

	for ( UINT i = 0; i < TARRAYSIZE( a_rMaps.apDSV ); i++ )
	{
		if ( a_rMaps.apDSV[ i ] )
		{
			if ( pRender->GetDesiredState().pDepthStencilView == a_rMaps.apDSV[ i ] )
				pRender->SetRenderTargets( pRender->GetRenderTargetView(), pRender->GetDepthStencilView() );

			a_rMaps.apDSV[ i ]->Release();
		}
	}

	if ( a_rMaps.pTexture )
		a_rMaps.pTexture->Release();

	a_rMaps = ShadowMapArray();

	// A new view can get the address of a released one that's still in the state cache
	pRender->InvalidateStateCache();
}

// D32 texture array, a depth view per slice and a shader view of all of them
static TBOOL CreateMapArray( ShadowMapArray& a_rMaps, const TCHAR* a_szName, UINT a_uiSize, UINT a_uiSlices )
{
	if ( a_rMaps.IsCreated() && a_rMaps.uiSize == a_uiSize && a_rMaps.uiSlices == a_uiSlices )
		return TTRUE;

	DestroyMapArray( a_rMaps );
	TASSERT( a_uiSlices <= TARRAYSIZE( a_rMaps.apDSV ) );

	ID3D11Device* pDevice = TRenderD3DInterface::Interface()->GetDevice();

	D3D11_TEXTURE2D_DESC oDesc = {};
	oDesc.Width                = a_uiSize;
	oDesc.Height               = a_uiSize;
	oDesc.MipLevels            = 1;
	oDesc.ArraySize            = a_uiSlices;
	oDesc.Format               = DXGI_FORMAT_R32_TYPELESS;
	oDesc.SampleDesc.Count     = 1;
	oDesc.Usage                = D3D11_USAGE_DEFAULT;
	oDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	TBOOL bOK = SUCCEEDED( pDevice->CreateTexture2D( &oDesc, TNULL, &a_rMaps.pTexture ) );

	for ( UINT i = 0; bOK && i < a_uiSlices; i++ )
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC oDSVDesc  = {};
		oDSVDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
		oDSVDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		oDSVDesc.Texture2DArray.FirstArraySlice = i;
		oDSVDesc.Texture2DArray.ArraySize       = 1;

		bOK = SUCCEEDED( pDevice->CreateDepthStencilView( a_rMaps.pTexture, &oDSVDesc, &a_rMaps.apDSV[ i ] ) );
	}

	if ( bOK )
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC oSRVDesc = {};
		oSRVDesc.Format                          = DXGI_FORMAT_R32_FLOAT;
		oSRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		oSRVDesc.Texture2DArray.MipLevels        = 1;
		oSRVDesc.Texture2DArray.ArraySize        = a_uiSlices;

		bOK = SUCCEEDED( pDevice->CreateShaderResourceView( a_rMaps.pTexture, &oSRVDesc, &a_rMaps.pSRV ) );
	}

	if ( !bOK )
	{
		TERROR( "[shadows] couldn't create the %s shadow maps (%u x %u^2)\n", a_szName, a_uiSlices, a_uiSize );
		DestroyMapArray( a_rMaps );
		return TFALSE;
	}

	a_rMaps.uiSize   = a_uiSize;
	a_rMaps.uiSlices = a_uiSlices;

	TINFO( "[shadows] %s shadow maps %u x %u^2 D32: %.1f MB\n", a_szName, a_uiSlices, a_uiSize, a_rMaps.GetMegabytes() );
	return TTRUE;
}

static TBOOL CreateSharedObjects()
{
	if ( !s_pComparisonSampler )
	{
		D3D11_SAMPLER_DESC oDesc = {};
		oDesc.Filter             = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		oDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
		oDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
		oDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
		oDesc.MaxAnisotropy      = 1;
		oDesc.ComparisonFunc     = D3D11_COMPARISON_LESS_EQUAL;
		oDesc.MinLOD             = 0.0f;
		oDesc.MaxLOD             = D3D11_FLOAT32_MAX;

		if ( FAILED( TRenderD3DInterface::Interface()->GetDevice()->CreateSamplerState( &oDesc, &s_pComparisonSampler ) ) )
			return TFALSE;
	}

	if ( !s_oSunCB.IsCreated() && !s_oSunCB.Create() )
		return TFALSE;

	return TTRUE;
}

//-----------------------------------------------------------------------------
// The casters: the calls of the main pass (ARenderer::RenderMainScene) from the light camera
//-----------------------------------------------------------------------------

// The projection parameters for which TRenderContext::ComputeOrthographicProjection gives a_rcProjection
// (with the viewport of a_pContext), so the frustum the game culls with is the box of the cascade
static TRenderContext::PROJECTIONPARAMS GetOrthographicParams( TRenderContext* a_pContext, const TMatrix44& a_rcProjection )
{
	const TFLOAT fWidth  = a_pContext->GetWidth();
	const TFLOAT fHeight = a_pContext->GetHeight();

	TRenderContext::PROJECTIONPARAMS oParams = a_pContext->GetProjectionParams();
	oParams.m_Proj.x                         = TMath::Max( a_rcProjection.m_f11 * fWidth * 0.5f, 0.0001f );
	oParams.m_Proj.y                         = TMath::Max( -a_rcProjection.m_f22 * fHeight * 0.5f, 0.0001f );
	oParams.m_Centre.x                       = ( a_rcProjection.m_f41 + 1.0f ) * fWidth * 0.5f;
	oParams.m_Centre.y                       = ( 1.0f - a_rcProjection.m_f42 ) * fHeight * 0.5f;
	oParams.m_fNearClip                      = -a_rcProjection.m_f43 / a_rcProjection.m_f33;
	oParams.m_fFarClip                       = oParams.m_fNearClip + 1.0f / a_rcProjection.m_f33;
	return oParams;
}

// The model instances outside the view whose bounding sphere is in the light frustum (the current render context):
// AModel::Update culls them with the main camera and AModel::Render skips them, so they wouldn't cast into the view.
// The hidden units (no skeleton update, a zero scale) and the glowing instances stay out, as in the main pass
static TINT s_iOffscreenCasters; // drawn in this frame

static void RenderOffscreenModels( AModel* a_pModel )
{
	TRenderContext* pContext = TRenderInterface::GetSingleton()->GetCurrentContext();
	auto&           rInstances = a_pModel->GetInstances();

	for ( TINT i = 0; i < rInstances.Size(); i++ )
	{
		AModelInstance* pInstance = rInstances[ i ].Get();

		if ( !pInstance || pInstance->IsVisible() || !pInstance->IsUpdatingSkeleton() || pInstance->IsGlow() )
			continue;

		TSceneObject*   pSceneObject = pInstance->GetSceneObject();
		TModel*         pModel       = pSceneObject->GetInstance()->GetModel();
		auto&           rTransform   = pSceneObject->GetTransform();
		const TVector4& rcScale      = rTransform.GetScale();
		auto&           rLOD         = pModel->GetLOD( 0 );

		TVector4 vSphere;
		vSphere.w = rLOD.BoundingSphere.GetRadius() * TMath::Max( TMath::Max( rcScale.x, rcScale.y ), rcScale.z );

		if ( vSphere.w <= 0.0f )
			continue;

		TMatrix44 oTransform;
		rTransform.GetLocalMatrixImp( oTransform );
		TMatrix44::TransformVector( vSphere.AsVector3(), oTransform, rLOD.BoundingSphere.GetOrigin() );

		if ( !TRenderContext::CullSphereToFrustumSimple( vSphere, pContext->GetWorldPlanes(), 6 ) )
			continue;

		pSceneObject->Render( pContext->GetClipFlags(), vSphere.AsVector3() );
		s_iOffscreenCasters++;
	}
}

// Draws the casters into the bound depth target. a_rcLightView is the world to light view matrix (a camera
// at the light), the context projection becomes a_rcProjParams in a_eCameraMode. a_pCameraObject decides the
// distance based culling and the LODs of the game, the main camera keeps them as in the main pass.
// Returns the projection the casters were drawn with (the one of the render context)
static TMatrix44 RenderCasters( TRenderContext* a_pContext, TCameraObject* a_pCameraObject, const TMatrix44& a_rcLightView, const TRenderContext::PROJECTIONPARAMS& a_rcProjParams, TRenderContext::CameraMode a_eCameraMode )
{
	auto  pRender     = TRenderD3DInterface::Interface();
	auto& rTransforms = pRender->GetTransforms();

	const TMatrix44                        oOldWorldView  = a_pContext->GetWorldViewMatrix();
	const TMatrix44                        oOldModelView  = a_pContext->GetModelViewMatrix();
	const TRenderContext::PROJECTIONPARAMS oOldProjParams = a_pContext->GetProjectionParams();
	const TRenderContext::CameraMode       eOldMode       = a_pContext->GetCameraMode();
	TCameraObject*                         pOldCamera     = a_pContext->GetCameraObject();

	// A spot light keeps its own far plane (TRenderContextD3D::Update replaces it with this one otherwise)
	const TFLOAT fOldFarClip                = TRenderContext::ms_fPerspectiveFarClip;
	TRenderContext::ms_fPerspectiveFarClip = 0.0f;

	a_pContext->SetProjectionParams( a_rcProjParams );
	a_pContext->SetCameraMode( a_eCameraMode );
	a_pContext->SetWorldViewMatrix( a_rcLightView );
	a_pContext->SetModelViewMatrix( a_rcLightView );
	a_pContext->Update();
	a_pContext->SetCameraObject( a_pCameraObject );

	const TMatrix44 oProjection = TRenderContextD3D::Upcast( a_pContext )->GetProjectionMatrix();

	AEffectsShadowPass::ms_mLightProjection = oProjection;
	AEffectsShadowPass::ms_bActive          = TTRUE;

	rTransforms.Push( a_rcLightView );

	// Model instances without the glow flag, the ones in the view and the ones outside it
	g_pModelRepos->RenderModels( AModelRepos::RENDERMASK_NORMAL );

	if ( AEffectsSettings::ms_iShadowOffscreen )
		g_pModelRepos->ForEachModel( RenderOffscreenModels );

	// Terrain, gates, trees, instances and regrowth
	if ( ATerrainInterface::GetSingleton() )
	{
		ATerrainInterface::GetSingleton()->Render();

		// A box of a cascade can hold more of the terrain than the view: the packets (TOrderTable::CreateStaticData) go now
		pRender->FlushShaders();

		if ( AGateManager::IsSingletonCreated() )
			AGateManager::GetSingleton()->Render();

		ATreeManager::GetSingleton()->Render();
		AInstanceManager::GetSingleton()->Render();
		ARegrowthManager::GetSingleton()->Render();
	}

	pRender->FlushShaders();

	rTransforms.Pop();
	AEffectsShadowPass::ms_bActive = TFALSE;

	TRenderContext::ms_fPerspectiveFarClip = fOldFarClip;

	a_pContext->SetProjectionParams( oOldProjParams );
	a_pContext->SetCameraMode( eOldMode );
	a_pContext->SetWorldViewMatrix( oOldWorldView );
	a_pContext->SetModelViewMatrix( oOldModelView );
	a_pContext->SetCameraObject( pOldCamera );
	a_pContext->Update();

	return oProjection;
}

// Binds a depth target of a_uiSize^2, clears it and sets the states of the casters
static void BeginShadowTarget( ID3D11DepthStencilView* a_pDSV, UINT a_uiSize, INT a_iDepthBias, TFLOAT a_fSlopeBias )
{
	auto pRender = TRenderD3DInterface::Interface();

	pRender->SetRenderTargets( TNULL, a_pDSV );
	pRender->ApplyDesiredState();
	pRender->GetDeviceContext()->ClearDepthStencilView( a_pDSV, D3D11_CLEAR_DEPTH, 1.0f, 0 );

	D3D11_VIEWPORT oViewport = { 0.0f, 0.0f, TFLOAT( a_uiSize ), TFLOAT( a_uiSize ), 0.0f, 1.0f };
	pRender->GetDeviceContext()->RSSetViewports( 1, &oViewport );

	pRender->SetZEnable( TTRUE );
	pRender->SetZWriteEnable( TTRUE );
	pRender->SetZFunc( D3D11_COMPARISON_LESS_EQUAL );
	pRender->SetAlphaBlendEnable( TFALSE );
	pRender->SetCullMode( D3D11_CULL_NONE );
	pRender->SetScissorEnable( TFALSE );
	pRender->SetDepthBias( a_iDepthBias, a_fSlopeBias );
}

//-----------------------------------------------------------------------------
// The sun
//-----------------------------------------------------------------------------
// The time of the day: the directional light of AGameTimeFX stays at night (the moon, about as bright as
// the sun), so the sun shadows go down over the dusk and come up over the dawn of AGameTimeManager
static TFLOAT ComputeSunFade()
{
	if ( !AGameTimeManager::IsSingletonCreated() )
		return 1.0f;

	const AGameTimeManager*          pTime     = AGameTimeManager::GetSingleton();
	const AGameTimeManager::DAYPHASE eDayPhase = pTime->GetCurrentDayPhase();

	if ( eDayPhase < 0 || eDayPhase >= AGameTimeManager::DAYPHASE_NUMOF )
		return 1.0f;

	TFLOAT fLeft = pTime->GetCurrentPhaseTimeLeft() / AGameTimeManager::DAYPHASE_DURATION[ eDayPhase ];
	TMath::Clip( fLeft, 0.0f, 1.0f );

	switch ( eDayPhase )
	{
		case AGameTimeManager::DAYPHASE_DAWN: return 1.0f - fLeft;
		case AGameTimeManager::DAYPHASE_DUSK: return fLeft;
		case AGameTimeManager::DAYPHASE_NIGHT: return 0.0f;
		default: return 1.0f;
	}
}

// The direction the sunlight travels: the game's light direction points to the sun
static TVector3 ComputeSunDirection()
{
	TVector3 vDirection;

	if ( AEffectsSettings::ms_iSunDirOverride )
	{
		// ERRender's CSMManager::BuildLightView put the axes of the light into the rows of its view matrix, so
		// its shadows went along the third column: ( right.z, up.z, dir.z ) = ( -0.156, 0.648, -0.745 ) for its
		// azimuth and elevation, downwards in the Y down world of the game
		const TFLOAT fAzimuth   = TMath::DegToRad( CSM_SUN_AZIMUTH );
		const TFLOAT fElevation = TMath::DegToRad( CSM_SUN_ELEVATION );

		const TVector3 vRight( cosf( fAzimuth ), 0.0f, -sinf( fAzimuth ) );
		const TVector3 vDir( cosf( fElevation ) * sinf( fAzimuth ), -sinf( fElevation ), cosf( fElevation ) * cosf( fAzimuth ) );

		TVector3 vUp;
		vUp.CrossProduct( vDir, vRight );

		vDirection.Set( vRight.z, vUp.z, vDir.z );
	}
	else
	{
		const TVector3& rcToSun = TRenderInterface::GetSingleton()->GetLightDirection().AsBasisVector3( 0 );
		vDirection.Set( -rcToSun.x, -rcToSun.y, -rcToSun.z );
	}

	if ( vDirection.MagnitudeSq() < 0.0001f )
		vDirection = TVector3( 0.0f, -1.0f, 0.0f );

	vDirection.Normalize();
	return vDirection;
}

// The world to light view matrix of a camera at a_rcOrigin looking along a_rcDirection
static void BuildLightView( TMatrix44& a_rOutView, const TVector3& a_rcDirection, const TVector3& a_rcOrigin )
{
	TVector3 vUpHint = ( TMath::Abs( a_rcDirection.y ) < 0.99f ) ? TVector3( 0.0f, 1.0f, 0.0f ) : TVector3( 1.0f, 0.0f, 0.0f );

	TVector3 vRight;
	vRight.CrossProduct( vUpHint, a_rcDirection );
	vRight.Normalize();

	TVector3 vUp;
	vUp.CrossProduct( a_rcDirection, vRight );
	vUp.Normalize();

	TMatrix44 oLightToWorld                             = TMatrix44::IDENTITY;
	oLightToWorld.AsBasisVector3( BASISVECTOR_RIGHT )   = vRight;
	oLightToWorld.AsBasisVector3( BASISVECTOR_UP )      = vUp;
	oLightToWorld.AsBasisVector3( BASISVECTOR_FORWARD ) = a_rcDirection;
	oLightToWorld.GetTranslation3()                     = a_rcOrigin;

	a_rOutView.InvertOrthogonal( oLightToWorld );
}

// ERRender's CSMManager::BuildOrthoMatrix: x, y to -1..1 (y flipped), z to 0..1
static void BuildOrthoMatrix( TMatrix44& a_rOutMatrix, TFLOAT a_fMinX, TFLOAT a_fMaxX, TFLOAT a_fMinY, TFLOAT a_fMaxY, TFLOAT a_fMinZ, TFLOAT a_fMaxZ )
{
	a_rOutMatrix       = TMatrix44::IDENTITY;
	a_rOutMatrix.m_f11 = 2.0f / ( a_fMaxX - a_fMinX );
	a_rOutMatrix.m_f22 = 2.0f / ( a_fMinY - a_fMaxY );
	a_rOutMatrix.m_f33 = 1.0f / ( a_fMaxZ - a_fMinZ );
	a_rOutMatrix.m_f41 = -( a_fMinX + a_fMaxX ) / ( a_fMaxX - a_fMinX );
	a_rOutMatrix.m_f42 = ( a_fMinY + a_fMaxY ) / ( a_fMaxY - a_fMinY );
	a_rOutMatrix.m_f43 = -a_fMinZ / ( a_fMaxZ - a_fMinZ );
}

// ERRender's CSMManager::BuildCascade: a sphere around the slice of the camera frustum between a_fNear and
// a_fFar, its box in light space snapped to texels (in world space, so the shadows don't crawl as the
// camera moves), reaching back towards the sun for the casters out of view
static void BuildCascade( TMatrix44& a_rOutProjection, TRenderContext* a_pMainContext, const TMatrix44& a_rcLightView, const TVector3& a_rcLightOrigin, TFLOAT a_fNear, TFLOAT a_fFar, UINT a_uiSize )
{
	const TRenderContext::PROJECTIONPARAMS& rcProj     = a_pMainContext->GetProjectionParams();
	const TRenderContext::VIEWPORTPARAMS&   rcViewport = a_pMainContext->GetViewportParameters();
	const TMatrix44&                        rcViewWorld = a_pMainContext->GetViewWorldMatrix();

	// The corners of the view frustum at depth 1 (TRenderContext::ComputePerspectiveFrustum)
	const TFLOAT fLeft   = -rcProj.m_Centre.x / rcProj.m_Proj.x;
	const TFLOAT fRight  = ( rcViewport.fWidth - rcProj.m_Centre.x ) / rcProj.m_Proj.x;
	const TFLOAT fTop    = ( rcViewport.fHeight - rcProj.m_Centre.y ) / rcProj.m_Proj.y;
	const TFLOAT fBottom = -rcProj.m_Centre.y / rcProj.m_Proj.y;

	TVector3 aCorners[ 8 ];
	TVector3 vCentre( 0.0f, 0.0f, 0.0f );

	for ( TINT i = 0; i < 8; i++ )
	{
		const TFLOAT fZ = ( i < 4 ) ? a_fNear : a_fFar;
		TVector3     vView( ( ( i & 1 ) ? fRight : fLeft ) * fZ, ( ( i & 2 ) ? fTop : fBottom ) * fZ, fZ );

		TMatrix44::TransformVector( aCorners[ i ], rcViewWorld, vView );
		vCentre += aCorners[ i ];
	}

	vCentre.Divide( 8.0f );

	TFLOAT fRadius = 0.0f;
	TFLOAT fMinZ   = FLT_MAX;
	TFLOAT fMaxZ   = -FLT_MAX;

	for ( TINT i = 0; i < 8; i++ )
	{
		TVector3 vLight;
		TMatrix44::TransformVector( vLight, a_rcLightView, aCorners[ i ] );

		fRadius = TMath::Max( fRadius, TVector3::Distance( vCentre, aCorners[ i ] ) );
		fMinZ   = TMath::Min( fMinZ, vLight.z );
		fMaxZ   = TMath::Max( fMaxZ, vLight.z );
	}

	// Whole units of radius: the texel size stays the same while the camera turns
	fRadius = ceilf( fRadius + CSM_CASCADE_PADDING );

	TVector3 vLightCentre;
	TMatrix44::TransformVector( vLightCentre, a_rcLightView, vCentre );

	// The light view starts at a_rcLightOrigin: snap in the light space of the world origin
	const TFLOAT fTexelSize = ( 2.0f * fRadius ) / TFLOAT( a_uiSize );
	const TFLOAT fOriginX   = TVector3::DotProduct( a_rcLightOrigin, TVector3( a_rcLightView.m_f11, a_rcLightView.m_f21, a_rcLightView.m_f31 ) );
	const TFLOAT fOriginY   = TVector3::DotProduct( a_rcLightOrigin, TVector3( a_rcLightView.m_f12, a_rcLightView.m_f22, a_rcLightView.m_f32 ) );

	TFLOAT fMinX = floorf( ( vLightCentre.x - fRadius + fOriginX ) / fTexelSize ) * fTexelSize - fOriginX;
	TFLOAT fMinY = floorf( ( vLightCentre.y - fRadius + fOriginY ) / fTexelSize ) * fTexelSize - fOriginY;
	TFLOAT fMaxX = fMinX + TFLOAT( a_uiSize ) * fTexelSize;
	TFLOAT fMaxY = fMinY + TFLOAT( a_uiSize ) * fTexelSize;

	fMinZ -= fRadius * 2.0f + CSM_CASTER_PADDING;

	BuildOrthoMatrix( a_rOutProjection, fMinX, fMaxX, fMinY, fMaxY, fMinZ, fMaxZ );
}

static UINT GetSunMapSize()
{
	switch ( AEffectsSettings::ms_iSunShadows )
	{
		case 1: return 1024;
		case 2: return 2048;
		case 3: return 4096;
		default: return 0;
	}
}

static TBOOL RenderSunShadows( TViewport* a_pViewport, TCameraObject* a_pCameraObject )
{
	const UINT uiSize = GetSunMapSize();

	if ( uiSize == 0 )
	{
		// Off: the memory goes back
		if ( s_oSunMaps.IsCreated() )
			DestroyMapArray( s_oSunMaps );

		return TFALSE;
	}

	s_fSunFade      = ComputeSunFade();
	s_vSunDirection = ComputeSunDirection();

	if ( s_iLogCountdown-- <= 0 && AEffectsSettings::ms_iFXTimings )
	{
		const TVector4& rcLight = TRenderInterface::GetSingleton()->GetLightColour().AsBasisVector4( 0 );
		TINFO( "[shadows] sun: direction %.3f %.3f %.3f, light colour %.3f %.3f %.3f, fade %.2f\n", s_vSunDirection.x, s_vSunDirection.y, s_vSunDirection.z, rcLight.x, rcLight.y, rcLight.z, s_fSunFade );
		s_iLogCountdown = 240;
	}

	// Under 2% of darkness (the end of the dusk, the start of the dawn) they can't be seen: no pass
	if ( s_fSunFade * AEffectsSettings::ms_fSunShadowIntensity < 0.02f )
		return TFALSE;

	// Inside the barn (ABarnExploreMinigameState) the roof would shadow everything
	if ( AInstanceManager::IsSingletonCreated() && AInstanceManager::GetSingleton()->IsInteriorMode() )
		return TFALSE;

	if ( !CreateSharedObjects() || !CreateMapArray( s_oSunMaps, "sun", uiSize, NUM_CASCADES ) )
		return TFALSE;

	TRenderContext* pContext = a_pViewport->GetRenderContext();

	// The cascades: ERRender's CSMManager::UpdateCascades
	const TRenderContext::PROJECTIONPARAMS& rcProj = pContext->GetProjectionParams();

	const TFLOAT fNear  = TMath::Max( rcProj.m_fNearClip, 0.1f );
	const TFLOAT fFar   = TMath::Max( TMath::Min( rcProj.m_fFarClip, AEffectsSettings::ms_fSunShadowDistance ), fNear + 1.0f );
	const TFLOAT fRange = fFar - fNear;
	const TFLOAT fRatio = fFar / fNear;

	TFLOAT afSplits[ NUM_CASCADES ];

	for ( TINT i = 0; i < NUM_CASCADES; i++ )
	{
		const TFLOAT fProgress = TFLOAT( i + 1 ) / TFLOAT( NUM_CASCADES );
		const TFLOAT fLog      = fNear * powf( fRatio, fProgress );
		const TFLOAT fUniform  = fNear + fRange * fProgress;
		afSplits[ i ]          = CSM_SPLIT_LAMBDA * ( fLog - fUniform ) + fUniform;
	}

	// The light camera stands at the main camera (the game culls trees and fades instances by the distance to it)
	const TVector3 vCameraPos = pContext->GetViewWorldMatrix().GetTranslation3();

	TMatrix44 oLightView;
	BuildLightView( oLightView, s_vSunDirection, vCameraPos );

	AEffectsSunShadowsCB& rCB = s_oSunCB.Edit();

	for ( TINT i = 0; i < NUM_CASCADES; i++ )
	{
		TMatrix44 oCascadeProj;
		BuildCascade( oCascadeProj, pContext, oLightView, vCameraPos, ( i == 0 ) ? fNear : afSplits[ i - 1 ], afSplits[ i ], uiSize );

		// Slope bias falls with the size of the texels
		const TFLOAT fSlopeBias = TMath::Max( CSM_DEPTH_BIAS_SLOPE / powf( 4.0f, TFLOAT( i ) ), CSM_MIN_SLOPE_DEPTH_BIAS );
		BeginShadowTarget( s_oSunMaps.apDSV[ i ], uiSize, CSM_DEPTH_BIAS_UNITS, fSlopeBias );

		AEffectsShadowPass::ms_iSlice = i;
		const TMatrix44 oProjection   = RenderCasters( pContext, a_pCameraObject, oLightView, GetOrthographicParams( pContext, oCascadeProj ), TRenderContext::CameraMode_Orthographic );

		TMatrix44 oViewProjection;
		oViewProjection.Multiply( oProjection, oLightView );
		rCB.amLightViewProjection[ i ].Transpose( oViewProjection );
	}

	rCB.vCascadeSplits      = TVector4( afSplits[ 0 ], afSplits[ 1 ], afSplits[ 2 ], 0.0f );
	TFLOAT fIntensity = AEffectsSettings::ms_fSunShadowIntensity;
	TMath::Clip( fIntensity, 0.0f, 1.0f );

	rCB.vShadowParams       = TVector4( CSM_RECEIVER_BIAS, 1.0f / TFLOAT( uiSize ), 0.0f, fIntensity * s_fSunFade );
	rCB.vShadowFilterParams = TVector4( CSM_PCF_RADIUS, CSM_RECEIVER_PLANE_BIAS, 0.0f, CSM_CASCADE_BLEND );

	return TTRUE;
}

//-----------------------------------------------------------------------------
// The spot lights
//-----------------------------------------------------------------------------

// Projection parameters giving the same projection with the viewport a_rcTo as a_rcParams with a_rcFrom (the
// glow objects have a 1 x 1 viewport, the render context of the casters keeps the main one)
static TRenderContext::PROJECTIONPARAMS ScaleProjectionParams( const TRenderContext::PROJECTIONPARAMS& a_rcParams, const TRenderContext::VIEWPORTPARAMS& a_rcFrom, const TRenderContext::VIEWPORTPARAMS& a_rcTo )
{
	const TFLOAT fScaleX = a_rcTo.fWidth / TMath::Max( a_rcFrom.fWidth, 0.0001f );
	const TFLOAT fScaleY = a_rcTo.fHeight / TMath::Max( a_rcFrom.fHeight, 0.0001f );

	TRenderContext::PROJECTIONPARAMS oParams = a_rcParams;
	oParams.m_Proj.x *= fScaleX;
	oParams.m_Proj.y *= fScaleY;
	oParams.m_Centre.x *= fScaleX;
	oParams.m_Centre.y *= fScaleY;
	return oParams;
}

static UINT GetLightMapSize()
{
	switch ( AEffectsSettings::ms_iDynamicLightShadows )
	{
		case 1: return 512;
		case 2: return 1024;
		default: return 0;
	}
}

// The shadow maps of the lights nearest to the camera (ERRender's RenderDynamicGlowShadowMaps, the night only
// lights are left out by day by ADynamicLights::Update), TRUE if any was rendered
static TBOOL RenderLightShadows( TViewport* a_pViewport, TCameraObject* a_pCameraObject )
{
	const UINT uiSize     = GetLightMapSize();
	const TINT iNumLights = ADynamicLights::GetNumLights();

	if ( uiSize == 0 || !AEffectsSettings::ms_iDynamicLights )
	{
		// Off: the memory goes back
		if ( s_oLightMaps.IsCreated() )
			DestroyMapArray( s_oLightMaps );

		return TFALSE;
	}

	// The nearest first
	TINT aiOrder[ ADynamicLights::MAX_FRAME_LIGHTS ];

	for ( TINT i = 0; i < iNumLights; i++ )
	{
		TINT k = i;

		for ( ; k > 0 && ADynamicLights::GetLight( aiOrder[ k - 1 ] ).fDistanceSq > ADynamicLights::GetLight( i ).fDistanceSq; k-- )
			aiOrder[ k ] = aiOrder[ k - 1 ];

		aiOrder[ k ] = i;
	}

	TRenderContext* pContext = a_pViewport->GetRenderContext();
	TINT            iSlices  = 0;

	for ( TINT i = 0; i < iNumLights && iSlices < LIGHT_SHADOW_SLICES; i++ )
	{
		ADynamicLights::Light& rLight = ADynamicLights::GetLight( aiOrder[ i ] );

		if ( rLight.fDistanceSq > LIGHT_SHADOW_DISTANCE * LIGHT_SHADOW_DISTANCE )
			break;

		if ( !CreateSharedObjects() || !CreateMapArray( s_oLightMaps, "light", uiSize, LIGHT_SHADOW_SLICES ) )
			return TFALSE;

		BeginShadowTarget( s_oLightMaps.apDSV[ iSlices ], uiSize, CSM_DEPTH_BIAS_UNITS, LIGHT_DEPTH_BIAS_SLOPE );

		AEffectsShadowPass::ms_iSlice = iSlices;
		const TMatrix44 oProjection   = RenderCasters( pContext, a_pCameraObject, rLight.oWorldToLight, ScaleProjectionParams( rLight.oProjectionParams, rLight.oViewportParams, pContext->GetViewportParameters() ), rLight.eCameraMode );

		TMatrix44 oViewProjection;
		oViewProjection.Multiply( oProjection, rLight.oWorldToLight );
		ADynamicLights::SetShadow( aiOrder[ i ], iSlices, uiSize, oViewProjection );

		iSlices++;
	}

	return iSlices > 0;
}

//-----------------------------------------------------------------------------
// AShadowMaps
//-----------------------------------------------------------------------------
TBOOL AShadowMaps::IsSupported()
{
	return TRenderD3DInterface::Interface()->GetDevice()->GetFeatureLevel() >= D3D_FEATURE_LEVEL_10_1;
}

void AShadowMaps::Render( TViewport* a_pViewport, TCameraObject* a_pCameraObject )
{
	auto pRender = TRenderD3DInterface::Interface();

	if ( !IsSupported() )
	{
		static TBOOL s_bLogged = TFALSE;

		if ( !s_bLogged && ( AEffectsSettings::ms_iSunShadows || AEffectsSettings::ms_iDynamicLights ) )
		{
			TWARN( "[shadows] the sun shadows and the dynamic lights need feature level 10_1, they are off\n" );
			s_bLogged = TTRUE;
		}

		return;
	}

	s_pMainContext = a_pViewport->GetRenderContext();
	s_uiFrame      = g_oSystemManager.GetFrameCount();

	// The maps become depth targets: they can't stay bound for reading
	pRender->PSSetShaderResource( AEffectsSlots::T_SUN_SHADOW_MAP, TNULL );
	pRender->PSSetShaderResource( AEffectsSlots::T_LIGHT_SHADOW_MAPS, TNULL );
	pRender->SetPSConstantBuffer( AEffectsSlots::CB_SUN_SHADOWS, TNULL );
	pRender->SetPSConstantBuffer( AEffectsSlots::CB_DYNAMIC_LIGHTS, TNULL );

	// Both off: no pass and no other state change, the memory goes back
	if ( !AEffectsSettings::ms_iSunShadows && !AEffectsSettings::ms_iDynamicLights )
	{
		DestroyMapArray( s_oSunMaps );
		DestroyMapArray( s_oLightMaps );
		ADynamicLights::Update( TVector3( 0.0f, 0.0f, 0.0f ) ); // no lights in this frame
		s_bSunThisFrame               = TFALSE;
		AShadowManager::ms_bHideBlobs = TFALSE;
		return;
	}

	pRender->FlushStripBatch();

	TRenderD3DInterface::STATEBLOCK oStateBlock;
	pRender->CaptureStateBlock( oStateBlock );

	// The terrain sets the colours of the world shader for its lit cells (RenderCellMeshWin) and the flush uses
	// the last ones: the main pass starts from the colours it had before the casters
	AWorldShaderHAL* pWorldShader   = AWorldShader::IsSingletonCreated() ? TSTATICCAST( AWorldShaderHAL, AWorldShader::GetSingleton() ) : TNULL;
	const TVector4   vShadowColour  = pWorldShader ? pWorldShader->GetShadowColour() : TVector4::VEC_ZERO;
	const TVector4   vAmbientColour = pWorldShader ? pWorldShader->GetAmbientColour() : TVector4::VEC_ZERO;

	LARGE_INTEGER oTime0, oTime1, oTime2;
	QueryPerformanceCounter( &oTime0 );
	s_iOffscreenCasters = 0;

	s_bSunThisFrame = RenderSunShadows( a_pViewport, a_pCameraObject );
	QueryPerformanceCounter( &oTime1 );

	// The units cast their shadows by day: the blob shadows under them go (they stay at night)
	AShadowManager::ms_bHideBlobs = s_bSunThisFrame && s_fSunFade >= 0.5f;

	// The dynamic lights of the frame and the shadow maps of the nearest ones
	ADynamicLights::Update( s_pMainContext->GetViewWorldMatrix().GetTranslation3() );
	const TBOOL bLightShadows = RenderLightShadows( a_pViewport, a_pCameraObject );
	QueryPerformanceCounter( &oTime2 );

	// With render.fx_timings: the CPU time of the pass (the draw calls are submitted in it) every 120 frames
	if ( AEffectsSettings::ms_iFXTimings )
	{
		static LARGE_INTEGER s_oFrequency;
		static double        s_dSunMs, s_dLightsMs, s_dOffscreen;
		static TINT          s_iFrames;

		if ( !s_oFrequency.QuadPart )
			QueryPerformanceFrequency( &s_oFrequency );

		s_dSunMs += double( oTime1.QuadPart - oTime0.QuadPart ) * 1000.0 / double( s_oFrequency.QuadPart );
		s_dLightsMs += double( oTime2.QuadPart - oTime1.QuadPart ) * 1000.0 / double( s_oFrequency.QuadPart );
		s_dOffscreen += s_iOffscreenCasters;

		if ( ++s_iFrames >= 120 )
		{
			TINFO( "[shadows] CPU ms, average of %d frames: sun %.3f, lights %.3f; model casters outside the view %.1f\n", s_iFrames, s_dSunMs / s_iFrames, s_dLightsMs / s_iFrames, s_dOffscreen / s_iFrames );
			s_dSunMs = s_dLightsMs = s_dOffscreen = 0.0;
			s_iFrames = 0;
		}
	}

	AEffectsShadowPass::ms_iSlice = 0;
	pRender->ApplyStateBlock( oStateBlock );

	if ( pWorldShader )
		pWorldShader->SetColours( vShadowColour, vAmbientColour );

	// Bound until the next frame's shadow pass, for the receivers and the post passes
	if ( s_bSunThisFrame )
	{
		pRender->PSSetShaderResource( AEffectsSlots::T_SUN_SHADOW_MAP, s_oSunMaps.pSRV );
		pRender->PSSetSampler( AEffectsSlots::S_SUN_SHADOW, s_pComparisonSampler );
		pRender->SetPSConstantBuffer( AEffectsSlots::CB_SUN_SHADOWS, &s_oSunCB );
	}

	if ( ADynamicLights::GetNumLights() > 0 && CreateSharedObjects() )
	{
		pRender->SetPSConstantBuffer( AEffectsSlots::CB_DYNAMIC_LIGHTS, ADynamicLights::GetConstantBuffer() );
		pRender->PSSetSampler( AEffectsSlots::S_LIGHT_SHADOW, s_pComparisonSampler );

		if ( bLightShadows )
			pRender->PSSetShaderResource( AEffectsSlots::T_LIGHT_SHADOW_MAPS, s_oLightMaps.pSRV );
	}
}

void AShadowMaps::Destroy()
{
	DestroyMapArray( s_oSunMaps );
	DestroyMapArray( s_oLightMaps );
	ADynamicLights::Destroy();

	if ( s_oSunCB.IsCreated() )
		s_oSunCB.Destroy();

	if ( s_pComparisonSampler )
	{
		s_pComparisonSampler->Release();
		s_pComparisonSampler = TNULL;
	}

	s_uiFrame       = TUINT32( -1 );
	s_bSunThisFrame = TFALSE;
	s_pMainContext  = TNULL;

	AShadowManager::ms_bHideBlobs = TFALSE;
}

TBOOL AShadowMaps::IsReceiverPass()
{
	return s_uiFrame == g_oSystemManager.GetFrameCount() && AEffects::IsInFrame() && !AEffectsShadowPass::ms_bActive &&
	    TRenderInterface::GetSingleton()->GetCurrentContext() == s_pMainContext;
}

TBOOL AShadowMaps::IsSunShadowReceiver()
{
	return s_bSunThisFrame && IsReceiverPass();
}

TBOOL AShadowMaps::IsCoveragePass()
{
	return AEffectsSettings::ms_iAlphaCoverage && AEffects::GetSceneSampleCount() > 1 && IsReceiverPass();
}

const D3D_SHADER_MACRO* AShadowMaps::GetReceiverDefines( TUINT a_uiPermutation )
{
	static const D3D_SHADER_MACRO s_aDefines[ 8 ][ 4 ] = {
		{ { TNULL, TNULL } },
		{ { "SUN_SHADOWS", "1" }, { TNULL, TNULL } },
		{ { "DYNAMIC_LIGHTS", "1" }, { TNULL, TNULL } },
		{ { "SUN_SHADOWS", "1" }, { "DYNAMIC_LIGHTS", "1" }, { TNULL, TNULL } },
		{ { "ALPHA_COVERAGE", "1" }, { TNULL, TNULL } },
		{ { "SUN_SHADOWS", "1" }, { "ALPHA_COVERAGE", "1" }, { TNULL, TNULL } },
		{ { "DYNAMIC_LIGHTS", "1" }, { "ALPHA_COVERAGE", "1" }, { TNULL, TNULL } },
		{ { "SUN_SHADOWS", "1" }, { "DYNAMIC_LIGHTS", "1" }, { "ALPHA_COVERAGE", "1" }, { TNULL, TNULL } },
	};

	return s_aDefines[ a_uiPermutation & 7 ];
}

const TCHAR* AShadowMaps::GetReceiverShaderSource()
{
	return s_szReceiverSource;
}

TBOOL AShadowMaps::CreateShaders( const TCHAR* a_szSource, LPCSTR a_szVSEntry, LPCSTR a_szPSEntry, const D3D_SHADER_MACRO* a_pDefines, const D3D11_INPUT_ELEMENT_DESC* a_pLayout, UINT a_uiNumElements, ID3D11VertexShader** a_ppVS, ID3D11PixelShader** a_ppPS, ID3D11InputLayout** a_ppLayout )
{
	ReleaseShaders( a_ppVS, a_ppPS, a_ppLayout );

	if ( !IsSupported() )
		return TFALSE;

	TString8 strSource = s_szReceiverSource;
	strSource += a_szSource;

	// ps_4_1: the comparison samples of the texture arrays
	ID3DBlob* pVSBlob = TRenderD3DInterface::CompileShader( strSource.GetString(), a_szVSEntry, "vs_4_1", a_pDefines );
	ID3DBlob* pPSBlob = a_szPSEntry ? TRenderD3DInterface::CompileShader( strSource.GetString(), a_szPSEntry, "ps_4_1", a_pDefines ) : TNULL;

	TBOOL bOK = pVSBlob && ( pPSBlob || !a_szPSEntry ) &&
	    TRenderD3DInterface::CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), a_ppVS ) &&
	    ( !a_szPSEntry || TRenderD3DInterface::CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), a_ppPS ) ) &&
	    SUCCEEDED( TRenderD3DInterface::Interface()->GetDevice()->CreateInputLayout( a_pLayout, a_uiNumElements, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), a_ppLayout ) );

	if ( pVSBlob ) pVSBlob->Release();
	if ( pPSBlob ) pPSBlob->Release();

	if ( !bOK )
	{
		TERROR( "[shadows] the shaders %s / %s (%s) don't compile, the effect is off for them\n", a_szVSEntry, a_szPSEntry ? a_szPSEntry : "-", ( a_pDefines && a_pDefines[ 0 ].Name ) ? a_pDefines[ 0 ].Name : "no defines" );
		ReleaseShaders( a_ppVS, a_ppPS, a_ppLayout );
	}

	return bOK;
}

void AShadowMaps::ReleaseShaders( ID3D11VertexShader** a_ppVS, ID3D11PixelShader** a_ppPS, ID3D11InputLayout** a_ppLayout )
{
	if ( a_ppVS && *a_ppVS )
	{
		( *a_ppVS )->Release();
		*a_ppVS = TNULL;
	}

	if ( a_ppPS && *a_ppPS )
	{
		( *a_ppPS )->Release();
		*a_ppPS = TNULL;
	}

	if ( a_ppLayout && *a_ppLayout )
	{
		( *a_ppLayout )->Release();
		*a_ppLayout = TNULL;
	}
}

ID3D11ShaderResourceView* AShadowMaps::GetSunShadowSRV()
{
	return ( s_bSunThisFrame && s_uiFrame == g_oSystemManager.GetFrameCount() ) ? s_oSunMaps.pSRV : TNULL;
}

ID3D11SamplerState* AShadowMaps::GetSunShadowSampler()
{
	return s_pComparisonSampler;
}

const AEffectsSunShadowsCB& AShadowMaps::GetSunShadowConstants()
{
	static const AEffectsSunShadowsCB s_oNone = {};
	return s_oSunCB.IsCreated() ? s_oSunCB.Get() : s_oNone;
}

const TVector3& AShadowMaps::GetSunDirection()
{
	return s_vSunDirection;
}

TFLOAT AShadowMaps::GetSunFade()
{
	return s_fSunFade;
}
