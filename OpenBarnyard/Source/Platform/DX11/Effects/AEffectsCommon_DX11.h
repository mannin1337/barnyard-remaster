#pragma once
//-----------------------------------------------------------------------------
// The contract of the DX11 effects (BARNYARD_COMMUNITY_PATCH, DX11 only), shared by the
// post effects (AEffects_DX11), the shadow maps and lights (AShadowMaps_DX11, ADynamicLights_DX11)
// and the receivers in the world, skin and grass HALs:
//  - the register slots, so the effects never collide with the texture stages of the renderer;
//  - the constant buffer layouts shared by C++ and HLSL;
//  - the shadow pass flag;
//  - the light ID packing of TRenderPacket::SetUserData;
//  - the console variables: their names (the Graphics menu drives them by name), defaults and storage.
// Effects ported from ERRender (Barnyard-DX11 by InfiniteC0re, no licence): private use, never publish.
//
// Matrices in the constant buffers are TMatrix44 transposed (TMatrix44::Transpose of the row vector
// matrix), read in HLSL with the default column_major packing as mul( float4( v, 1.0f ), m ), like the
// game's shaders read their b0 constants.
//-----------------------------------------------------------------------------
#include <Math/TMatrix44.h>
#include <Math/TVector4.h>
#include <Render/TRenderContext.h>

#include <d3d11.h>

//-----------------------------------------------------------------------------
// Register slots. The texture stages own t0-t7 and s0-s7 (TRenderD3DInterface::UpdateSamplerStates
// sets the stage samplers again before draws), b0 is the shared ring of SetVec4InVSBuffer.
// Whoever binds a slot unbinds it when done (PSSetShaderResource( slot, TNULL )): a view still bound
// as a shader resource can't become a render target or depth buffer.
//-----------------------------------------------------------------------------
namespace AEffectsSlots
{
// Constant buffers (TConstantBuffer<T>, SetVSConstantBuffer / SetPSConstantBuffer)
constexpr UINT CB_SUN_SHADOWS    = 1; // AEffectsSunShadowsCB, receivers (PS), cbuffer ... : register( b1 )
constexpr UINT CB_DYNAMIC_LIGHTS = 2; // AEffectsDynamicLightsCB, receivers (PS), register( b2 )
constexpr UINT CB_POST           = 3; // AEffectsPostCB, fullscreen passes (PS), register( b3 )

// Shader resources
constexpr UINT T_SUN_SHADOW_MAP    = 8;  // Texture2DArray of the cascades, register( t8 )
constexpr UINT T_LIGHT_SHADOW_MAPS = 9;  // Texture2DArray of the spot lights, register( t9 )
constexpr UINT T_POST_0            = 10; // inputs of the fullscreen passes, register( t10 ) ... ( t13 )
constexpr UINT T_POST_1            = 11;
constexpr UINT T_POST_2            = 12;
constexpr UINT T_POST_3            = 13;
constexpr UINT T_FONT_ATLAS        = 14; // the SDF atlas of the HD fonts (AGUI2FontSDF_DX11), register( t14 )

// Samplers
constexpr UINT S_SUN_SHADOW   = 8;  // SamplerComparisonState of the cascades, register( s8 )
constexpr UINT S_LIGHT_SHADOW = 9;  // SamplerComparisonState of the spot lights, register( s9 )
constexpr UINT S_POINT_CLAMP  = 10; // fullscreen passes, register( s10 )
constexpr UINT S_LINEAR_CLAMP = 11; // fullscreen passes, register( s11 )
constexpr UINT S_FONT_ATLAS   = 12; // the SDF atlas of the HD fonts, register( s12 )
} // namespace AEffectsSlots

//-----------------------------------------------------------------------------
// Constant buffers
//-----------------------------------------------------------------------------

// b1: the sun shadows (CSM), cbuffer ShadowBuffer of ERRender's ShadowSampling.hlsli
struct AEffectsSunShadowsCB
{
	static constexpr TINT NUM_CASCADES = 3;

	Toshi::TMatrix44 amLightViewProjection[ NUM_CASCADES ]; // world space to the clip space of each cascade
	Toshi::TVector4  vCascadeSplits;                        // view depth where each cascade ends
	Toshi::TVector4  vShadowParams;                         // as cb_ShadowParams of ERRender: x receiver bias, y texel size, z debug cascade, w intensity
	Toshi::TVector4  vShadowFilterParams;                   // as cb_ShadowFilterParams of ERRender: x PCF radius, y receiver plane bias, w cascade blend
};

// b2: the dynamic lights, cbuffer DynamicGlowLights of ERRender's DynamicLights.hlsli
struct AEffectsDynamicLightsCB
{
	static constexpr TINT MAX_LIGHTS = Toshi::TLightIDList::MAX_NUM_LIGHTS;

	Toshi::TVector4  avPositionRadius[ MAX_LIGHTS ];      // xyz world position, w radius
	Toshi::TVector4  avDirectionCone[ MAX_LIGHTS ];       // xyz direction (from the light to the scene), w cos of the outer cone
	Toshi::TMatrix44 amLightViewProjection[ MAX_LIGHTS ]; // world space to the clip space of the spot light's shadow map
	Toshi::TVector4  avShadowParams[ MAX_LIGHTS ];        // x shadow map slice (< 0: none), y texel size, z bias, w strength
	Toshi::TVector4  avColour[ MAX_LIGHTS ];              // xyz colour, w volumetric intensity
	Toshi::TVector4  avIntensity[ MAX_LIGHTS ];           // x surface intensity, y bump scale, z cos of the inner cone
	Toshi::TVector4  vParams;                             // x number of lights
};

// b3: the fullscreen passes of AEffects, the meaning of the registers depends on the pass
struct AEffectsPostCB
{
	static constexpr TINT NUM_REGISTERS = 32;

	Toshi::TVector4 avParams[ NUM_REGISTERS ];
};

//-----------------------------------------------------------------------------
// The shadow pass. AShadowMaps sets ms_bActive while the casters render (the same calls as the main
// pass, ARenderer::RenderMainScene, with the light camera): the world, skin and grass HALs then draw
// depth only into the bound shadow map with MVP = ms_mLightProjection * packet model-view, and the
// materials skip their per-frame work (the UV animation of AWorldMaterialHAL::PreRender would run once
// a cascade). AEffects::RenderShadowPass binds the main targets and viewport again afterwards.
//-----------------------------------------------------------------------------
struct AEffectsShadowPass
{
	static TBOOL            ms_bActive;
	static TINT             ms_iSlice;           // cascade or spot light being rendered
	static Toshi::TMatrix44 ms_mLightProjection; // the projection of the light camera (not transposed)
};

//-----------------------------------------------------------------------------
// The lights of a packet: the meshes put the light IDs of the render context (GetLightIDs, set by the
// terrain for its cells) into TRenderPacket::SetUserData. Each ID is a byte XORed with 0xFF, so a packet
// whose user data was never set (TNULL) has no lights (-1 in every slot).
//-----------------------------------------------------------------------------
inline void* AEffectsPackLightIDs( const Toshi::TLightIDList& a_rcList )
{
	TSTATICASSERT( sizeof( void* ) == 4 && Toshi::TLightIDList::MAX_NUM_LIGHTS == 4 );

	TUINT32 uiPacked = 0;

	for ( TINT i = 0; i < Toshi::TLightIDList::MAX_NUM_LIGHTS; i++ )
		uiPacked |= TUINT32( TUINT8( a_rcList.aIDs[ i ] ) ^ 0xFFu ) << ( i * 8 );

	return reinterpret_cast<void*>( uintptr_t( uiPacked ) );
}

inline void AEffectsUnpackLightIDs( const void* a_pPacked, Toshi::TLightID a_aOutIDs[ Toshi::TLightIDList::MAX_NUM_LIGHTS ] )
{
	const TUINT32 uiPacked = TUINT32( reinterpret_cast<uintptr_t>( a_pPacked ) );

	for ( TINT i = 0; i < Toshi::TLightIDList::MAX_NUM_LIGHTS; i++ )
		a_aOutIDs[ i ] = Toshi::TLightID( TUINT8( ( uiPacked >> ( i * 8 ) ) & 0xFFu ) ^ 0xFFu );
}

//-----------------------------------------------------------------------------
// GPU timings (render.fx_timings 1): the passes between AEffects::BeginTiming and EndTiming,
// averaged over 120 frames into the log
//-----------------------------------------------------------------------------
enum AEffectsTiming : TINT
{
	AEFFECTS_TIMING_FRAME,     // AEffects::BeginFrame to the end of EndFrame: the whole main scene
	AEFFECTS_TIMING_SHADOWS,   // the sun and light shadow maps (AShadowMaps)
	AEFFECTS_TIMING_RESOLVE,   // the MSAA resolve of the scene
	AEFFECTS_TIMING_AO,        // ambient occlusion
	AEFFECTS_TIMING_BLOOM,     // glow bloom
	AEFFECTS_TIMING_SUNSHAFTS, // sun shafts
	AEFFECTS_TIMING_FOG,       // volumetric fog
	AEFFECTS_TIMING_SCENE,     // the main and overlay passes: after the shadow pass, before the resolve
	AEFFECTS_TIMING_NUMOF
};

//-----------------------------------------------------------------------------
// Console variables, registered by AEffects::RegisterSettings (APrimeDebug::RegisterSettings).
// The effects read the storage every frame, a change takes effect from the next frame on.
//-----------------------------------------------------------------------------
struct AEffectsSettings
{
	// Names, the Graphics menu finds them with PConsole::FindVariable
	static constexpr const TCHAR* MSAA                  = "render.msaa";                  // 1, 2, 4 or 8 samples
	static constexpr const TCHAR* ANISOTROPY            = "render.anisotropy";            // 1 (off) to 16
	static constexpr const TCHAR* ALPHA_COVERAGE        = "render.alpha_coverage";        // MSAA edges of the alpha tested pixels
	static constexpr const TCHAR* AO                    = "render.ao";                    // 0 off, 1 HBAO, 2 GTAO
	static constexpr const TCHAR* AO_QUALITY            = "render.ao_quality";            // 0 half, 1 full resolution
	static constexpr const TCHAR* AO_RADIUS             = "render.ao_radius";             // world units
	static constexpr const TCHAR* AO_INTENSITY          = "render.ao_intensity";
	static constexpr const TCHAR* AO_DEBUG              = "render.ao_debug";              // shows the occlusion alone
	static constexpr const TCHAR* BLOOM                 = "render.bloom";                 // glow bloom
	static constexpr const TCHAR* BLOOM_INTENSITY       = "render.bloom_intensity";
	static constexpr const TCHAR* BLOOM_LEVELS          = "render.bloom_levels";          // 1 to 5 blur levels
	static constexpr const TCHAR* DYNAMIC_LIGHTS        = "render.dynamic_lights";        // night lights (bike headlight)
	static constexpr const TCHAR* DYNAMIC_LIGHT_SHADOWS = "render.dynamic_light_shadows"; // 0 off, 1 512, 2 1024
	static constexpr const TCHAR* DYNAMIC_LIGHT_FLICKER = "render.dynamic_light_flicker";
	static constexpr const TCHAR* SUN_SHADOWS           = "render.sun_shadows";           // 0 off, 1 3x1024, 2 3x2048, 3 3x4096
	static constexpr const TCHAR* SUN_SHADOW_DISTANCE   = "render.sun_shadow_distance";
	static constexpr const TCHAR* SUN_SHADOW_INTENSITY  = "render.sun_shadow_intensity";
	static constexpr const TCHAR* SUN_DIR_OVERRIDE      = "render.sun_dir_override";      // ERRender's fixed sun direction
	static constexpr const TCHAR* SHADOW_OFFSCREEN      = "render.shadow_offscreen";      // models outside the view cast too
	static constexpr const TCHAR* SUN_SHAFTS            = "render.sun_shafts";
	static constexpr const TCHAR* SUN_SHAFTS_INTENSITY  = "render.sun_shafts_intensity";
	static constexpr const TCHAR* VOLUMETRIC_FOG        = "render.volumetric_fog";        // 0 off, 1 additive, 2 darken
	static constexpr const TCHAR* HD_FONTS              = "render.hd_fonts";
	static constexpr const TCHAR* FX_TIMINGS            = "render.fx_timings";            // GPU times of the passes in the log

	// Storage (defaults in AEffects_DX11.cpp)
	static TINT   ms_iMSAA;                // 4
	static TINT   ms_iAnisotropy;          // 16
	static TINT   ms_iAlphaCoverage;       // 1
	static TINT   ms_iAO;                  // 2 (GTAO: visible at the default intensity and half the cost of HBAO)
	static TINT   ms_iAOQuality;           // 0
	static TFLOAT ms_fAORadius;            // 0.7
	static TFLOAT ms_fAOIntensity;         // 1.0
	static TINT   ms_iAODebug;             // 0
	static TINT   ms_iBloom;               // 1
	static TFLOAT ms_fBloomIntensity;      // 0.715
	static TINT   ms_iBloomLevels;         // 3 (from half the size: ERRender's 2 from a quarter)
	static TINT   ms_iDynamicLights;       // 1
	static TINT   ms_iDynamicLightShadows; // 2
	static TINT   ms_iDynamicLightFlicker; // 1
	static TINT   ms_iSunShadows;          // 2
	static TFLOAT ms_fSunShadowDistance;   // 56
	static TFLOAT ms_fSunShadowIntensity;  // 0.8
	static TINT   ms_iSunDirOverride;      // 0
	static TINT   ms_iShadowOffscreen;     // 1
	static TINT   ms_iSunShafts;           // 0
	static TFLOAT ms_fSunShaftsIntensity;  // 0.04
	static TINT   ms_iVolumetricFog;       // 0
	static TINT   ms_iHDFonts;             // 1, 0 when Data\Fonts\CCThatsAllFolks.ttf isn't there (T2GUIFontSDF)
	static TINT   ms_iFXTimings;           // 0
};
