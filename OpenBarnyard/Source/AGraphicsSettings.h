#pragma once

#ifdef BARNYARD_COMMUNITY_PATCH

#  include <Render/TRenderInterface.h>

//-----------------------------------------------------------------------------
// Purpose: the settings of the Graphics and Effects pages of the options (AGraphicsOptionsState).
// NOTE: not in the original. Retail reads its display settings from the registry (HKEY_CURRENT_USER\Software\
// THQ\Barnyard, written by its setup program; AOptions reads Width, Height and RealForcedWindowed at boot). The
// pages keep those names and the mod's Borderless and HDTextures (AMaterialLibrary), and add DWORD values for the
// settings that are console variables (CONSOLE_SETTINGS): the menu sets the variables by name, so the same pages
// work with whatever the backend registers (DX8: render.msaa, render.anisotropy, render.vsync, render.refresh_rate
// in APrimeDebug; DX11: render.msaa, render.anisotropy and the effects). Rows of variables that don't exist are
// hidden. Only the values the pages change are written.
//
// Boot: Load reads the display values before the console is created; ApplySavedConsoleSettings sets the console
// variables from the registry right after APrimeDebug registered them, so the dev config and the command line
// ("+render.msaa 4", "+game.fps_limit 60") still win.
//
// Applied right away: the console variables (their change callbacks: on DX8 a Reset of the device for the
// multisampling, vsync and refresh rate, the texture filtering from the next frame, the frame rate limit), the
// size and the mode of the display through a Reset at the start of the next frame (ARenderer::RequestDisplayChange).
// A new aspect ratio only outside of a game (the frontend); HD textures from the next start. On DX8 vsync and the
// refresh rate only matter for a fullscreen display: windows present unsynced like retail, the FPS limit caps them.
//-----------------------------------------------------------------------------
class AGraphicsSettings
{
public:
	enum DISPLAYMODE : TINT
	{
		DISPLAYMODE_FULLSCREEN,
		DISPLAYMODE_BORDERLESS, // a window without a caption that covers the monitor
		DISPLAYMODE_WINDOWED,
		DISPLAYMODE_NUMOF
	};

	// The settings that are no console variables
	struct Display
	{
		TINT iWidth       = 800;
		TINT iHeight      = 600;
		TINT iDisplayMode = DISPLAYMODE_FULLSCREEN;

		TBOOL operator==( const Display& ) const = default;
	};

	// A setting kept in a console variable and saved in the registry
	struct ConsoleSetting
	{
		const TCHAR* szVariable;
		const TCHAR* szRegistryValue;
	};

	static constexpr ConsoleSetting CONSOLE_SETTINGS[] = {
		{ "render.refresh_rate", "RefreshRate" },
		{ "render.vsync", "VSync" },
		{ "game.fps_limit", "FPSLimit" },
		{ "render.msaa", "AntiAliasing" },
		{ "render.anisotropy", "Anisotropy" },
		{ "render.draw_distance", "DrawDistance" },
		{ "render.grass_density", "GrassDensity" },
		// The effects of the DX11 backend (names fixed by its port plan, it registers them)
		{ "render.ao", "AmbientOcclusion" },
		{ "render.ao_quality", "AmbientOcclusionQuality" },
		{ "render.bloom", "Bloom" },
		{ "render.dynamic_lights", "DynamicLights" },
		{ "render.dynamic_light_shadows", "DynamicLightShadows" },
		{ "render.sun_shadows", "SunShadows" },
		{ "render.sun_shafts", "SunShafts" },
		{ "render.volumetric_fog", "VolumetricFog" },
	};

public:
	// Reads the saved display settings. Called at boot before the console is created and before the display
	static void Load();

	// Sets the console variables of CONSOLE_SETTINGS from the registry. Called by APrimeDebug::CreateConsole right
	// after the settings are registered, before the dev config and the command line run
	static void ApplySavedConsoleSettings();

	// The saved display settings, which the next start uses
	static const Display& GetSavedDisplay() { return ms_oSavedDisplay; }

	// Saves a_rDisplay and changes the display of the running game to it when it can (CanApplyDisplayNow).
	// Returns TTRUE if the display is changed now (at the start of the next frame)
	static TBOOL ApplyDisplay( const Display& a_rDisplay );

	// TTRUE if a_rDisplay can be applied without a restart: a mode of that size exists, and the aspect ratio stays
	// the same or the game is in the frontend
	static TBOOL CanApplyDisplayNow( const Display& a_rDisplay );

	// TTRUE if the display of the running game isn't a_rDisplay (the mode isn't compared while windowed is forced)
	static TBOOL IsDisplayDifferent( const Display& a_rDisplay );

	// The display is windowed whatever the setting is: "-windowed", "-background" or a debugger (AOptions::IsWindowed)
	static TBOOL IsWindowedForced();

	// Console variables by name: TFALSE if the backend doesn't have a_szVariable. Floats are rounded
	static TBOOL HasConsoleSetting( const TCHAR* a_szVariable );
	static TBOOL GetConsoleSetting( const TCHAR* a_szVariable, TINT& a_rValue );

	// Sets the variable (its change callback applies it) and saves it in the registry
	static TBOOL SetConsoleSetting( const TCHAR* a_szVariable, TINT a_iValue );

	// The refresh rate, vsync and multisampling of the console variables, for the display ARenderer creates at
	// boot and for display changes
	static void FillDisplayParams( Toshi::TRenderInterface::DISPLAYPARAMS& a_rParams );

	// Lists for the pages. The display modes of the adapter (32 bit, at least 640x480) with the aspect ratio of the
	// desktop, and the sizes in a_aExtraSizes; fewest pixels first. Returns the number of sizes
	static TINT GetResolutions( TINT ( *a_aSizes )[ 2 ], TINT a_iMaxSizes, const TINT ( *a_aExtraSizes )[ 2 ], TINT a_iNumExtraSizes );

	// The refresh rates the adapter lists for a_iWidth x a_iHeight (32 bit), lowest first. Returns their number
	static TINT GetRefreshRates( TINT a_iWidth, TINT a_iHeight, TINT* a_aRates, TINT a_iMaxRates );

	// Highest multisample count up to a_iSamples the device has (1: none), windowed or fullscreen. Backends that
	// can't tell return a_iSamples
	static TINT GetSupportedMultiSamples( TINT a_iSamples, TBOOL a_bWindowed );

	// Highest anisotropy the device has (1: none). Backends that can't tell return 16
	static TINT GetDeviceMaxAnisotropy();

#  if defined( TRENDERINTERFACE_DX8 ) || defined( TRENDERINTERFACE_DX11 )
	// The console variables of the display (APrimeDebug::RegisterSettings): the storage and the change callback
	inline static TINT ms_iVSync       = 1; // render.vsync
	inline static TINT ms_iRefreshRate = 0; // render.refresh_rate: Hz, 0 (DX8: the default of the driver, retail; DX11: the desktop's)

	// render.vsync, render.refresh_rate (and render.msaa on DX8): the display is changed to the new values
	static void OnDisplayVarChanged();
#  endif

#  ifdef TRENDERINTERFACE_DX8
	// The DX8 multisampling and texture filtering (the DX11 effects register their own render.msaa, render.anisotropy)
	inline static TINT ms_iMultiSamples = 1; // render.msaa: 1 (off, retail), 2, 4, 8 (16)
	inline static TINT ms_iAnisotropy   = 1; // render.anisotropy: 1 (trilinear), 2-16

	// render.anisotropy: the renderer takes it from the next frame
	static void ApplyAnisotropy();
#  endif

	//-----------------------------------------------------------------------------
	// Draw distance (render.draw_distance, the Detail page): the distances the trees, the instances (fences,
	// props) and the regrowth objects (fruits, flowers) are drawn to, in percent of the original (25 to 150).
	// The managers take it when they're created (AOptions::GetRenderDistanceScale) and right away (ApplyDrawDistance)
	// outside of minigames, which set their own distances. The fog and the far clip plane stay: at 150% the trees
	// end where the fog of the farm does. "-renderdistance <percent>" on the command line wins over the saved value
	//-----------------------------------------------------------------------------
	inline static TINT ms_iDrawDistance = 100;

	static TFLOAT GetDrawDistanceScale() { return TFLOAT( ms_iDrawDistance ) / 100.0f; }

	// Gives the managers that exist the distances of ms_iDrawDistance (not in a minigame)
	static void ApplyDrawDistance();

	// TTRUE while the draw distance can't be changed: a minigame keeps its own distances
	static TBOOL IsDrawDistanceLocked();

private:
	inline static Display ms_oSavedDisplay;
	inline static TBOOL   ms_bDrawDistanceOnCommandLine = TFALSE;
};

#endif // BARNYARD_COMMUNITY_PATCH
