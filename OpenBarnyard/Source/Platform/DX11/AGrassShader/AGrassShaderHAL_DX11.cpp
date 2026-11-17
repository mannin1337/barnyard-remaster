#include "pch.h"
#include "AGrassShaderHAL_DX11.h"
#include "AGrassMaterialHAL_DX11.h"
#include "AGrassMeshHAL_DX11.h"
#include "Cameras/ACameraManager.h"
#include "Assets/AModelLoader.h"
#include "Platform/DX11/AWorldShader/AWorldShader_DX11.h"

#include <Toshi/TScheduler.h>
#include <Platform/DX11/TRenderInterface_DX11.h>
#include <Platform/DX11/TRenderContext_DX11.h>
#include <Platform/DX11/TVertexBlockResource_DX11.h>
#include <Platform/DX11/TVertexPoolResource_DX11.h>
#include <Platform/DX11/TIndexBlockResource_DX11.h>
#include <Platform/DX11/TIndexPoolResource_DX11.h>
#include <Platform/DX11/TTextureResourceHAL_DX11.h>

#ifdef BARNYARD_COMMUNITY_PATCH
#  include "Platform/DX11/Effects/AShadowMaps_DX11.h"
#  include "Platform/DX11/Effects/ADynamicLights_DX11.h"
#endif

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( AGrassShaderHAL );

MODAPI TTextureResourceHAL* g_pGrassShaderTerrainTexture = TNULL;
static TVector3      g_vecAnimationDataOffset;
static TVector4      g_vecAnimOffset;
static const TFLOAT  g_fAnimationSpeed = 2.25f;
static TFLOAT        g_fWindTime;

// AGrassShader_D3D8_Win.vsh and the texture stages of the grass.
// BARNYARD_COMMUNITY_PATCH: SUN_SHADOWS receives the sun shadows (AShadowMaps): the shadow scales the vertex
// lighting, like in the world shader. DYNAMIC_LIGHTS adds the lights of the packet (ADynamicLights). vs_depth
// draws the ground of the grass into the shadow maps (AEffectsShadowPass)
static const TCHAR s_szGrassShader[] = R"(
cbuffer Constants : register( b0 )
{
	float4x4 g_mModelViewProjection : packoffset( c0 );
	float4   g_vAmbientColour       : packoffset( c4 );
	float4   g_vShadowColour        : packoffset( c5 );
	float4   g_vOne                 : packoffset( c6 );
	float4   g_vLayerOffset         : packoffset( c7 );  // how far the layer is pushed along the normal
	float4   g_vAnimation           : packoffset( c8 );  // wind (xyz), UV scale of the layer (w)
	float4   g_vAlphaTest           : packoffset( c9 );
	float4   g_vFog                 : packoffset( c10 ); // enabled, start, end
	float4   g_vFogColour           : packoffset( c11 );
	float4   g_vStages              : packoffset( c12 ); // texture of stage 0, wind time
#if defined( SUN_SHADOWS ) || defined( DYNAMIC_LIGHTS )
	float4x4 g_mModel               : packoffset( c13 ); // model to world
#endif
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
	float2 vUV : TEXCOORD0;
#if defined( SUN_SHADOWS ) || defined( DYNAMIC_LIGHTS )
	float3 vWorldPos : TEXCOORD1;
	float3 vLighting : TEXCOORD2;
	float3 vWorldNormal : TEXCOORD3;
#endif
};

PS_IN vs_main( VS_IN In )
{
	float4 vPosition = float4( In.vNormal, 1.0f ) * g_vLayerOffset;
	vPosition += g_vAnimation * g_vLayerOffset;
	vPosition += float4( In.vPosition, 1.0f );

	// Keep the ground fixed and give the tips most of the displacement.
	float fTip = saturate( 4.0f * g_vLayerOffset.x );
	[branch]
	if ( fTip > 0.0f )
	{
		float fPhase = dot( In.vPosition.xz, float2( 0.16f, 0.22f ) );
		float fGust = sin( fPhase - g_vStages.y * 1.35f );
		float fRipple = sin( fPhase * 3.1f + In.vPosition.x * 0.43f - g_vStages.y * 2.8f );
		vPosition.xz += fTip * fTip * float2( 0.060f * fGust + 0.025f * fRipple, 0.034f * fGust - 0.017f * fRipple );
	}

	float4 vLighting = float4( In.vLighting, 1.0f );
	float4 vColour   = vLighting * g_vAmbientColour + ( g_vOne - vLighting ) * g_vShadowColour;
	vColour.w        = g_vOne.w;

	PS_IN Out;
	Out.vPosition = mul( vPosition, g_mModelViewProjection );
	Out.vColour   = saturate( vColour );
	Out.vUV       = In.vUV * g_vAnimation.w;
#if defined( SUN_SHADOWS ) || defined( DYNAMIC_LIGHTS )
	Out.vWorldPos    = mul( float4( vPosition.xyz, 1.0f ), g_mModel ).xyz;
	Out.vLighting    = In.vLighting;
	Out.vWorldNormal = normalize( mul( In.vNormal, (float3x3)g_mModel ) );
#endif
	return Out;
}

// The shadow casters: the ground layer, depth only
float4 vs_depth( VS_IN In ) : SV_POSITION
{
	return mul( float4( In.vPosition, 1.0f ), g_mModelViewProjection );
}

Texture2D    g_Texture : register( t0 );
SamplerState g_Sampler : register( s0 );

float4 ps_main( PS_IN In
#ifdef ALPHA_COVERAGE
	, out uint a_uiCoverage : SV_Coverage
#endif
	) : SV_TARGET
{
	float4 vColour = In.vColour;

#ifdef DYNAMIC_LIGHTS
	float3 vGlow = GetDynamicLights( In.vWorldPos, In.vWorldNormal );
#endif

#ifdef SUN_SHADOWS
	// The shadowed part of the light goes like the baked lighting
	float fSun = GetSunShadow( In.vWorldPos, In.vPosition.w );
#  ifdef DYNAMIC_LIGHTS
	fSun = lerp( fSun, 1.0f, GetDynamicLightsShadowCancel( vGlow ) );
#  endif
	vColour.rgb = saturate( lerp( g_vShadowColour.rgb, g_vAmbientColour.rgb, In.vLighting * fSun ) );
#endif

	// D3DTOP_MODULATE
	if ( g_vStages.x != 0.0f )
		vColour = saturate( g_Texture.Sample( g_Sampler, In.vUV ) * vColour );

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
)";

AGrassShaderHAL::AGrassShaderHAL()
    : m_pVertexShader( TNULL )
    , m_pPixelShader( TNULL )
    , m_pInputLayout( TNULL )
{
}

AGrassShaderHAL::~AGrassShaderHAL()
{
}

void AGrassShaderHAL::Flush()
{
	Validate();

	auto pRender         = TRenderD3DInterface::Interface();
	auto pCurrentContext = TRenderContextD3D::Upcast( pRender->GetCurrentContext() );

	pRender->SetTextureMinFilter( 0, TRenderD3DInterface::TEXTUREFILTER_LINEAR );
	pRender->SetTextureMagFilter( 0, TRenderD3DInterface::TEXTUREFILTER_LINEAR );
	pRender->SetTextureMipFilter( 0, TRenderD3DInterface::TEXTUREFILTER_POINT );
	pRender->SetTextureOp( TRenderD3DInterface::TEXTUREOP_MODULATE );
	pRender->SetFixedFunctionShader();

	pRender->SetTransform( TRenderD3DInterface::TRANSFORM_PROJECTION, pCurrentContext->GetProjectionMatrix() );
	pRender->SetTransform( TRenderD3DInterface::TRANSFORM_VIEW, TMatrix44::IDENTITY );

	pRender->SetAlphaBlendEnable( TFALSE );
	pRender->SetAlphaTestEnable( TTRUE );
	pRender->SetAlphaRef( 1 );
	pRender->SetAlphaFunc( D3D11_COMPARISON_GREATER );

	m_oOrderTable.Render();
	pRender->SetAlphaTestEnable( TFALSE );
}

void AGrassShaderHAL::StartFlush()
{
	if ( IsValidated() )
	{
		auto pRender = TRenderD3DInterface::Interface();

		pRender->SetTextureAddress( 0, ADDRESSINGMODE_WRAP );

#ifdef BARNYARD_COMMUNITY_PATCH
		// The receivers of the sun shadows while they are on (the lights are set per packet)
		m_uiFxFlags = 0;

		if ( AShadowMaps::IsSunShadowReceiver() && m_aFxShaders[ FXSHADERS_SUN ].pVertexShader )
			m_uiFxFlags |= FXSHADERS_SUN;

		if ( ADynamicLights::IsReceiver() && m_aFxShaders[ FXSHADERS_LIGHTS ].pVertexShader && m_aFxShaders[ FXSHADERS_SUN | FXSHADERS_LIGHTS ].pVertexShader )
			m_uiFxFlags |= FXSHADERS_LIGHTS;

		// The grass doesn't blend: alpha to coverage for all of it under MSAA, if every permutation of the flush has it
		TBOOL bCoverage = AShadowMaps::IsCoveragePass();

		for ( TUINT i = 0; i < FXSHADERS_COVERAGE && bCoverage; i++ )
			bCoverage = ( ( i & ~m_uiFxFlags ) != 0 ) || m_aFxShaders[ i | FXSHADERS_COVERAGE ].pVertexShader != TNULL;

		if ( bCoverage )
			m_uiFxFlags |= FXSHADERS_COVERAGE;

		SetFxShaders( m_uiFxFlags & ( FXSHADERS_SUN | FXSHADERS_COVERAGE ) );
#else
		pRender->SetShaders( m_pVertexShader, m_pPixelShader, m_pInputLayout );
#endif // BARNYARD_COMMUNITY_PATCH

		AWorldShaderHAL*          pWorldShader    = TSTATICCAST( AWorldShaderHAL, AWorldShader::GetSingleton() );
		TVector4                  vecShadowColor  = pWorldShader->GetShadowColour();
		TVector4                  vecAmbientColor = pWorldShader->GetAmbientColour();
		static constexpr TVector4 vecColor        = TVector4( 1.0f, 1.0f, 1.0f, 1.0f );

		pRender->SetVec4InVSBuffer( CONSTANT_AMBIENT, &vecAmbientColor );
		pRender->SetVec4InVSBuffer( CONSTANT_SHADOW, &vecShadowColor );
		pRender->SetVec4InVSBuffer( CONSTANT_ONE, &vecColor );

		pRender->SetZWriteEnable( TTRUE );
		pRender->SetAlphaTestEnable( TTRUE );
		pRender->SetAlphaBlendEnable( TFALSE );
		pRender->SetAlphaFunc( D3D11_COMPARISON_GREATER );
		pRender->SetTextureOp( TRenderD3DInterface::TEXTUREOP_MODULATE );

		UpdateAnimation();
	}
}

void AGrassShaderHAL::EndFlush()
{
	auto pRender = TRenderD3DInterface::Interface();

	pRender->SetFogEnable( TFALSE );
	pRender->SetAlphaBlendEnable( TTRUE );
}

TBOOL AGrassShaderHAL::Create()
{
	m_oOrderTable.Create( this, -2000 );
	AGrassShader::Create();

	return Validate();
}

TBOOL AGrassShaderHAL::Validate()
{
	if ( IsValidated() )
		return TTRUE;

	ID3DBlob* pVSBlob = TRenderD3DInterface::CompileShader( s_szGrassShader, "vs_main", "vs_4_0", TNULL );
	ID3DBlob* pPSBlob = TRenderD3DInterface::CompileShader( s_szGrassShader, "ps_main", "ps_4_0", TNULL );

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
		AShadowMaps::CreateShaders( s_szGrassShader, "vs_main", "ps_main", AShadowMaps::GetReceiverDefines( i ), s_aInputLayout, TARRAYSIZE( s_aInputLayout ), &m_aFxShaders[ i ].pVertexShader, &m_aFxShaders[ i ].pPixelShader, &m_aFxShaders[ i ].pInputLayout );

	AShadowMaps::CreateShaders( s_szGrassShader, "vs_depth", TNULL, TNULL, s_aInputLayout, TARRAYSIZE( s_aInputLayout ), &m_pVertexShaderDepth, TNULL, &m_pInputLayoutDepth );
#endif // BARNYARD_COMMUNITY_PATCH

	return AGrassShader::Validate();
}

void AGrassShaderHAL::Invalidate()
{
	if ( IsValidated() )
	{
		ID3D11DeviceChild* apObjects[] = { m_pVertexShader, m_pPixelShader, m_pInputLayout };

		for ( auto pObject : apObjects )
		{
			if ( pObject )
				pObject->Release();
		}

		m_pVertexShader = TNULL;
		m_pPixelShader  = TNULL;
		m_pInputLayout  = TNULL;

#ifdef BARNYARD_COMMUNITY_PATCH
		for ( TUINT i = 1; i < FXSHADERS_NUMOF; i++ )
			AShadowMaps::ReleaseShaders( &m_aFxShaders[ i ].pVertexShader, &m_aFxShaders[ i ].pPixelShader, &m_aFxShaders[ i ].pInputLayout );

		AShadowMaps::ReleaseShaders( &m_pVertexShaderDepth, TNULL, &m_pInputLayoutDepth );
#endif // BARNYARD_COMMUNITY_PATCH

		AGrassShader::Invalidate();
	}
}

TBOOL AGrassShaderHAL::TryInvalidate()
{
	Invalidate();
	return TTRUE;
}

TBOOL AGrassShaderHAL::TryValidate()
{
	Validate();
	return TTRUE;
}

static void DrawGrassLayer( TIndexPoolResource* a_pIndexPool, const TIndexBlockResource::HALBuffer& a_rIndexBuffer, const TVertexBlockResource::HALBuffer& a_rVertexBuffer )
{
	auto pRender = TRenderD3DInterface::Interface();

	TVector4 vFog;
	TVector4 vFogColour;
	pRender->GetFogParams( vFog, vFogColour );

	const TVector4 vAlphaTest = pRender->GetAlphaTestParams();
	const TVector4 vStages( pRender->GetTexture( 0 ) ? 1.0f : 0.0f, g_fWindTime, 0.0f, 0.0f );
	pRender->SetVec4InVSBuffer( AGrassShaderHAL::CONSTANT_ALPHATEST, &vAlphaTest );
	pRender->SetVec4InVSBuffer( AGrassShaderHAL::CONSTANT_FOG, &vFog );
	pRender->SetVec4InVSBuffer( AGrassShaderHAL::CONSTANT_FOGCOLOUR, &vFogColour );
	pRender->SetVec4InVSBuffer( AGrassShaderHAL::CONSTANT_STAGES, &vStages );

	pRender->DrawIndexed(
	    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	    a_pIndexPool->GetNumIndices(),
	    a_rIndexBuffer.pIndexBuffer,
	    a_rIndexBuffer.uiIndexOffset * sizeof( TIndexType ),
	    DXGI_FORMAT_R16_UINT,
	    a_rVertexBuffer.apVertexBuffers[ 0 ],
	    sizeof( WorldVertex ),
	    a_rVertexBuffer.uiVertexOffset * sizeof( WorldVertex )
	);
}

void AGrassShaderHAL::Render( Toshi::TRenderPacket* a_pRenderPacket )
{
	AGrassMeshHAL* pMesh;

	if ( a_pRenderPacket && ( pMesh = TSTATICCAST( AGrassMeshHAL, a_pRenderPacket->GetMesh() ), pMesh != TNULL ) )
	{
		auto pRender         = TRenderD3DInterface::Interface();
		auto pCurrentContext = TRenderContextD3D::Upcast( pRender->GetCurrentContext() );

#ifdef BARNYARD_COMMUNITY_PATCH
		// The shadow pass (AShadowMaps): the ground layer only, depth only, as in ERRender
		if ( AEffectsShadowPass::ms_bActive )
		{
			if ( !m_pVertexShaderDepth )
				return;

			TMatrix44 matShadowMVP;
			matShadowMVP.Multiply( AEffectsShadowPass::ms_mLightProjection, a_pRenderPacket->GetModelViewMatrix() );

			TMatrix44 matShadowMVPTransposed;
			matShadowMVPTransposed.Transpose( matShadowMVP );

			pRender->SetShaders( m_pVertexShaderDepth, TNULL, m_pInputLayoutDepth );
			pRender->SetZWriteEnable( TTRUE );
			pRender->SetAlphaBlendEnable( TFALSE );
			pRender->SetCullMode( D3D11_CULL_NONE );
			pRender->SetVec4InVSBuffer( CONSTANT_MVP, &matShadowMVPTransposed, 4 );

			TVertexBlockResource::HALBuffer oShadowVertices;
			TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() )->GetHALBuffer( &oShadowVertices );

			TIndexPoolResource*            pShadowIndexPool = TSTATICCAST( TIndexPoolResource, pMesh->GetSubMesh( 0 )->pIndexPool );
			TIndexBlockResource::HALBuffer oShadowIndices;
			pShadowIndexPool->GetHALBuffer( &oShadowIndices );

			pRender->DrawIndexed(
			    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
			    pShadowIndexPool->GetNumIndices(),
			    oShadowIndices.pIndexBuffer,
			    oShadowIndices.uiIndexOffset * sizeof( TIndexType ),
			    DXGI_FORMAT_R16_UINT,
			    oShadowVertices.apVertexBuffers[ 0 ],
			    sizeof( WorldVertex ),
			    oShadowVertices.uiVertexOffset * sizeof( WorldVertex )
			);

			return;
		}
#endif // BARNYARD_COMMUNITY_PATCH

		TMatrix44 matMVP;
		matMVP.Multiply( pCurrentContext->GetProjectionMatrix(), a_pRenderPacket->GetModelViewMatrix() );

		TMatrix44 matMVPTransposed;
		matMVPTransposed.Transpose( matMVP );
		pRender->SetVec4InVSBuffer( CONSTANT_MVP, &matMVPTransposed, 4 );

#ifdef BARNYARD_COMMUNITY_PATCH
		// The lights of this packet
		if ( m_uiFxFlags & FXSHADERS_LIGHTS )
		{
			const TUINT uiShaders = ( m_uiFxFlags & ( FXSHADERS_SUN | FXSHADERS_COVERAGE ) ) | ( ADynamicLights::SetPacketLights( a_pRenderPacket->GetUserData() ) ? FXSHADERS_LIGHTS : 0 );

			if ( uiShaders != m_uiSetFxShaders )
				SetFxShaders( uiShaders );
		}

		// The receivers need the world position
		if ( m_uiFxFlags & ( FXSHADERS_SUN | FXSHADERS_LIGHTS ) )
		{
			TMatrix44 matModel;
			matModel.Multiply( pCurrentContext->GetViewWorldMatrix(), a_pRenderPacket->GetModelViewMatrix() );

			TMatrix44 matModelTransposed;
			matModelTransposed.Transpose( matModel );
			pRender->SetVec4InVSBuffer( CONSTANT_MODEL, &matModelTransposed, 4 );
		}
#endif // BARNYARD_COMMUNITY_PATCH

		TVertexBlockResource::HALBuffer oVertexBuffer;
		TVertexPoolResource*            pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
		pVertexPool->GetHALBuffer( &oVertexBuffer );

		TIndexBlockResource::HALBuffer oIndexBuffer;
		TIndexPoolResource*            pIndexPool = TSTATICCAST( TIndexPoolResource, pMesh->GetSubMesh( 0 )->pIndexPool );
		pIndexPool->GetHALBuffer( &oIndexBuffer );

		TVector4 vecOffset = TVector4( 0.0f, 0.0f, 0.0f, 0.0f );
		g_vecAnimOffset.w  = 1.0f;

		pRender->SetVec4InVSBuffer( CONSTANT_ANIMATION, &g_vecAnimOffset );
		pRender->SetVec4InVSBuffer( CONSTANT_LAYER, &vecOffset );
		pRender->SetTexture( 0, g_pGrassShaderTerrainTexture->GetD3DTexture() );
		DrawGrassLayer( pIndexPool, oIndexBuffer, oVertexBuffer );

		ACamera* pCamera   = ACameraManager::GetSingleton()->GetCurrentCamera();
		TVector4 vecCamPos = pCamera->GetMatrix().GetTranslation();

		// Transform coordinates to the actual world position, since they are exported rotated
		TVector4 vecMeshBounding;
		vecMeshBounding.x = pMesh->GetCellMeshSphere()->m_BoundingSphere.AsVector4().x;
		vecMeshBounding.y = -pMesh->GetCellMeshSphere()->m_BoundingSphere.AsVector4().z;
		vecMeshBounding.z = pMesh->GetCellMeshSphere()->m_BoundingSphere.AsVector4().y;
		vecMeshBounding.w = pMesh->GetCellMeshSphere()->m_BoundingSphere.GetRadius();

		TFLOAT fDistanceToCamera = TVector4::DistanceXZ( vecMeshBounding, vecCamPos ) - vecMeshBounding.w;

		// The layers of the grass density (render.grass_density, the original ones with Normal)
		const TINT iNumLayers = GetNumLayers( fDistanceToCamera );

		if ( iNumLayers > 0 )
		{
			g_vecAnimOffset.w = 2.0f;
			TFLOAT fStepSize  = 0.25f / iNumLayers;

			for ( TINT i = 1; i <= iNumLayers; i++ )
			{
				pRender->SetTexture( 0, g_aGrassLayers[ GetLayerTexture( i, iNumLayers ) ]->GetD3DTexture() );
				pRender->SetVec4InVSBuffer( CONSTANT_ANIMATION, &g_vecAnimOffset );

				vecOffset.x += fStepSize;
				vecOffset.y += fStepSize;
				vecOffset.z += fStepSize;
				pRender->SetVec4InVSBuffer( CONSTANT_LAYER, &vecOffset );
				DrawGrassLayer( pIndexPool, oIndexBuffer, oVertexBuffer );
			}
		}
	}
}

#ifdef BARNYARD_COMMUNITY_PATCH
// a_uiShaders: FXSHADERS_* of the permutation, 0 the shaders of the game
void AGrassShaderHAL::SetFxShaders( TUINT a_uiShaders )
{
	m_uiSetFxShaders = a_uiShaders;

	if ( a_uiShaders == 0 )
		TRenderD3DInterface::Interface()->SetShaders( m_pVertexShader, m_pPixelShader, m_pInputLayout );
	else
		TRenderD3DInterface::Interface()->SetShaders( m_aFxShaders[ a_uiShaders ].pVertexShader, m_aFxShaders[ a_uiShaders ].pPixelShader, m_aFxShaders[ a_uiShaders ].pInputLayout );
}
#endif // BARNYARD_COMMUNITY_PATCH

AGrassMaterial* AGrassShaderHAL::CreateMaterial( const TCHAR* a_szName )
{
	Validate();

	AGrassMaterialHAL* pMaterial = new AGrassMaterialHAL();
	pMaterial->SetShader( this );

	m_oOrderTable.RegisterMaterial( pMaterial );
	return pMaterial;
}

AGrassMesh* AGrassShaderHAL::CreateMesh( const TCHAR* a_szName )
{
	Validate();

	AGrassMeshHAL* pMesh = new AGrassMeshHAL();
	pMesh->SetOwnerShader( this );

	return pMesh;
}

void AGrassShaderHAL::UpdateAnimation()
{
	TFLOAT fCurrentTime = g_oSystemManager.GetScheduler()->GetTotalTime();
	g_fWindTime = fCurrentTime;

	TVector3 vecAnim = TVector3(
	    fCurrentTime * 0.70f * g_fAnimationSpeed,
	    fCurrentTime * 0.92f * g_fAnimationSpeed,
	    fCurrentTime * 0.20f * g_fAnimationSpeed
	);

	TMath::NormaliseAngle( vecAnim.x );
	TMath::NormaliseAngle( vecAnim.y );
	TMath::NormaliseAngle( vecAnim.z );

	g_vecAnimationDataOffset.x = TMath::Sin( vecAnim.x );
	g_vecAnimationDataOffset.y = TMath::Sin( vecAnim.y );
	g_vecAnimationDataOffset.z = TMath::Sin( vecAnim.z );
	g_vecAnimationDataOffset.Multiply( 0.075f );

	g_vecAnimOffset.x = g_vecAnimationDataOffset.x;
	g_vecAnimOffset.y = g_vecAnimationDataOffset.y;
	g_vecAnimOffset.z = g_vecAnimationDataOffset.z;
}
