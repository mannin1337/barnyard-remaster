#pragma once
#include "AGrassMaterial.h"
#include "AGrassMesh.h"

#include <Toshi/Toshi.h>
#include <Render/TShader.h>

class MODAPI AGrassShader
    : public Toshi::TShader
    , public Toshi::TSingleton<AGrassShader>
{
public:
	TDECLARE_CLASS( AGrassShader, Toshi::TShader );

public:
	AGrassShader();
	~AGrassShader();

	//-----------------------------------------------------------------------------
	// Own methods
	//-----------------------------------------------------------------------------
	virtual void            EnableRenderEnvMap( TBOOL a_bEnable );
	virtual TBOOL           IsHighEndSkinning();
	virtual void            EnableHighEndSkinning( TBOOL a_bEnable );
	virtual TBOOL           IsCapableHighEndSkinning();
	virtual TBOOL           IsLightScattering();
	virtual void            SetLightScattering( TBOOL a_bEnable );
	virtual TBOOL           IsAlphaBlendMaterial();
	virtual void            SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial );
	virtual AGrassMaterial* CreateMaterial( const TCHAR* a_szName ) = 0;
	virtual AGrassMesh*     CreateMesh( const TCHAR* a_szName )     = 0;

#ifdef BARNYARD_COMMUNITY_PATCH
	//-----------------------------------------------------------------------------
	// Grass density (render.grass_density, the Detail page of the options).
	// NOTE: not in the original. The grass is a shell: the ground pass and up to 5 layers drawn above it, fewer
	// the further the mesh is (5 within 20 units, down to 1 at 50, none beyond 100). The density sets the number of
	// layers and scales these distances. Normal is the original. Read by the DX8 and DX11 shaders every frame
	//-----------------------------------------------------------------------------
	enum DENSITY : TINT
	{
		DENSITY_OFF,    // only the ground pass
		DENSITY_LOW,    // 3 layers, 60% of the distances
		DENSITY_NORMAL, // 5 layers (original)
		DENSITY_HIGH,   // 8 layers, 150%
		DENSITY_ULTRA,  // 10 layers, 200%
		DENSITY_NUMOF
	};

	inline static TINT ms_iDensity = DENSITY_NORMAL;

	// Number of layers drawn above a mesh this far from the camera (nearest edge of its bounding sphere, XZ)
	static TINT GetNumLayers( TFLOAT a_fDistanceToCamera );

	// Index of the texture of layer a_iLayer (1 to a_iNumLayers) in g_aGrassLayers. Up to 5 layers it's the layer
	// itself (original), more layers share the 6 textures bottom to top
	static TINT GetLayerTexture( TINT a_iLayer, TINT a_iNumLayers );
#endif // BARNYARD_COMMUNITY_PATCH

private:
	TUINT  m_Unk1;
	TFLOAT m_Unk2;
	TUINT  m_Unk3;
};
