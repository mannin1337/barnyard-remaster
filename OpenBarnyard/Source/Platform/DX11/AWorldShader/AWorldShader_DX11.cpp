#include "pch.h"
#include "AWorldShader_DX11.h"
#include "AWorldMaterial_DX11.h"
#include "AWorldMesh_DX11.h"
#include "World/AWorld.h"

#include <Render/TRenderPacket.h>

#include <Platform/DX11/TRenderInterface_DX11.h>
#include <Platform/DX11/TRenderContext_DX11.h>
#include <Platform/DX11/TVertexBlockResource_DX11.h>
#include <Platform/DX11/TVertexPoolResource_DX11.h>
#include <Platform/DX11/TIndexBlockResource_DX11.h>
#include <Platform/DX11/TIndexPoolResource_DX11.h>

#ifdef BARNYARD_COMMUNITY_PATCH
#  include "Platform/DX11/Effects/AEffects_DX11.h"
#  include "Platform/DX11/Effects/AShadowMaps_DX11.h"
#  include "Platform/DX11/Effects/ADynamicLights_DX11.h"
#  include <Platform/DX11/TTextureResourceHAL_DX11.h>
#  include <Toshi/TScheduler.h>
#endif

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( AWorldShaderHAL );

#ifdef BARNYARD_COMMUNITY_PATCH
static TFLOAT GetFoliageWind( const AWorldMaterialHAL* a_pMaterial )
{
	auto pTexture = TDYNAMICCAST( TTextureResourceHAL, a_pMaterial->GetTexture( 0 ) );
	return pTexture && pTexture->GetSurfaceDetail() == TTextureResourceHAL::SurfaceDetail::Foliage ? 0.10f : 0.0f;
}
#endif

// AWorldShader_D3D8_Win.vsh and the texture stages of the world materials.
// BARNYARD_COMMUNITY_PATCH: SUN_SHADOWS receives the sun shadows (AShadowMaps): the shadow scales the vertex
// lighting, a shadowed pixel gets the shadow colour like the baked shadows. DYNAMIC_LIGHTS adds the lights of
// the packet (ADynamicLights). vs_depth / ps_depth draw the casters into the shadow maps (AEffectsShadowPass)
static const TCHAR s_szWorldShader[] = R"(
cbuffer Constants : register( b0 )
{
	float4x4 g_mModelViewProjection;
	float4   g_vAmbientColour;
	float4   g_vShadowColour;
	float4   g_vOne;
	float4   g_vUVOffsets;    // texture 0 (xy), texture 1 (zw)
	float4   g_vAlphaWeights; // weight of the lighting (x) and of the opaque alpha (y)
	float4   g_vAlphaTest;
	float4   g_vFog;          // enabled, start, end
	float4   g_vFogColour;
	float4   g_vStages;       // texture of stage 0, texture of stage 1, foliage wind strength, wind time
	float4x4 g_mModel;        // c13: model to world
	float4   g_vFoliageSun;  // c17: direction of sunlight and daylight factor
};

struct VS_IN
{
	float3 vPosition : POSITION;
	float3 vNormal : NORMAL;
	float3 vLighting : COLOR;
	float2 vUV : TEXCOORD;
};

struct PS_IN
{
	float4 vPosition : SV_POSITION;
	float4 vColour : COLOR;
	float2 vUV0 : TEXCOORD0;
	float2 vUV1 : TEXCOORD1;
#if defined( SUN_SHADOWS ) || defined( DYNAMIC_LIGHTS )
	float3 vWorldPos : TEXCOORD2;
	float3 vLighting : TEXCOORD3;
	float3 vWorldNormal : TEXCOORD4;
#endif
};

float3 GetFoliagePosition( float3 vPosition )
{
	[branch]
	if ( g_vStages.z > 0.0f )
	{
		float fPhase = dot( vPosition, float3( 0.16f, 0.25f, 0.22f ) );
		float fGust = sin( fPhase - g_vStages.w * 1.35f );
		float fFlutter = sin( fPhase * 2.7f + vPosition.x * 0.35f - g_vStages.w * 2.8f );
		vPosition += g_vStages.z * float3( fGust * 0.72f + fFlutter * 0.18f, fFlutter * 0.12f, fGust * 0.40f - fFlutter * 0.25f );
	}
	return vPosition;
}

PS_IN vs_main( VS_IN In )
{
	float4 vLighting = float4( In.vLighting, 1.0f );
	float4 vColour   = vLighting * g_vAmbientColour + ( g_vOne - vLighting ) * g_vShadowColour;
	vColour.w        = g_vOne.w * g_vAlphaWeights.y + vLighting.x * g_vAlphaWeights.x;

	float4 vPosition = float4( GetFoliagePosition( In.vPosition ), 1.0f );
	PS_IN Out;
	Out.vPosition = mul( vPosition, g_mModelViewProjection );
	Out.vColour   = saturate( vColour );
	Out.vUV0      = g_vUVOffsets.xy + In.vUV;
	Out.vUV1      = g_vUVOffsets.zw + In.vUV;
#if defined( SUN_SHADOWS ) || defined( DYNAMIC_LIGHTS )
	Out.vWorldPos    = mul( vPosition, g_mModel ).xyz;
	Out.vLighting    = In.vLighting;
	Out.vWorldNormal = mul( In.vNormal, (float3x3)g_mModel );
#endif
	return Out;
}

Texture2D    g_Texture0 : register( t0 );
Texture2D    g_Texture1 : register( t1 );
SamplerState g_Sampler0 : register( s0 );
SamplerState g_Sampler1 : register( s1 );

float4 ps_main( PS_IN In
#ifdef ALPHA_COVERAGE
	, out uint a_uiCoverage : SV_Coverage
#endif
	) : SV_TARGET
{
	float4 vColour = In.vColour;

#ifdef DYNAMIC_LIGHTS
	// The gradients first, before the alpha test can discard
	float3 vGlow = GetDynamicLights( In.vWorldPos, ComputeDerivedWorldNormal( In.vWorldPos, In.vWorldNormal, In.vUV0, g_Texture0, g_Sampler0, g_avLightIntensity[ 0 ].y ) );
#endif

#ifdef SUN_SHADOWS
	// The shadowed part of the light goes like the baked lighting (the colour of the vertex is lerp( shadow, ambient, lighting ))
	float fSun = GetSunShadow( In.vWorldPos, In.vPosition.w );
	float fTransmittedSun = 0.0f;
	[branch]
	if ( g_vStages.z > 0.0f )
	{
		float3 vNormal = In.vWorldNormal * rsqrt( max( dot( In.vWorldNormal, In.vWorldNormal ), 0.0001f ) );
		float fBacklit = saturate( dot( vNormal, g_vFoliageSun.xyz ) );
		fTransmittedSun = fBacklit * fSun * g_vFoliageSun.w;
	}
#  ifdef DYNAMIC_LIGHTS
	fSun = lerp( fSun, 1.0f, GetDynamicLightsShadowCancel( vGlow ) );
#  endif
	vColour.rgb = saturate( lerp( g_vShadowColour.rgb, g_vAmbientColour.rgb, In.vLighting * fSun ) +
	                       float3( 0.15f, 0.13f, 0.07f ) * fTransmittedSun );
#endif

	// D3DTOP_MODULATE
	if ( g_vStages.x != 0.0f )
		vColour = saturate( g_Texture0.Sample( g_Sampler0, In.vUV0 ) * vColour );

	// D3DTOP_BLENDTEXTUREALPHA
	if ( g_vStages.y != 0.0f )
	{
		float4 vTexture = g_Texture1.Sample( g_Sampler1, In.vUV1 );
		vColour         = saturate( lerp( vColour, vTexture, vTexture.a ) );
	}

	// The additive glowing meshes (the light cones of the lamps at night) fade out close to the camera, from z
	// (view depth) over w: from inside a cone it covered the screen with a pale wedge
	if ( g_vAlphaWeights.z > 0.0f )
		vColour.a *= saturate( ( In.vPosition.w - g_vAlphaWeights.z ) / g_vAlphaWeights.w );

#ifdef ALPHA_COVERAGE
	a_uiCoverage = AlphaTestCoverage( vColour.a, g_vAlphaTest, In.vPosition.xy );
#else
	AlphaTest( vColour.a, g_vAlphaTest );
#endif

#ifdef DYNAMIC_LIGHTS
	vColour.rgb = ApplyDynamicLights( vColour.rgb, vGlow );
#endif

	// D3DFOG_LINEAR
	if ( g_vFog.x != 0.0f )
		vColour.rgb = lerp( g_vFogColour.rgb, vColour.rgb, saturate( ( g_vFog.z - In.vPosition.w ) / ( g_vFog.z - g_vFog.y ) ) );

	return vColour;
}

// The shadow casters: depth only, the texture's alpha cuts the holes of the foliage
struct DEPTH_PS_IN
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
};

DEPTH_PS_IN vs_depth( VS_IN In )
{
	DEPTH_PS_IN Out;
	Out.vPosition = mul( float4( GetFoliagePosition( In.vPosition ), 1.0f ), g_mModelViewProjection );
	Out.vUV       = g_vUVOffsets.xy + In.vUV;
	return Out;
}

void ps_depth( DEPTH_PS_IN In )
{
	if ( g_vStages.x != 0.0f )
		clip( g_Texture0.Sample( g_Sampler0, In.vUV ).a - 0.5f );
}
)";

AWorldShaderHAL::AWorldShaderHAL()
    : m_ShadowColour( 0.3f, 0.3f, 0.3f, 1.0f ), m_AmbientColour( 1.0f, 1.0f, 1.0f, 1.0f )
{
	m_pVertexShader       = TNULL;
	m_pPixelShader        = TNULL;
	m_pInputLayout        = TNULL;
	m_iAlphaRef           = 128;
	m_bIsHighEndMode      = TTRUE;
	m_bRenderEnvMap       = TFALSE;
	m_bAlphaBlendMaterial = TFALSE;
	m_bBatchIsWater      = TFALSE;
#ifdef BARNYARD_COMMUNITY_PATCH
	for ( TINT i = 0; i < FXSHADERS_NUMOF; i++ )
	{
		m_apVertexShaderFx[ i ] = TNULL;
		m_apPixelShaderFx[ i ]  = TNULL;
		m_apInputLayoutFx[ i ]  = TNULL;
	}

	m_pVertexShaderDepth = TNULL;
	m_pPixelShaderDepth  = TNULL;
	m_pInputLayoutDepth  = TNULL;
	m_uiFxFlags          = 0;
	m_uiSetFxShaders     = 0;
	m_bReceiving         = TFALSE;
	m_pBatchLights       = TNULL;
	m_uiBatchCoverage    = 0;
#endif // BARNYARD_COMMUNITY_PATCH
}

AWorldShaderHAL::~AWorldShaderHAL()
{
}

#ifdef BARNYARD_COMMUNITY_PATCH
// The shaders of the main pass: the receivers of the sun shadows while they are on (the lights and alpha to coverage
// are set per packet)
void AWorldShaderHAL::SetMainShaders()
{
	m_uiFxFlags = 0;

	if ( AShadowMaps::IsSunShadowReceiver() && m_apVertexShaderFx[ FXSHADERS_SUN ] )
		m_uiFxFlags |= FXSHADERS_SUN;

	if ( ADynamicLights::IsReceiver() && m_apVertexShaderFx[ FXSHADERS_LIGHTS ] && m_apVertexShaderFx[ FXSHADERS_SUN_LIGHTS ] )
		m_uiFxFlags |= FXSHADERS_LIGHTS;

	// Every permutation of the flush has to exist with coverage too
	TBOOL bCoverage = AShadowMaps::IsCoveragePass();

	for ( TUINT i = 0; i < FXSHADERS_COVERAGE && bCoverage; i++ )
		bCoverage = ( ( i & ~m_uiFxFlags ) != 0 ) || m_apVertexShaderFx[ i | FXSHADERS_COVERAGE ] != TNULL;

	if ( bCoverage )
		m_uiFxFlags |= FXSHADERS_COVERAGE;

	m_bReceiving = ( m_uiFxFlags & ( FXSHADERS_SUN | FXSHADERS_LIGHTS ) ) != 0;
	SetFxShaders( m_uiFxFlags & FXSHADERS_SUN );
}

// a_uiShaders: FXSHADERS_* of the permutation, 0 the shaders of the game
void AWorldShaderHAL::SetFxShaders( TUINT a_uiShaders )
{
	m_uiSetFxShaders = a_uiShaders;

	if ( a_uiShaders == 0 )
		TRenderD3DInterface::Interface()->SetShaders( m_pVertexShader, m_pPixelShader, m_pInputLayout );
	else
		TRenderD3DInterface::Interface()->SetShaders( m_apVertexShaderFx[ a_uiShaders ], m_apPixelShaderFx[ a_uiShaders ], m_apInputLayoutFx[ a_uiShaders ] );
}
#endif // BARNYARD_COMMUNITY_PATCH

void AWorldShaderHAL::Flush()
{
	if ( IsValidated() )
	{
		auto pRender = TRenderD3DInterface::Interface();

		Validate();
#ifdef BARNYARD_COMMUNITY_PATCH
		SetMainShaders();
#else
		pRender->SetShaders( m_pVertexShader, m_pPixelShader, m_pInputLayout );
#endif

		static constexpr TVector4 s_vOne( 1.0f, 1.0f, 1.0f, 1.0f );
		pRender->SetVec4InVSBuffer( CONSTANT_AMBIENT, &m_AmbientColour );
		pRender->SetVec4InVSBuffer( CONSTANT_SHADOW, &m_ShadowColour );
		pRender->SetVec4InVSBuffer( CONSTANT_ONE, &s_vOne );

		pRender->SetZWriteEnable( TTRUE );
		pRender->SetAlphaTestEnable( TTRUE );
		pRender->SetAlphaBlendEnable( TTRUE );
		pRender->SetAlphaFunc( D3D11_COMPARISON_GREATER );
		pRender->SetTextureOp( TRenderD3DInterface::TEXTUREOP_MODULATE );
		pRender->SetCullMode( m_bRenderEnvMap == TTRUE ? D3D11_CULL_BACK : D3D11_CULL_FRONT );
		pRender->SetFogEnable( TFALSE );
	}
}

void AWorldShaderHAL::StartFlush()
{
	if ( IsValidated() )
	{
		auto pRender = TRenderD3DInterface::Interface();

		pRender->SetTextureAddress( 0, ADDRESSINGMODE_WRAP );

		Validate();
#ifdef BARNYARD_COMMUNITY_PATCH
		SetMainShaders();
#else
		pRender->SetShaders( m_pVertexShader, m_pPixelShader, m_pInputLayout );
#endif

		static constexpr TVector4 s_vOne( 1.0f, 1.0f, 1.0f, 1.0f );
		pRender->SetVec4InVSBuffer( CONSTANT_AMBIENT, &m_AmbientColour );
		pRender->SetVec4InVSBuffer( CONSTANT_SHADOW, &m_ShadowColour );
		pRender->SetVec4InVSBuffer( CONSTANT_ONE, &s_vOne );

		pRender->SetZWriteEnable( TTRUE );
		pRender->SetAlphaTestEnable( TTRUE );
		pRender->SetAlphaBlendEnable( TTRUE );
		pRender->SetAlphaFunc( D3D11_COMPARISON_GREATER );
		pRender->SetTextureOp( TRenderD3DInterface::TEXTUREOP_MODULATE );
		pRender->SetCullMode( m_bRenderEnvMap == TTRUE ? D3D11_CULL_BACK : D3D11_CULL_FRONT );
	}
}

void AWorldShaderHAL::EndFlush()
{
	TRenderD3DInterface::Interface()->FlushStripBatch();
	TRenderD3DInterface::Interface()->SetFogEnable( TFALSE );
}

TBOOL AWorldShaderHAL::Create()
{
	m_aOrderTables[ 0 ].Create( this, -3000 );
	m_aOrderTables[ 1 ].Create( this, 100 );
	m_aOrderTables[ 2 ].Create( this, 101 );
	m_aOrderTables[ 3 ].Create( this, 601 );
	m_aOrderTables[ 4 ].Create( this, -400 );
	m_aOrderTables[ 5 ].Create( this, 500 );
	m_aOrderTables[ 6 ].Create( this, -6005 );
	m_aOrderTables[ 7 ].Create( this, -7000 );

	return AWorldShader::Create();
}

TBOOL AWorldShaderHAL::Validate()
{
	if ( IsValidated() )
	{
		return TTRUE;
	}

	SetHighEndMode( TTRUE );

	ID3DBlob* pVSBlob = TRenderD3DInterface::CompileShader( s_szWorldShader, "vs_main", "vs_4_0", TNULL );
	ID3DBlob* pPSBlob = TRenderD3DInterface::CompileShader( s_szWorldShader, "ps_main", "ps_4_0", TNULL );

	if ( !pVSBlob || !pPSBlob ||
	     !TRenderD3DInterface::CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pVertexShader ) ||
	     !TRenderD3DInterface::CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), &m_pPixelShader ) )
	{
		TASSERT( !"Couldn't create the shaders" );
		return TFALSE;
	}

	static constexpr D3D11_INPUT_ELEMENT_DESC s_aInputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof( WorldVertex, Position ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof( WorldVertex, Normal ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof( WorldVertex, Color ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof( WorldVertex, UV ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	TRenderD3DInterface::Interface()->GetDevice()->CreateInputLayout( s_aInputLayout, TARRAYSIZE( s_aInputLayout ), pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pInputLayout );

	pVSBlob->Release();
	pPSBlob->Release();

#ifdef BARNYARD_COMMUNITY_PATCH
	// The receivers of the sun shadows and the lights and the shadow casters, the game runs without them if they don't compile
	for ( TUINT i = 1; i < FXSHADERS_NUMOF; i++ )
		AShadowMaps::CreateShaders( s_szWorldShader, "vs_main", "ps_main", AShadowMaps::GetReceiverDefines( i ), s_aInputLayout, TARRAYSIZE( s_aInputLayout ), &m_apVertexShaderFx[ i ], &m_apPixelShaderFx[ i ], &m_apInputLayoutFx[ i ] );

	AShadowMaps::CreateShaders( s_szWorldShader, "vs_depth", "ps_depth", TNULL, s_aInputLayout, TARRAYSIZE( s_aInputLayout ), &m_pVertexShaderDepth, &m_pPixelShaderDepth, &m_pInputLayoutDepth );
#endif // BARNYARD_COMMUNITY_PATCH

	return AWorldShader::Validate();
}

void AWorldShaderHAL::Invalidate()
{
	if ( !IsValidated() ) return;

	if ( m_pVertexShader )
	{
		m_pVertexShader->Release();
		m_pVertexShader = TNULL;
	}

	if ( m_pPixelShader )
	{
		m_pPixelShader->Release();
		m_pPixelShader = TNULL;
	}

	if ( m_pInputLayout )
	{
		m_pInputLayout->Release();
		m_pInputLayout = TNULL;
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	for ( TUINT i = 1; i < FXSHADERS_NUMOF; i++ )
		AShadowMaps::ReleaseShaders( &m_apVertexShaderFx[ i ], &m_apPixelShaderFx[ i ], &m_apInputLayoutFx[ i ] );

	AShadowMaps::ReleaseShaders( &m_pVertexShaderDepth, &m_pPixelShaderDepth, &m_pInputLayoutDepth );
#endif // BARNYARD_COMMUNITY_PATCH

	TShader::Invalidate();
}

TBOOL AWorldShaderHAL::TryInvalidate()
{
	Invalidate();
	return TTRUE;
}

TBOOL AWorldShaderHAL::TryValidate()
{
	Validate();
	return TTRUE;
}

#ifdef BARNYARD_COMMUNITY_PATCH
// The shadow pass (AShadowMaps): depth only from the light, strips batched like in the main pass
void AWorldShaderHAL::RenderShadowCaster( TRenderPacket* a_pRenderPacket )
{
	auto pRender   = TRenderD3DInterface::Interface();
	auto pMesh     = TSTATICCAST( AWorldMeshHAL, a_pRenderPacket->GetMesh() );
	auto pMaterial = TSTATICCAST( AWorldMaterialHAL, pMesh->GetMaterial() );

	if ( !m_pVertexShaderDepth || !pMaterial || pMesh->IsWater() )
		return;

	// Opaque and alpha blended materials cast, the additive, modulating and glowing ones (light beams, decals) don't
	const AWorldMaterial::BLENDMODE eBlendMode = pMaterial->GetBlendMode();

	if ( ( eBlendMode != AWorldMaterial::BLENDMODE_DEFAULT && eBlendMode != AWorldMaterial::BLENDMODE_ALPHA ) || HASANYFLAG( pMaterial->GetFlags(), TMaterial::FLAGS_GLOW ) )
		return;

	TMatrix44 mMVP;
	mMVP.Multiply( AEffectsShadowPass::ms_mLightProjection, a_pRenderPacket->GetModelViewMatrix() );

	TMatrix44 mMVPTransposed;
	mMVPTransposed.Transpose( mMVP );

	auto pIndexPool  = TSTATICCAST( TIndexPoolResource, pMesh->GetSubMesh( 0 )->pIndexPool );
	auto pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );

	TVertexBlockResource::HALBuffer oVertices;
	pVertexPool->GetHALBuffer( &oVertices );

	const TUINT16* pIndices = pIndexPool->GetIndices();

	if ( pRender->IsStripBatchOpen() )
	{
		if ( pIndices && TUtil::MemCompare( &mMVPTransposed, &m_oBatchMVP, sizeof( m_oBatchMVP ) ) == 0 &&
		     pRender->AddStripToBatch( pIndices, pIndexPool->GetNumIndices(), oVertices.apVertexBuffers[ 0 ], sizeof( WorldVertex ), oVertices.uiVertexOffset * sizeof( WorldVertex ) ) )
		{
			return;
		}

		pRender->FlushStripBatch();
	}

	pRender->SetShaders( m_pVertexShaderDepth, m_pPixelShaderDepth, m_pInputLayoutDepth );
	pRender->SetZWriteEnable( TTRUE );
	pRender->SetAlphaBlendEnable( TFALSE );
	pRender->SetCullMode( D3D11_CULL_NONE );
	pRender->SetVec4InVSBuffer( CONSTANT_MVP, &mMVPTransposed, 4 );

	const TFLOAT fWind = GetFoliageWind( pMaterial );
	const TFLOAT fTime = fWind > 0.0f ? g_oSystemManager.GetScheduler()->GetTotalTime() : 0.0f;
	const TVector4 vStages( pRender->GetTexture( 0 ) ? 1.0f : 0.0f, 0.0f, fWind, fTime );
	pRender->SetVec4InVSBuffer( CONSTANT_STAGES, &vStages );

	if ( pIndices && pRender->AddStripToBatch( pIndices, pIndexPool->GetNumIndices(), oVertices.apVertexBuffers[ 0 ], sizeof( WorldVertex ), oVertices.uiVertexOffset * sizeof( WorldVertex ) ) )
	{
		m_oBatchMVP = mMVPTransposed;
		return;
	}

	TIndexBlockResource::HALBuffer oIndices;
	pIndexPool->GetHALBuffer( &oIndices );

	pRender->DrawIndexed(
	    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	    pIndexPool->GetNumIndices(),
	    oIndices.pIndexBuffer,
	    oIndices.uiIndexOffset * sizeof( TIndexType ),
	    DXGI_FORMAT_R16_UINT,
	    oVertices.apVertexBuffers[ 0 ],
	    sizeof( WorldVertex ),
	    oVertices.uiVertexOffset * sizeof( WorldVertex )
	);
}
#endif // BARNYARD_COMMUNITY_PATCH

void AWorldShaderHAL::Render( TRenderPacket* a_pRenderPacket )
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( AEffectsShadowPass::ms_bActive )
	{
		if ( a_pRenderPacket && a_pRenderPacket->GetMesh() )
			RenderShadowCaster( a_pRenderPacket );

		return;
	}
#endif // BARNYARD_COMMUNITY_PATCH

	if ( IsHighEndMode() && a_pRenderPacket && a_pRenderPacket->GetMesh() )
	{
		auto pRender         = TRenderD3DInterface::Interface();
		auto pCurrentContext = TRenderContextD3D::Upcast( pRender->GetCurrentContext() );

		TMatrix44 mMVP;
		mMVP.Multiply( pCurrentContext->GetProjectionMatrix(), a_pRenderPacket->GetModelViewMatrix() );

		TMatrix44 mMVPTransposed;
		mMVPTransposed.Transpose( mMVP );

		auto pBatchMesh      = TSTATICCAST( AWorldMeshHAL, a_pRenderPacket->GetMesh() );
		auto pBatchIndexPool = TSTATICCAST( TIndexPoolResource, pBatchMesh->GetSubMesh( 0 )->pIndexPool );
		auto pBatchPool      = TSTATICCAST( TVertexPoolResource, pBatchMesh->GetVertexPool() );

#ifdef BARNYARD_COMMUNITY_PATCH
		// Glowing meshes are drawn into the glow target of the bloom too, one at a time (AEffects); the additive ones
		// fade out close to the camera (their own alpha weights, they don't batch)
		const TBOOL bGlowing  = pBatchMesh->GetMaterial() && HASANYFLAG( pBatchMesh->GetMaterial()->GetFlags(), TMaterial::FLAGS_GLOW );
		const TBOOL bGlow     = bGlowing && AEffects::IsGlowTargetActive();
		const TBOOL bGlowFade = bGlowing && TSTATICCAST( AWorldMaterialHAL, pBatchMesh->GetMaterial() )->GetBlendMode() == AWorldMaterial::BLENDMODE_ADDITIVE;
#else
		constexpr TBOOL bGlowing = TFALSE;
		constexpr TBOOL bGlow    = TFALSE;
#endif // BARNYARD_COMMUNITY_PATCH

		// Fading meshes change the alpha test between packets
		const TBOOL    bCanBatch = !bGlowing && a_pRenderPacket->GetAlpha() >= 1.0f && pBatchIndexPool->GetIndices() != TNULL;
		const TBOOL    bIsWater  = pBatchMesh->IsWater();
		const TUINT16* pIndices  = bCanBatch ? pBatchIndexPool->GetIndices() : TNULL;

		TVertexBlockResource::HALBuffer oBatchVertices;
		pBatchPool->GetHALBuffer( &oBatchVertices );

#ifdef BARNYARD_COMMUNITY_PATCH
		// The lights of the packet (ADynamicLights) and its alpha to coverage are a part of the batch key. The glowing
		// meshes shine by themselves (ERRender leaves them out too), the packets without blending use alpha to coverage
		auto        pBatchMaterial = TSTATICCAST( AWorldMaterialHAL, pBatchMesh->GetMaterial() );
		const TBOOL bGlowMaterial  = pBatchMaterial && HASANYFLAG( pBatchMaterial->GetFlags(), TMaterial::FLAGS_GLOW );
		void* const pPacketLights  = ( ( m_uiFxFlags & FXSHADERS_LIGHTS ) && !bGlowMaterial ) ? a_pRenderPacket->GetUserData() : TNULL;
		const TBOOL bBlended       = !pBatchMaterial || pBatchMaterial->GetBlendMode() != AWorldMaterial::BLENDMODE_DEFAULT || a_pRenderPacket->GetAlpha() < 1.0f;
		const TUINT uiCoverage     = ( ( m_uiFxFlags & FXSHADERS_COVERAGE ) && !bBlended ) ? FXSHADERS_COVERAGE : 0;
#else
		void* const     pPacketLights     = TNULL;
		void* const     m_pBatchLights    = TNULL;
		constexpr TUINT uiCoverage        = 0;
		constexpr TUINT m_uiBatchCoverage = 0;
#endif // BARNYARD_COMMUNITY_PATCH

		if ( pRender->IsStripBatchOpen() )
		{
			if ( bCanBatch && bIsWater == m_bBatchIsWater && pPacketLights == m_pBatchLights && uiCoverage == m_uiBatchCoverage && TUtil::MemCompare( &mMVPTransposed, &m_oBatchMVP, sizeof( m_oBatchMVP ) ) == 0 &&
			     pRender->AddStripToBatch( pIndices, pBatchIndexPool->GetNumIndices(), oBatchVertices.apVertexBuffers[ 0 ], sizeof( WorldVertex ), oBatchVertices.uiVertexOffset * sizeof( WorldVertex ) ) )
			{
				return;
			}

			pRender->FlushStripBatch();
		}

		pRender->SetVec4InVSBuffer( CONSTANT_MVP, &mMVPTransposed, 4 );

#ifdef BARNYARD_COMMUNITY_PATCH
		// The permutation of this packet (the previous batch is drawn already): the sun shadows of the flush, the
		// lights of the packet, alpha to coverage without blending
		if ( m_uiFxFlags != 0 )
		{
			TUINT uiShaders = ( m_uiFxFlags & FXSHADERS_SUN ) | uiCoverage;

			if ( ( m_uiFxFlags & FXSHADERS_LIGHTS ) && ADynamicLights::SetPacketLights( pPacketLights ) )
				uiShaders |= FXSHADERS_LIGHTS;

			if ( uiShaders != m_uiSetFxShaders )
				SetFxShaders( uiShaders );
		}

		// The receivers need the world position (the batch key stays the MVP: the model matrix follows from it)
		if ( m_bReceiving )
		{
			TMatrix44 mModel;
			mModel.Multiply( pCurrentContext->GetViewWorldMatrix(), a_pRenderPacket->GetModelViewMatrix() );

			TMatrix44 mModelTransposed;
			mModelTransposed.Transpose( mModel );
			pRender->SetVec4InVSBuffer( CONSTANT_MODEL, &mModelTransposed, 4 );
		}
#endif // BARNYARD_COMMUNITY_PATCH

		if ( a_pRenderPacket->GetAlpha() >= 1.0f || isnan( a_pRenderPacket->GetAlpha() ) )
		{
			if ( s_RenderStateFlags & RENDERSTATE_ALPHAREF )
			{
				pRender->SetAlphaRef( m_iAlphaRef );
				s_RenderStateFlags &= ~RENDERSTATE_ALPHAREF;
			}
		}
		else
		{
			pRender->SetAlphaRef( TUINT8( m_iAlphaRef * a_pRenderPacket->GetAlpha() ) );
			s_RenderStateFlags |= RENDERSTATE_ALPHAREF;
		}

		auto pMesh     = TSTATICCAST( AWorldMeshHAL, a_pRenderPacket->GetMesh() );
		auto pMaterial = TSTATICCAST( AWorldMaterialHAL, pMesh->GetMaterial() );

		if ( pMaterial->GetBlendMode() != AWorldMaterial::BLENDMODE_DEFAULT || a_pRenderPacket->GetAlpha() < 1.0f )
		{
			if ( ISZERO( s_RenderStateFlags & RENDERSTATE_ALPHABLEND ) )
			{
				pRender->SetAlphaBlendEnable( TTRUE );
				s_RenderStateFlags |= RENDERSTATE_ALPHABLEND;
			}
		}
		else if ( !ISZERO( s_RenderStateFlags & RENDERSTATE_ALPHABLEND ) )
		{
			pRender->SetAlphaBlendEnable( TFALSE );
			s_RenderStateFlags &= ~RENDERSTATE_ALPHABLEND;
		}

		TVector4 vAlphaWeights;
		vAlphaWeights.x = 0.0f;
		vAlphaWeights.y = 1.0f;

		if ( pMesh->IsWater() )
		{
			vAlphaWeights.x = 1.0f;
			vAlphaWeights.y = 0.0f;
		}

		vAlphaWeights.w = 1.0f;
		vAlphaWeights.z = 0.0f;

#ifdef BARNYARD_COMMUNITY_PATCH
		// The light cones of the lamps fade out from 12 to 2 units in front of the camera (see ps_main)
		if ( bGlowFade )
		{
			vAlphaWeights.z = 2.0f;
			vAlphaWeights.w = 10.0f;
		}
#endif // BARNYARD_COMMUNITY_PATCH
		pRender->SetVec4InVSBuffer( CONSTANT_ALPHAWEIGHTS, &vAlphaWeights );

		TVector4 vFog;
		TVector4 vFogColour;
		pRender->GetFogParams( vFog, vFogColour );

		const TVector4 vAlphaTest = pRender->GetAlphaTestParams();
#ifdef BARNYARD_COMMUNITY_PATCH
		const TFLOAT fWind = GetFoliageWind( pMaterial );
		const TFLOAT fTime = fWind > 0.0f ? g_oSystemManager.GetScheduler()->GetTotalTime() : 0.0f;

		if ( fWind > 0.0f && HASANYFLAG( m_uiFxFlags, FXSHADERS_SUN ) )
		{
			const auto& vDirection = AShadowMaps::GetSunDirection();
			const TVector4 vFoliageSun( vDirection.x, vDirection.y, vDirection.z, AShadowMaps::GetSunFade() );
			pRender->SetVec4InVSBuffer( CONSTANT_FOLIAGESUN, &vFoliageSun );
		}
#else
		constexpr TFLOAT fWind = 0.0f;
		constexpr TFLOAT fTime = 0.0f;
#endif
		const TVector4 vStages( pRender->GetTexture( 0 ) ? 1.0f : 0.0f, pMaterial->GetTextureNum() > 1 && pRender->GetTexture( 1 ) ? 1.0f : 0.0f, fWind, fTime );
		pRender->SetVec4InVSBuffer( CONSTANT_ALPHATEST, &vAlphaTest );
		pRender->SetVec4InVSBuffer( CONSTANT_FOG, &vFog );
		pRender->SetVec4InVSBuffer( CONSTANT_FOGCOLOUR, &vFogColour );
		pRender->SetVec4InVSBuffer( CONSTANT_STAGES, &vStages );

		auto pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
		TVALIDPTR( pVertexPool );

		TVertexBlockResource::HALBuffer vertexBuffer;
		pVertexPool->GetHALBuffer( &vertexBuffer );

		auto pIndexPool = TSTATICCAST( TIndexPoolResource, pMesh->GetSubMesh( 0 )->pIndexPool );
		TVALIDPTR( pIndexPool );

		TIndexBlockResource::HALBuffer indexBuffer;
		pIndexPool->GetHALBuffer( &indexBuffer );

		if ( bCanBatch && pRender->AddStripToBatch( pIndices, pIndexPool->GetNumIndices(), vertexBuffer.apVertexBuffers[ 0 ], sizeof( WorldVertex ), vertexBuffer.uiVertexOffset * sizeof( WorldVertex ) ) )
		{
			m_oBatchMVP     = mMVPTransposed;
			m_bBatchIsWater = bIsWater;
#ifdef BARNYARD_COMMUNITY_PATCH
			m_pBatchLights    = pPacketLights;
			m_uiBatchCoverage = uiCoverage;
#endif
			return;
		}

		pRender->DrawIndexed(
		    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
		    pIndexPool->GetNumIndices(),
		    indexBuffer.pIndexBuffer,
		    indexBuffer.uiIndexOffset * sizeof( TIndexType ),
		    DXGI_FORMAT_R16_UINT,
		    vertexBuffer.apVertexBuffers[ 0 ],
		    sizeof( WorldVertex ),
		    vertexBuffer.uiVertexOffset * sizeof( WorldVertex )
		);

#ifdef BARNYARD_COMMUNITY_PATCH
		if ( bGlow && AEffects::BeginGlowDraw() )
		{
			pRender->DrawIndexed(
			    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
			    pIndexPool->GetNumIndices(),
			    indexBuffer.pIndexBuffer,
			    indexBuffer.uiIndexOffset * sizeof( TIndexType ),
			    DXGI_FORMAT_R16_UINT,
			    vertexBuffer.apVertexBuffers[ 0 ],
			    sizeof( WorldVertex ),
			    vertexBuffer.uiVertexOffset * sizeof( WorldVertex )
			);

			AEffects::EndGlowDraw();
		}
#endif // BARNYARD_COMMUNITY_PATCH
	}
}

void AWorldShaderHAL::EnableRenderEnvMap( TBOOL a_bEnable )
{
	m_bRenderEnvMap = a_bEnable;
}

TBOOL AWorldShaderHAL::IsAlphaBlendMaterial()
{
	return m_bAlphaBlendMaterial;
}

void AWorldShaderHAL::SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial )
{
	m_bAlphaBlendMaterial = a_bIsAlphaBlendMaterial;
}

AWorldMaterial* AWorldShaderHAL::CreateMaterial( const TCHAR* a_szName )
{
	Validate();

	auto pMaterial = new AWorldMaterialHAL();
	pMaterial->SetShader( this );

	if ( AWorldShaderHAL::IsAlphaBlendMaterial() )
	{
		auto pAlphaBlendMaterial = new AWorldMaterialHAL();
		pAlphaBlendMaterial->SetShader( this );
		pAlphaBlendMaterial->Create( 1 );

		pMaterial->SetAlphaBlendMaterial( pAlphaBlendMaterial );
	}

	return pMaterial;
}

AWorldMesh* AWorldShaderHAL::CreateMesh( const TCHAR* a_szName )
{
	Validate();

	auto pMesh = new AWorldMeshHAL();
	pMesh->SetOwnerShader( this );

	return pMesh;
}

TBOOL AWorldShaderHAL::IsHighEndMode()
{
	return m_bIsHighEndMode;
}

void AWorldShaderHAL::SetHighEndMode( TBOOL a_bEnable )
{
	m_bIsHighEndMode = a_bEnable && IsCapableShaders();
}

TBOOL AWorldShaderHAL::IsCapableShaders()
{
	return TTRUE;
}

TBOOL AWorldShaderHAL::IsRenderEnvMapEnabled()
{
	return m_bRenderEnvMap;
}

void* AWorldShaderHAL::CreateUnknown( void*, void*, void*, void* )
{
	TASSERT( !"What's this?" );
	return TNULL;
}

void AWorldShaderHAL::SetColours( const Toshi::TVector4& a_rShadowColour, const Toshi::TVector4 a_rAmbientColour )
{
	m_ShadowColour  = a_rShadowColour;
	m_AmbientColour = a_rAmbientColour;

	Toshi::TMath::Clip( m_ShadowColour.x, 0.0f, 1.0f );
	Toshi::TMath::Clip( m_ShadowColour.y, 0.0f, 1.0f );
	Toshi::TMath::Clip( m_ShadowColour.z, 0.0f, 1.0f );
	Toshi::TMath::Clip( m_AmbientColour.x, 0.0f, 1.0f );
	Toshi::TMath::Clip( m_AmbientColour.y, 0.0f, 1.0f );
	Toshi::TMath::Clip( m_AmbientColour.z, 0.0f, 1.0f );
}
