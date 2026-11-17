#include "pch.h"
#include "ADynamicLights_DX11.h"
#include "AEffects_DX11.h"
#include "AShadowMaps_DX11.h"

#include "Render/AGlowViewport.h"
#include "Helpers/AGameTimeManager.h"

#include <Toshi/TSystem.h>
#include <Toshi/TScheduler.h>
#include <Render/TRenderInterface.h>
#include <Platform/DX11/TRenderInterface_DX11.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

//-----------------------------------------------------------------------------
// The defaults of ERRender's DynamicGlowLights.cpp
//-----------------------------------------------------------------------------
static constexpr TFLOAT LIGHT_INTENSITY            = 1.75f;
static constexpr TFLOAT LIGHT_VOLUMETRIC_INTENSITY = 0.3f;
static constexpr TFLOAT LIGHT_COLOUR[ 3 ]          = { 255.0f / 255.0f, 181.0f / 255.0f, 110.0f / 255.0f };
static constexpr TFLOAT LIGHT_SHADOW_INTENSITY     = 1.0f;
static constexpr TFLOAT LIGHT_SHADOW_BIAS          = 0.0015f;
static constexpr TFLOAT LIGHT_BUMP_SCALE           = 3.5f;
static constexpr TFLOAT LIGHT_FLICKER_SPEED        = 5.0f;
static constexpr TFLOAT LIGHT_FLICKER_STRENGTH     = 0.1f;

static ADynamicLights::Light                    s_aLights[ ADynamicLights::MAX_FRAME_LIGHTS ];
static TINT                                     s_iNumLights;
static TINT8                                    s_aiLightByID[ 256 ]; // index in s_aLights of a light ID, -1 if it isn't on
static TBOOL                                    s_bLightByIDValid;
static TUINT32                                  s_uiFrame = TUINT32( -1 ); // of the lights
static TConstantBuffer<AEffectsDynamicLightsCB> s_oLightsCB;

static void ResetLightMap()
{
	for ( TINT i = 0; i < TARRAYSIZE( s_aiLightByID ); i++ )
		s_aiLightByID[ i ] = -1;

	s_bLightByIDValid = TTRUE;
}

// ERRender's ComputeFlickerMultiplier: three sines of incommensurable frequencies, 1 +- strength
static TFLOAT ComputeFlicker()
{
	if ( !AEffectsSettings::ms_iDynamicLightFlicker )
		return 1.0f;

	const TFLOAT fTime = g_oSystemManager.GetScheduler()->GetTotalTime() * LIGHT_FLICKER_SPEED;
	const TFLOAT fWave = TMath::Sin( fTime * 1.00f ) * 0.50f + TMath::Sin( fTime * 2.71f ) * 0.30f + TMath::Sin( fTime * 7.33f ) * 0.20f;

	return TMath::Max( 0.0f, 1.0f + fWave * LIGHT_FLICKER_STRENGTH );
}

void ADynamicLights::Update( const TVector3& a_rcCameraPos )
{
	if ( !s_bLightByIDValid )
		ResetLightMap();

	for ( TINT i = 0; i < s_iNumLights; i++ )
		s_aiLightByID[ TUINT8( s_aLights[ i ].iID ) ] = -1;

	s_iNumLights = 0;
	s_uiFrame    = g_oSystemManager.GetFrameCount();

	if ( !AEffectsSettings::ms_iDynamicLights || !AGlowViewport::IsSingletonCreated() )
		return;

	// The night lights (the bike headlight) shine at night only: GetInfluencingLightIDs gives them to nothing by day
	const TBOOL  bNight   = AGameTimeManager::IsSingletonCreated() && g_pGameTimeMngr->IsItNight();
	const TFLOAT fFlicker = ComputeFlicker();

	// With render.fx_timings the log tells when the glow objects change (the bike race turns the headlight off
	// during its intro sequence)
	if ( AEffectsSettings::ms_iFXTimings )
	{
		static TINT s_iLoggedObjects = -1;
		static TINT s_iLoggedNight   = -1;
		TINT        iNumObjects      = 0;
		TINT        iNumEnabled      = 0;

		for ( AGlowViewport::GlowObject* pGlowObject = AGlowViewport::GetSingleton()->GetFirstUsedObject(); pGlowObject; pGlowObject = pGlowObject->m_pNextObject )
		{
			iNumObjects++;
			iNumEnabled += pGlowObject->IsEnabled() ? 1 : 0;
		}

		if ( iNumObjects * 100 + iNumEnabled != s_iLoggedObjects || TINT( bNight ) != s_iLoggedNight )
		{
			TINFO( "[lights] %d glow object(s), %d enabled, night %d\n", iNumObjects, iNumEnabled, TINT( bNight ) );
			s_iLoggedObjects = iNumObjects * 100 + iNumEnabled;
			s_iLoggedNight   = TINT( bNight );
		}
	}

	for ( AGlowViewport::GlowObject* pGlowObject = AGlowViewport::GetSingleton()->GetFirstUsedObject(); pGlowObject && s_iNumLights < MAX_FRAME_LIGHTS; pGlowObject = pGlowObject->m_pNextObject )
	{
		if ( !pGlowObject->IsEnabled() || ( pGlowObject->IsNightLight() && !bNight ) || pGlowObject->m_iID < 0 )
			continue;

		Light& rLight = s_aLights[ s_iNumLights ];

		if ( !pGlowObject->GetWorldTransform( rLight.oLightToWorld ) )
			continue;

		rLight.iID               = pGlowObject->m_iID;
		rLight.oViewportParams   = pGlowObject->m_oViewportParams;
		rLight.oProjectionParams = pGlowObject->m_oProjectionParams;
		rLight.eCameraMode       = pGlowObject->m_eCameraMode;
		rLight.iShadowSlice      = -1;
		rLight.oWorldToLight.InvertOrthogonal( rLight.oLightToWorld );

		const TVector3& rcPosition = rLight.oLightToWorld.GetTranslation3();
		rLight.fDistanceSq         = TVector3::DistanceSq( rcPosition, a_rcCameraPos );

		// The cone of ERRender's FillDynamicGlowLightCBufferEntry
		TVector4 vDirection = rLight.oLightToWorld.AsBasisVector4( BASISVECTOR_FORWARD );
		vDirection.w        = 0.0f;
		vDirection.Normalise();

		const TFLOAT fRadius     = TMath::Max( rLight.oProjectionParams.m_fFarClip, 1.0f );
		const TFLOAT fHalfWidth  = rLight.oViewportParams.fWidth * 0.25f;
		const TFLOAT fHalfHeight = rLight.oViewportParams.fHeight * 0.25f;
		const TFLOAT fTanOuter   = TMath::Max( fHalfWidth / TMath::Max( rLight.oProjectionParams.m_Proj.x, 0.001f ), fHalfHeight / TMath::Max( rLight.oProjectionParams.m_Proj.y, 0.001f ) );
		const TFLOAT fTanInner   = fTanOuter * 0.4f;

		rLight.vPositionRadius = TVector4( rcPosition.x, rcPosition.y, rcPosition.z, fRadius );
		rLight.vDirectionCone  = TVector4( vDirection.x, vDirection.y, vDirection.z, 1.0f / TMath::Sqrt( 1.0f + fTanOuter * fTanOuter ) );
		rLight.vColour         = TVector4( LIGHT_COLOUR[ 0 ], LIGHT_COLOUR[ 1 ], LIGHT_COLOUR[ 2 ], LIGHT_VOLUMETRIC_INTENSITY * fFlicker );
		rLight.vIntensity      = TVector4( LIGHT_INTENSITY * fFlicker, LIGHT_BUMP_SCALE, 1.0f / TMath::Sqrt( 1.0f + fTanInner * fTanInner ), 0.0f );
		rLight.vShadowParams   = TVector4( -1.0f, 0.0f, 0.0f, 0.0f );

		// The projection decides where the light reaches (ERRender leaves it zero without a shadow map)
		TMatrix44 oProjection;

		if ( rLight.eCameraMode == TRenderContext::CameraMode_Orthographic )
			TRenderContext::ComputeOrthographicProjection( oProjection, rLight.oViewportParams, rLight.oProjectionParams );
		else
			TRenderContext::ComputePerspectiveProjection( oProjection, rLight.oViewportParams, rLight.oProjectionParams );

		TMatrix44 oViewProjection;
		oViewProjection.Multiply( oProjection, rLight.oWorldToLight );
		rLight.oViewProjection.Transpose( oViewProjection );

		s_aiLightByID[ TUINT8( rLight.iID ) ] = TINT8( s_iNumLights );
		s_iNumLights++;
	}
}

void ADynamicLights::SetShadow( TINT a_iIndex, TINT a_iSlice, UINT a_uiSize, const TMatrix44& a_rcViewProjection )
{
	TASSERT( a_iIndex >= 0 && a_iIndex < s_iNumLights );

	Light& rLight       = s_aLights[ a_iIndex ];
	rLight.iShadowSlice = a_iSlice;
	rLight.vShadowParams = TVector4( TFLOAT( a_iSlice ), 1.0f / TFLOAT( a_uiSize ), LIGHT_SHADOW_BIAS, LIGHT_SHADOW_INTENSITY );
	rLight.oViewProjection.Transpose( a_rcViewProjection );
}

void ADynamicLights::Destroy()
{
	if ( s_oLightsCB.IsCreated() )
		s_oLightsCB.Destroy();

	s_iNumLights = 0;
	s_uiFrame    = TUINT32( -1 );
	ResetLightMap();
}

TINT ADynamicLights::GetNumLights()
{
	return s_iNumLights;
}

ADynamicLights::Light& ADynamicLights::GetLight( TINT a_iIndex )
{
	TASSERT( a_iIndex >= 0 && a_iIndex < s_iNumLights );
	return s_aLights[ a_iIndex ];
}

void* ADynamicLights::GetContextLights()
{
	if ( !AEffectsSettings::ms_iDynamicLights || s_iNumLights == 0 )
		return TNULL;

	const TLightIDList& rcIDs = TRenderInterface::GetSingleton()->GetCurrentContext()->GetLightIDs();

	if ( rcIDs.aIDs[ 0 ] < 0 && rcIDs.aIDs[ 1 ] < 0 && rcIDs.aIDs[ 2 ] < 0 && rcIDs.aIDs[ 3 ] < 0 )
		return TNULL;

	return AEffectsPackLightIDs( rcIDs );
}

TBOOL ADynamicLights::SetPacketLights( const void* a_pPackedLights )
{
	if ( !a_pPackedLights || s_iNumLights == 0 || !s_oLightsCB.IsCreated() )
		return TFALSE;

	TLightID aIDs[ TLightIDList::MAX_NUM_LIGHTS ];
	AEffectsUnpackLightIDs( a_pPackedLights, aIDs );

	AEffectsDynamicLightsCB oCB = {};
	TINT                    iNumLights = 0;

	for ( TINT i = 0; i < TLightIDList::MAX_NUM_LIGHTS; i++ )
	{
		if ( aIDs[ i ] < 0 )
			continue;

		// A light twice in the list would light the mesh twice
		TBOOL bDuplicate = TFALSE;

		for ( TINT k = 0; k < i; k++ )
			bDuplicate |= ( aIDs[ k ] == aIDs[ i ] );

		const TINT iIndex = s_aiLightByID[ TUINT8( aIDs[ i ] ) ];

		if ( bDuplicate || iIndex < 0 )
			continue;

		const Light& rcLight = s_aLights[ iIndex ];

		oCB.avPositionRadius[ iNumLights ]      = rcLight.vPositionRadius;
		oCB.avDirectionCone[ iNumLights ]       = rcLight.vDirectionCone;
		oCB.amLightViewProjection[ iNumLights ] = rcLight.oViewProjection;
		oCB.avShadowParams[ iNumLights ]        = rcLight.vShadowParams;
		oCB.avColour[ iNumLights ]              = rcLight.vColour;
		oCB.avIntensity[ iNumLights ]           = rcLight.vIntensity;
		iNumLights++;
	}

	if ( iNumLights == 0 )
		return TFALSE;

	oCB.vParams = TVector4( TFLOAT( iNumLights ), 0.0f, 0.0f, 0.0f );
	s_oLightsCB.Set( oCB );
	return TTRUE;
}

TBOOL ADynamicLights::SetFrameLights()
{
	if ( s_iNumLights == 0 || !s_oLightsCB.IsCreated() )
		return TFALSE;

	AEffectsDynamicLightsCB oCB = {};
	TINT                    iNumLights = 0;

	for ( TINT i = 0; i < s_iNumLights && iNumLights < AEffectsDynamicLightsCB::MAX_LIGHTS; i++ )
	{
		const Light& rcLight = s_aLights[ i ];

		if ( rcLight.iShadowSlice < 0 )
			continue;

		oCB.avPositionRadius[ iNumLights ]      = rcLight.vPositionRadius;
		oCB.avDirectionCone[ iNumLights ]       = rcLight.vDirectionCone;
		oCB.amLightViewProjection[ iNumLights ] = rcLight.oViewProjection;
		oCB.avShadowParams[ iNumLights ]        = rcLight.vShadowParams;
		oCB.avColour[ iNumLights ]              = rcLight.vColour;
		oCB.avIntensity[ iNumLights ]           = rcLight.vIntensity;
		iNumLights++;
	}

	oCB.vParams = TVector4( TFLOAT( iNumLights ), 0.0f, 0.0f, 0.0f );
	s_oLightsCB.Set( oCB );
	return iNumLights > 0;
}

TConstantBufferGeneric* ADynamicLights::GetConstantBuffer()
{
	if ( !s_oLightsCB.IsCreated() )
		s_oLightsCB.Create();

	return s_oLightsCB.IsCreated() ? &s_oLightsCB : TNULL;
}

TBOOL ADynamicLights::IsReceiver()
{
	return s_iNumLights > 0 && s_uiFrame == g_oSystemManager.GetFrameCount() && s_oLightsCB.IsCreated() && AShadowMaps::IsReceiverPass();
}
