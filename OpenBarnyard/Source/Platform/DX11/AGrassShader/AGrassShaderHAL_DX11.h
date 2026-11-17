#pragma once
#include "Render/AGrassShader/AGrassShader.h"

#include <Render/TOrderTable.h>

#include <d3d11.h>

namespace Toshi
{
class TTextureResourceHAL;
}

extern MODAPI Toshi::TTextureResourceHAL* g_pGrassShaderTerrainTexture;

class MODAPI AGrassShaderHAL
    : public AGrassShader
{
public:
	TDECLARE_CLASS( AGrassShaderHAL, AGrassShader );

	enum CONSTANT_ : TINT
	{
		CONSTANT_MVP        = 0,
		CONSTANT_AMBIENT    = 4,
		CONSTANT_SHADOW     = 5,
		CONSTANT_ONE        = 6,
		CONSTANT_LAYER      = 7,
		CONSTANT_ANIMATION  = 8,
		CONSTANT_ALPHATEST  = 9,
		CONSTANT_FOG        = 10,
		CONSTANT_FOGCOLOUR  = 11,
		CONSTANT_STAGES     = 12,
		CONSTANT_MODEL      = 13, // BARNYARD_COMMUNITY_PATCH: model to world (4 registers), the receivers of the DX11 effects
	};

public:
	AGrassShaderHAL();
	~AGrassShaderHAL();

	//-----------------------------------------------------------------------------
	// Toshi::TShader
	//-----------------------------------------------------------------------------
	virtual void  Flush() OVERRIDE;
	virtual void  StartFlush() OVERRIDE;
	virtual void  EndFlush() OVERRIDE;
	virtual TBOOL Create() OVERRIDE;
	virtual TBOOL Validate() OVERRIDE;
	virtual void  Invalidate() OVERRIDE;
	virtual TBOOL TryInvalidate() OVERRIDE;
	virtual TBOOL TryValidate() OVERRIDE;
	virtual void  Render( Toshi::TRenderPacket* a_pRenderPacket ) OVERRIDE;

	//-----------------------------------------------------------------------------
	// AGrassShader
	//-----------------------------------------------------------------------------
	virtual AGrassMaterial* CreateMaterial( const TCHAR* a_szName ) OVERRIDE;
	virtual AGrassMesh*     CreateMesh( const TCHAR* a_szName ) OVERRIDE;

private:
	void UpdateAnimation();

private:
	Toshi::TOrderTable  m_oOrderTable;
	ID3D11VertexShader* m_pVertexShader;
	ID3D11PixelShader*  m_pPixelShader;
	ID3D11InputLayout*  m_pInputLayout;

#ifdef BARNYARD_COMMUNITY_PATCH
	// The DX11 effects: the receivers of the sun shadows and the lights and the casters of the shadow pass
	enum FXSHADERS_ : TUINT
	{
		FXSHADERS_SUN      = 1, // SUN_SHADOWS
		FXSHADERS_LIGHTS   = 2, // DYNAMIC_LIGHTS
		FXSHADERS_COVERAGE = 4, // ALPHA_COVERAGE: the grass under MSAA
		FXSHADERS_NUMOF    = 8, // 0: the shaders of the game
	};

	struct FxShaders
	{
		ID3D11VertexShader* pVertexShader = TNULL;
		ID3D11PixelShader*  pPixelShader  = TNULL;
		ID3D11InputLayout*  pInputLayout  = TNULL;
	};

	void SetFxShaders( TUINT a_uiShaders );

	FxShaders           m_aFxShaders[ FXSHADERS_NUMOF ];
	ID3D11VertexShader* m_pVertexShaderDepth = TNULL;
	ID3D11InputLayout*  m_pInputLayoutDepth  = TNULL;
	TUINT               m_uiFxFlags          = 0; // FXSHADERS_ the flush allows: the sun shadows, the lights of the packets
	TUINT               m_uiSetFxShaders     = 0; // the permutation that is set
#endif // BARNYARD_COMMUNITY_PATCH
};
