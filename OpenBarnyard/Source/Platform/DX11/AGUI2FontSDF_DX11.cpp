#include "pch.h"
#include "AGUI2FontSDF_DX11.h"
#include "AGUI2Renderer_DX11.h"
#include "Effects/AEffectsCommon_DX11.h"

#include <GUI/T2GUI.h>
#include <Platform/DX11/TRenderInterface_DX11.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

// The vertex shader of the fixed function pipeline (the GUI's transform), the pixel shader draws the look of the
// atlas glyphs from the distance: the fill in the vertex colour with an edge one screen pixel wide, under it the
// black outline and its soft shadow, the vertex alpha over all of it (the atlas texture modulated by the vertex colour)
static const TCHAR s_szShader[] = R"(
cbuffer Constants : register( b0 )
{
	row_major float4x4 g_mWorldViewProjection;
	float4             g_vParams;  // alpha test (xyz)
	float4             g_vSDF;     // x, y: distance from the TTF outline = texel * x + y (font pixels, positive inside), z: how far the fill grows
	float4             g_vOutline; // x: alpha of the outline at the fill, y: less per font pixel, z to w: fades out (font pixels from the fill)
};

struct VS_IN
{
	float3 vPosition : POSITION;
	float4 vColour : COLOR;
	float2 vUV : TEXCOORD;
};

struct PS_IN
{
	float4 vPosition : SV_POSITION;
	float4 vColour : COLOR;
	float2 vUV : TEXCOORD;
};

PS_IN vs_main( VS_IN In )
{
	PS_IN Out;
	Out.vPosition = mul( float4( In.vPosition, 1.0f ), g_mWorldViewProjection );
	Out.vColour   = In.vColour;
	Out.vUV       = In.vUV;
	return Out;
}

Texture2D    g_Atlas : register( t14 );
SamplerState g_AtlasSampler : register( s12 );

float4 ps_main( PS_IN In ) : SV_TARGET
{
	// Font pixels from the edge of the fill (positive inside) and per screen pixel
	float fDistance = g_Atlas.Sample( g_AtlasSampler, In.vUV ).r * g_vSDF.x + g_vSDF.y + g_vSDF.z;
	float fPixel    = max( fwidth( fDistance ), 0.001f );

	float fFill    = saturate( fDistance / fPixel + 0.5f );
	float fOutside = max( -fDistance, 0.0f );
	float fOutline = saturate( g_vOutline.x - g_vOutline.y * fOutside ) * ( 1.0f - smoothstep( g_vOutline.z, g_vOutline.w, fOutside ) );

	float  fAlpha  = fFill + fOutline * ( 1.0f - fFill );
	float4 vColour = float4( In.vColour.rgb * ( fFill / max( fAlpha, 0.0001f ) ), fAlpha * In.vColour.a );

	AlphaTest( vColour.a, g_vParams );
	return vColour;
}
)";

struct SDFAtlas
{
	ID3D11Texture2D*          pTexture;
	ID3D11ShaderResourceView* pSRV;
};

static ID3D11VertexShader* s_pVertexShader;
static ID3D11PixelShader*  s_pPixelShader;
static ID3D11InputLayout*  s_pInputLayout;
static ID3D11SamplerState* s_pSampler;
static TBOOL               s_bShadersFailed;

static AGUI2RendererDX8::Vertex s_aVertices[ T2GUIFontSDF::MAX_QUADS * 6 ];

AGUI2FontSDFDX11& AGUI2FontSDFDX11::GetSingleton()
{
	static AGUI2FontSDFDX11 s_oSingleton;
	return s_oSingleton;
}

void AGUI2FontSDFDX11::Install()
{
	T2GUIFontSDF::SetRenderer( &GetSingleton() );
}

void AGUI2FontSDFDX11::Uninstall()
{
	if ( T2GUIFontSDF::GetRenderer() == &GetSingleton() )
		T2GUIFontSDF::SetRenderer( TNULL );

	GetSingleton().DestroyShaders();
}

TBOOL AGUI2FontSDFDX11::IsEnabled()
{
	return AEffectsSettings::ms_iHDFonts != 0;
}

TBOOL AGUI2FontSDFDX11::CreateShaders()
{
	if ( s_pPixelShader )
		return TTRUE;

	if ( s_bShadersFailed )
		return TFALSE;

	auto      pRender = TRenderD3DInterface::Interface();
	ID3DBlob* pVSBlob = TRenderD3DInterface::CompileShader( s_szShader, "vs_main", "vs_4_0", TNULL );
	ID3DBlob* pPSBlob = TRenderD3DInterface::CompileShader( s_szShader, "ps_main", "ps_4_0", TNULL );

	if ( pVSBlob && pPSBlob )
	{
		const D3D11_INPUT_ELEMENT_DESC aLayout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		TRenderD3DInterface::CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &s_pVertexShader );
		TRenderD3DInterface::CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), &s_pPixelShader );
		pRender->GetDevice()->CreateInputLayout( aLayout, TARRAYSIZE( aLayout ), pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &s_pInputLayout );
	}

	if ( pVSBlob ) pVSBlob->Release();
	if ( pPSBlob ) pPSBlob->Release();

	D3D11_SAMPLER_DESC oSamplerDesc = {};
	oSamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	oSamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
	oSamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
	oSamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
	oSamplerDesc.MaxAnisotropy      = 1;
	oSamplerDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
	oSamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;
	pRender->GetDevice()->CreateSamplerState( &oSamplerDesc, &s_pSampler );

	if ( !s_pVertexShader || !s_pPixelShader || !s_pInputLayout || !s_pSampler )
	{
		TERROR( "HD fonts: the SDF shaders can't be created, the fonts are drawn from their atlases\n" );
		DestroyShaders();
		s_bShadersFailed = TTRUE;
		return TFALSE;
	}

	return TTRUE;
}

void AGUI2FontSDFDX11::DestroyShaders()
{
	if ( s_pVertexShader ) s_pVertexShader->Release();
	if ( s_pPixelShader ) s_pPixelShader->Release();
	if ( s_pInputLayout ) s_pInputLayout->Release();
	if ( s_pSampler ) s_pSampler->Release();

	s_pVertexShader = TNULL;
	s_pPixelShader  = TNULL;
	s_pInputLayout  = TNULL;
	s_pSampler      = TNULL;
}

void* AGUI2FontSDFDX11::CreateAtlas( TUINT a_uiWidth, TUINT a_uiHeight )
{
	auto pRender = TRenderD3DInterface::Interface();

	if ( !pRender || !pRender->GetDevice() )
		return TNULL;

	D3D11_TEXTURE2D_DESC oDesc = {};
	oDesc.Width                = a_uiWidth;
	oDesc.Height               = a_uiHeight;
	oDesc.MipLevels            = 1;
	oDesc.ArraySize            = 1;
	oDesc.Format               = DXGI_FORMAT_R8_UNORM;
	oDesc.SampleDesc.Count     = 1;
	oDesc.Usage                = D3D11_USAGE_DEFAULT;
	oDesc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;

	// Cleared: between the glyphs is "far outside", what the bilinear filter reads at their edges
	void* pZeros = TMalloc( a_uiWidth * a_uiHeight );
	TUtil::MemClear( pZeros, a_uiWidth * a_uiHeight );

	D3D11_SUBRESOURCE_DATA oData = { pZeros, a_uiWidth, 0 };
	SDFAtlas               oAtlas = {};

	if ( FAILED( pRender->GetDevice()->CreateTexture2D( &oDesc, &oData, &oAtlas.pTexture ) ) ||
	     FAILED( pRender->GetDevice()->CreateShaderResourceView( oAtlas.pTexture, TNULL, &oAtlas.pSRV ) ) )
	{
		if ( oAtlas.pTexture ) oAtlas.pTexture->Release();
		TFree( pZeros );
		return TNULL;
	}

	TFree( pZeros );
	return new SDFAtlas( oAtlas );
}

void AGUI2FontSDFDX11::UpdateAtlas( void* a_pAtlas, TUINT a_uiX, TUINT a_uiY, TUINT a_uiWidth, TUINT a_uiHeight, const TUINT8* a_pData )
{
	SDFAtlas* pAtlas = TSTATICCAST( SDFAtlas, a_pAtlas );

	if ( !pAtlas )
		return;

	D3D11_BOX oBox = { a_uiX, a_uiY, 0, a_uiX + a_uiWidth, a_uiY + a_uiHeight, 1 };
	TRenderD3DInterface::Interface()->GetDeviceContext()->UpdateSubresource( pAtlas->pTexture, 0, &oBox, a_pData, a_uiWidth, 0 );
}

void AGUI2FontSDFDX11::DestroyAtlas( void* a_pAtlas )
{
	SDFAtlas* pAtlas = TSTATICCAST( SDFAtlas, a_pAtlas );

	if ( !pAtlas )
		return;

	if ( auto pRender = TRenderD3DInterface::Interface() )
		pRender->InvalidateShaderResource( pAtlas->pSRV );

	pAtlas->pSRV->Release();
	pAtlas->pTexture->Release();
	delete pAtlas;
}

void AGUI2FontSDFDX11::RenderQuads( void* a_pAtlas, const T2GUIFontSDFStyle& a_rcStyle, const T2GUIFontSDFQuad* a_pQuads, TINT a_iNumQuads, TUINT32 a_uiColour )
{
	SDFAtlas* pAtlas = TSTATICCAST( SDFAtlas, a_pAtlas );

	if ( !pAtlas || a_iNumQuads <= 0 || !CreateShaders() )
		return;

	auto pRender = TRenderD3DInterface::Interface();

	// The transform of the element being drawn, as RenderRectangle has it
	TSTATICCAST( AGUI2RendererDX8, T2GUI::GetRenderer() )->ApplyTransform();

	TMatrix44 oWorldView;
	oWorldView.Multiply( pRender->GetTransform( TRenderD3DInterface::TRANSFORM_VIEW ), pRender->GetTransform( TRenderD3DInterface::TRANSFORM_WORLD ) );

	TMatrix44 oWorldViewProjection;
	oWorldViewProjection.Multiply( pRender->GetTransform( TRenderD3DInterface::TRANSFORM_PROJECTION ), oWorldView );

	const TVector4 avConstants[ 3 ] = {
		pRender->GetAlphaTestParams(),
		TVector4( a_rcStyle.fDistanceScale, a_rcStyle.fDistanceBias, a_rcStyle.fFillGrow, 0.0f ),
		TVector4( a_rcStyle.fOutlineAlpha, a_rcStyle.fOutlineSlope, a_rcStyle.fOutlineFadeStart, a_rcStyle.fOutlineFadeEnd ),
	};

	pRender->SetShaders( s_pVertexShader, s_pPixelShader, s_pInputLayout );
	pRender->SetVec4InVSBuffer( 0, &oWorldViewProjection, 4 );
	pRender->SetVec4InVSBuffer( 4, avConstants, TARRAYSIZE( avConstants ) );
	pRender->PSSetShaderResource( AEffectsSlots::T_FONT_ATLAS, pAtlas->pSRV );
	pRender->PSSetSampler( AEffectsSlots::S_FONT_ATLAS, s_pSampler );

	// Two triangles a quad, in the order of the quads
	const TINT   iNumQuads = TMath::Min( a_iNumQuads, T2GUIFontSDF::MAX_QUADS );
	const TFLOAT fZ        = AGUI2RendererDX8::sm_fZCoordinate;

	for ( TINT i = 0; i < iNumQuads; i++ )
	{
		const T2GUIFontSDFQuad&   rcQuad    = a_pQuads[ i ];
		AGUI2RendererDX8::Vertex* pVertices = s_aVertices + i * 6;

		const AGUI2RendererDX8::Vertex aCorners[ 4 ] = {
			{ { rcQuad.afPos[ 0 ], rcQuad.afPos[ 1 ], fZ }, a_uiColour, { rcQuad.afUV[ 0 ], rcQuad.afUV[ 1 ] } },
			{ { rcQuad.afPos[ 2 ], rcQuad.afPos[ 1 ], fZ }, a_uiColour, { rcQuad.afUV[ 2 ], rcQuad.afUV[ 1 ] } },
			{ { rcQuad.afPos[ 0 ], rcQuad.afPos[ 3 ], fZ }, a_uiColour, { rcQuad.afUV[ 0 ], rcQuad.afUV[ 3 ] } },
			{ { rcQuad.afPos[ 2 ], rcQuad.afPos[ 3 ], fZ }, a_uiColour, { rcQuad.afUV[ 2 ], rcQuad.afUV[ 3 ] } },
		};

		pVertices[ 0 ] = aCorners[ 0 ];
		pVertices[ 1 ] = aCorners[ 1 ];
		pVertices[ 2 ] = aCorners[ 2 ];
		pVertices[ 3 ] = aCorners[ 2 ];
		pVertices[ 4 ] = aCorners[ 1 ];
		pVertices[ 5 ] = aCorners[ 3 ];
	}

	pRender->DrawNonIndexedImmediate( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, s_aVertices, iNumQuads * 6, sizeof( AGUI2RendererDX8::Vertex ) );

	// The GUI goes on with the fixed function shader, the atlas leaves its slot
	pRender->PSSetShaderResource( AEffectsSlots::T_FONT_ATLAS, TNULL );
	pRender->SetFixedFunctionShader();
}
