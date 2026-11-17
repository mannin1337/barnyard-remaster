#include "pch.h"
#include "AGrassShader.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_NORUNTIME( AGrassShader );

// $Barnyard: FUNCTION 005f7a40
AGrassShader::AGrassShader()
    : m_Unk2( 0.5f )
    , m_Unk3( 0 )
{
}

// $Barnyard: FUNCTION 005f7a70
// $Barnyard: FUNCTION 005f7bc0
AGrassShader::~AGrassShader()
{
}

void AGrassShader::EnableRenderEnvMap( TBOOL a_bEnable )
{
}

TBOOL AGrassShader::IsHighEndSkinning()
{
	return TTRUE;
}

void AGrassShader::EnableHighEndSkinning( TBOOL a_bEnable )
{
}

TBOOL AGrassShader::IsCapableHighEndSkinning()
{
	return TTRUE;
}

TBOOL AGrassShader::IsLightScattering()
{
	return TFALSE;
}

void AGrassShader::SetLightScattering( TBOOL a_bEnable )
{
}

TBOOL AGrassShader::IsAlphaBlendMaterial()
{
	return TFALSE;
}

void AGrassShader::SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial )
{
}

#ifdef BARNYARD_COMMUNITY_PATCH

// The layers and the distance scale of each density
static constexpr struct
{
	TINT   iMaxLayers;
	TFLOAT fDistanceScale;
} s_aDensities[ AGrassShader::DENSITY_NUMOF ] = {
	{ 0, 1.0f },
	{ 3, 0.6f },
	{ 5, 1.0f },
	{ 8, 1.5f },
	{ 10, 2.0f },
};

TINT AGrassShader::GetNumLayers( TFLOAT a_fDistanceToCamera )
{
	TINT iDensity = ms_iDensity;
	TMath::Clip( iDensity, 0, TINT( DENSITY_NUMOF - 1 ) );

	const auto&  rcDensity = s_aDensities[ iDensity ];
	const TINT   iMax      = rcDensity.iMaxLayers;
	const TFLOAT fScale    = rcDensity.fDistanceScale;

	// The original: all of them within 20, one less every 10 after it, 1 from 50, none beyond 100
	if ( iMax == 0 || a_fDistanceToCamera > 100.0f * fScale )
		return 0;

	if ( a_fDistanceToCamera <= 20.0f * fScale )
		return iMax;

	if ( a_fDistanceToCamera >= 50.0f * fScale || iMax <= 2 )
		return 1;

	const TFLOAT fStep = ( 30.0f * fScale ) / TFLOAT( iMax - 2 );
	return iMax - 1 - TMath::Min( TINT( ( a_fDistanceToCamera - 20.0f * fScale ) / fStep ), iMax - 2 );
}

TINT AGrassShader::GetLayerTexture( TINT a_iLayer, TINT a_iNumLayers )
{
	// g_aGrassLayers has 6 (1-6, two of each texture from the densest to the sparsest)
	if ( a_iNumLayers <= 5 )
		return a_iLayer;

	return 1 + ( ( a_iLayer - 1 ) * 6 ) / a_iNumLayers;
}

#endif // BARNYARD_COMMUNITY_PATCH
