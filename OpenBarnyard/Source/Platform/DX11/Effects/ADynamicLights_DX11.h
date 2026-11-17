#pragma once
#include "AEffectsCommon_DX11.h"

#include <Render/TRenderContext.h>
#include <Render/TCameraObject.h>
#include <Platform/DX11/TConstantBuffer_DX11.h>

//-----------------------------------------------------------------------------
// Purpose: The dynamic lights (plan section 3 d), ported from ERRender's DynamicGlowLights (private use only,
// ERRender has no licence): the glow objects of AGlowViewport (the bike headlight) become spot lights with
// a shadow map each.
//
// - Each frame AShadowMaps::Render calls Update: the lights of the frame (the night only ones at night
//   only), their transforms, cones and flicker; then it renders the shadow maps of the nearest ones
//   (render.dynamic_light_shadows) and gives them back with SetShadow.
// - The world, skin and grass meshes put the light IDs of the render context (the terrain cells and the
//   model instances set them) into their packets: GetContextLights, TRenderPacket::SetUserData.
// - The receivers (DYNAMIC_LIGHTS permutation) take the lights of their packet with SetPacketLights: b2
//   (AEffectsDynamicLightsCB), the shadow maps at t9 / s9 (bound by AShadowMaps).
// Settings: render.dynamic_lights, render.dynamic_light_shadows, render.dynamic_light_flicker.
//-----------------------------------------------------------------------------
class ADynamicLights
{
public:
	static constexpr TINT MAX_FRAME_LIGHTS = 16; // lights of a frame
	static constexpr TINT MAX_SHADOWS      = Toshi::TLightIDList::MAX_NUM_LIGHTS;

	// A light of the frame
	struct Light
	{
		Toshi::TLightID                         iID;
		Toshi::TMatrix44                        oLightToWorld;
		Toshi::TMatrix44                        oWorldToLight;
		Toshi::TRenderContext::VIEWPORTPARAMS   oViewportParams;   // of the glow object
		Toshi::TRenderContext::PROJECTIONPARAMS oProjectionParams; // of the glow object
		Toshi::TRenderContext::CameraMode       eCameraMode;
		TFLOAT                                  fDistanceSq;       // to the camera
		TINT                                    iShadowSlice;      // -1: no shadow map this frame

		// The entry of AEffectsDynamicLightsCB
		Toshi::TVector4  vPositionRadius;
		Toshi::TVector4  vDirectionCone;
		Toshi::TMatrix44 oViewProjection; // transposed
		Toshi::TVector4  vShadowParams;
		Toshi::TVector4  vColour;
		Toshi::TVector4  vIntensity;
	};

	// The lights of this frame (nothing while render.dynamic_lights is 0), a_rcCameraPos for their distance
	static void Update( const Toshi::TVector3& a_rcCameraPos );
	static void Destroy();

	static TINT   GetNumLights();
	static Light& GetLight( TINT a_iIndex );

	// The shadow map of a light: slice of the array, size and the world to clip space matrix it was drawn with
	static void SetShadow( TINT a_iIndex, TINT a_iSlice, UINT a_uiSize, const Toshi::TMatrix44& a_rcViewProjection );

	// For the meshes: the light IDs of the current render context packed for TRenderPacket::SetUserData,
	// TNULL when there is none or the lights are off
	static void* GetContextLights();

	// For the receivers: the lights of a packet (its user data) into b2, TRUE if any of them is on this frame.
	// Call it before the draw, after the strip batch of the previous packet is flushed
	static TBOOL SetPacketLights( const void* a_pPackedLights );

	// For the post passes (the volumetric fog, ERRender's UploadVolumetricDynamicGlowLightsCBuffer): the lights
	// of the frame with a shadow map into b2, TRUE if there is any
	static TBOOL SetFrameLights();

	// The constant buffer of b2 (AShadowMaps binds it with the shadow maps)
	static Toshi::TConstantBufferGeneric* GetConstantBuffer();

	// TRUE while the frame has lights: the receivers of the main pass take them
	static TBOOL IsReceiver();
};
