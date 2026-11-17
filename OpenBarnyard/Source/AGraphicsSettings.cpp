#include "pch.h"
#include "AGraphicsSettings.h"

#ifdef BARNYARD_COMMUNITY_PATCH

#  include "AOptions.h"
#  include "Render/ARenderer.h"
#  include "Render/ABackend.h"
#  include "GameInterface/AFrontEndMainMenuState2.h"
#  include "Helpers/AGameLoader.h"
#  include "Helpers/AInstanceManager.h"
#  include "Helpers/ATreeManager.h"
#  include "Helpers/ARegrowthManager.h"

#  include <PrimeDebug.h>
#  include <Console/PConsole.h>

#  include <windows.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#  include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

// The key of the retail setup program, AOptions and AMaterialLibrary (HDTextures) use it too
static const TCHAR* s_szSettingsKey = "Software\\THQ\\Barnyard";

// The display values: Width, Height and RealForcedWindowed of retail, Borderless of the community patch (AOptions)
static const TCHAR* s_szWidthValue      = "Width";
static const TCHAR* s_szHeightValue     = "Height";
static const TCHAR* s_szWindowedValue   = "RealForcedWindowed";
static const TCHAR* s_szBorderlessValue = "Borderless";

static const TCHAR* s_aszDisplayModeNames[ AGraphicsSettings::DISPLAYMODE_NUMOF ] = { "fullscreen", "borderless", "windowed" };

static TBOOL ReadDWORD( HKEY a_hKey, const TCHAR* a_szName, TINT& a_rValue )
{
	DWORD dwType = REG_NONE;
	DWORD dwData = 0;
	DWORD dwSize = sizeof( dwData );

	if ( ERROR_SUCCESS != RegQueryValueExA( a_hKey, a_szName, NULL, &dwType, (LPBYTE)&dwData, &dwSize ) || dwType != REG_DWORD )
		return TFALSE;

	a_rValue = TINT( dwData );
	return TTRUE;
}

static TBOOL WriteDWORD( HKEY a_hKey, const TCHAR* a_szName, TINT a_iValue )
{
	DWORD dwData = DWORD( a_iValue );
	return ERROR_SUCCESS == RegSetValueExA( a_hKey, a_szName, 0, REG_DWORD, (LPBYTE)&dwData, sizeof( dwData ) );
}

// Opens the key for writing, it isn't there before the setup program or the menu saved something
static TBOOL OpenSettingsKeyForWriting( HKEY& a_rKey )
{
	return ERROR_SUCCESS == RegCreateKeyExA( HKEY_CURRENT_USER, s_szSettingsKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &a_rKey, NULL );
}

void AGraphicsSettings::Load()
{
	Display oDisplay;
	TBOOL   bOpened = TFALSE;
	HKEY    hKey;

	if ( ERROR_SUCCESS == RegOpenKeyExA( HKEY_CURRENT_USER, s_szSettingsKey, 0, KEY_READ, &hKey ) )
	{
		bOpened = TTRUE;

		// Same rules as AOptions::LoadSettings
		TINT iWidth  = 0;
		TINT iHeight = 0;

		if ( ReadDWORD( hKey, s_szWidthValue, iWidth ) && ReadDWORD( hKey, s_szHeightValue, iHeight ) && iWidth > 0 && iHeight > 0 )
		{
			oDisplay.iWidth  = iWidth;
			oDisplay.iHeight = iHeight;
		}

		TINT iWindowed   = 0;
		TINT iBorderless = 0;
		ReadDWORD( hKey, s_szWindowedValue, iWindowed );
		ReadDWORD( hKey, s_szBorderlessValue, iBorderless );
		oDisplay.iDisplayMode = !iWindowed ? DISPLAYMODE_FULLSCREEN : ( iBorderless ? DISPLAYMODE_BORDERLESS : DISPLAYMODE_WINDOWED );

		RegCloseKey( hKey );
	}

	ms_oSavedDisplay = oDisplay;
	TINFO( "Graphics settings: display %dx%d %s (%s)\n", oDisplay.iWidth, oDisplay.iHeight, s_aszDisplayModeNames[ oDisplay.iDisplayMode ], bOpened ? "saved" : "nothing saved, the default" );

	// "-renderdistance <percent>" (the switch of the web build, 25 to 100 there): it wins over the saved draw distance
	if ( const TCHAR* szArgument = TStringManager::String8FindString( GetCommandLineA(), "-renderdistance " ) )
	{
		const TINT iPercent = atoi( szArgument + 16 );

		if ( iPercent >= 25 && iPercent <= 150 )
		{
			ms_iDrawDistance              = iPercent;
			ms_bDrawDistanceOnCommandLine = TTRUE;
		}
	}
}

static PCVar* FindConsoleVariable( const TCHAR* a_szVariable )
{
	return PConsole::IsStarted() ? PConsole::FindVariable( a_szVariable ) : TNULL;
}

// Sets the variable without saving it
static TBOOL SetConsoleVariable( PCVar* a_pVar, TINT a_iValue )
{
	if ( a_pVar->GetType() == PCVar::TYPE_FLOAT )
		return a_pVar->SetFloat( TFLOAT( a_iValue ) );

	if ( a_pVar->GetType() == PCVar::TYPE_INT )
		return a_pVar->SetInt( a_iValue );

	return TFALSE;
}

void AGraphicsSettings::ApplySavedConsoleSettings()
{
	HKEY hKey;

	if ( ERROR_SUCCESS != RegOpenKeyExA( HKEY_CURRENT_USER, s_szSettingsKey, 0, KEY_READ, &hKey ) )
		return;

	for ( const ConsoleSetting& rcSetting : CONSOLE_SETTINGS )
	{
		TINT   iValue;
		PCVar* pVar = FindConsoleVariable( rcSetting.szVariable );

		if ( !pVar || !ReadDWORD( hKey, rcSetting.szRegistryValue, iValue ) )
			continue;

		if ( ms_bDrawDistanceOnCommandLine && TStringManager::String8Compare( rcSetting.szVariable, "render.draw_distance" ) == 0 )
		{
			TINFO( "Graphics settings: the draw distance is %d percent (-renderdistance), not the saved %d\n", ms_iDrawDistance, iValue );
			continue;
		}

		if ( SetConsoleVariable( pVar, iValue ) )
			TINFO( "Graphics settings: %s %d (saved as %s)\n", rcSetting.szVariable, iValue, rcSetting.szRegistryValue );
		else
			TWARN( "Graphics settings: the saved %s %d isn't valid for %s\n", rcSetting.szRegistryValue, iValue, rcSetting.szVariable );
	}

	RegCloseKey( hKey );
}

TBOOL AGraphicsSettings::HasConsoleSetting( const TCHAR* a_szVariable )
{
	return FindConsoleVariable( a_szVariable ) != TNULL;
}

TBOOL AGraphicsSettings::GetConsoleSetting( const TCHAR* a_szVariable, TINT& a_rValue )
{
	PCVar* pVar = FindConsoleVariable( a_szVariable );

	if ( !pVar )
		return TFALSE;

	if ( pVar->GetType() == PCVar::TYPE_FLOAT )
		a_rValue = TINT( pVar->GetFloat() + 0.5f );
	else if ( pVar->GetType() == PCVar::TYPE_INT )
		a_rValue = pVar->GetInt();
	else
		return TFALSE;

	return TTRUE;
}

TBOOL AGraphicsSettings::SetConsoleSetting( const TCHAR* a_szVariable, TINT a_iValue )
{
	PCVar* pVar = FindConsoleVariable( a_szVariable );

	if ( !pVar || !SetConsoleVariable( pVar, a_iValue ) )
	{
		TWARN( "Graphics settings: %s couldn't be set to %d\n", a_szVariable, a_iValue );
		return TFALSE;
	}

	// The registry name of the variable
	const TCHAR* szValueName = TNULL;

	for ( const ConsoleSetting& rcSetting : CONSOLE_SETTINGS )
	{
		if ( TStringManager::String8Compare( rcSetting.szVariable, a_szVariable ) == 0 )
			szValueName = rcSetting.szRegistryValue;
	}

	TBOOL bSaved = TFALSE;
	HKEY  hKey;

	if ( szValueName && OpenSettingsKeyForWriting( hKey ) )
	{
		bSaved = WriteDWORD( hKey, szValueName, a_iValue );
		RegCloseKey( hKey );
	}

	TINFO( "Graphics settings: %s %d%s\n", a_szVariable, a_iValue, bSaved ? ", saved" : ", NOT SAVED" );
	return TTRUE;
}

TBOOL AGraphicsSettings::IsWindowedForced()
{
	// AOptions::IsWindowed without the setting
	return TStringManager::String8FindString( GetCommandLineA(), "-windowed" ) ||
	    TStringManager::String8FindString( GetCommandLineA(), "-background" ) ||
	    IsDebuggerPresent();
}

void AGraphicsSettings::FillDisplayParams( TRenderInterface::DISPLAYPARAMS& a_rParams )
{
#  if defined( TRENDERINTERFACE_DX8 ) || defined( TRENDERINTERFACE_DX11 )
	a_rParams.uiRefreshRate = TUINT32( TMath::Max( ms_iRefreshRate, 0 ) );
	a_rParams.bVSync        = ms_iVSync != 0;
#  endif

#  ifdef TRENDERINTERFACE_DX8
	a_rParams.uiMultiSamples = TUINT32( TMath::Max( ms_iMultiSamples, 0 ) );
#  endif

	// The DX11 effects take render.msaa and render.anisotropy themselves (the scene target and the samplers)
}

TBOOL AGraphicsSettings::CanApplyDisplayNow( const Display& a_rDisplay )
{
	if ( !ARenderer::IsSingletonCreated() || !TRenderInterface::GetSingleton() || !TRenderInterface::GetSingleton()->IsDisplayCreated() )
		return TFALSE;

	// The display needs a mode of that size (as at boot)
	if ( !ARenderer::GetSingleton()->GetDisplayModes().DoesModeExist( TUINT32( a_rDisplay.iWidth ), TUINT32( a_rDisplay.iHeight ), 32 ) )
		return TFALSE;

	// A new aspect ratio changes the width of the GUI canvas: the screens of a game keep the size they were made
	// with, the frontend makes its screens again when they're shown
	const TRenderInterface::DISPLAYPARAMS* pCurrent    = TRenderInterface::GetSingleton()->GetCurrentDisplayParams();
	const TBOOL                            bSameAspect = TINT64( a_rDisplay.iWidth ) * pCurrent->uiHeight == TINT64( a_rDisplay.iHeight ) * pCurrent->uiWidth;

	return bSameAspect || AFrontEndMainMenuState2::ms_bIsInserted;
}

TBOOL AGraphicsSettings::IsDisplayDifferent( const Display& a_rDisplay )
{
	if ( !TRenderInterface::GetSingleton() || !TRenderInterface::GetSingleton()->IsDisplayCreated() )
		return TFALSE;

	const TRenderInterface::DISPLAYPARAMS* pCurrent = TRenderInterface::GetSingleton()->GetCurrentDisplayParams();

	if ( TUINT32( a_rDisplay.iWidth ) != pCurrent->uiWidth || TUINT32( a_rDisplay.iHeight ) != pCurrent->uiHeight )
		return TTRUE;

	if ( IsWindowedForced() )
		return TFALSE;

	const TBOOL bWindowed = a_rDisplay.iDisplayMode != DISPLAYMODE_FULLSCREEN;
	return bWindowed != pCurrent->bWindowed || ( bWindowed && ( a_rDisplay.iDisplayMode == DISPLAYMODE_BORDERLESS ) != pCurrent->bBorderless );
}

TBOOL AGraphicsSettings::ApplyDisplay( const Display& a_rDisplay )
{
	const Display oOld = ms_oSavedDisplay;

	if ( a_rDisplay == oOld )
		return TFALSE;

	// Only the values that changed are written: the key keeps what the user never touched as it is
	TBOOL bSaved = TFALSE;
	HKEY  hKey;

	if ( OpenSettingsKeyForWriting( hKey ) )
	{
		bSaved = TTRUE;

		if ( a_rDisplay.iWidth != oOld.iWidth || a_rDisplay.iHeight != oOld.iHeight )
			bSaved = WriteDWORD( hKey, s_szWidthValue, a_rDisplay.iWidth ) && WriteDWORD( hKey, s_szHeightValue, a_rDisplay.iHeight ) && bSaved;

		if ( a_rDisplay.iDisplayMode != oOld.iDisplayMode )
		{
			bSaved = WriteDWORD( hKey, s_szWindowedValue, a_rDisplay.iDisplayMode != DISPLAYMODE_FULLSCREEN ) && bSaved;
			bSaved = WriteDWORD( hKey, s_szBorderlessValue, a_rDisplay.iDisplayMode == DISPLAYMODE_BORDERLESS ) && bSaved;
		}

		RegCloseKey( hKey );
	}

	ms_oSavedDisplay = a_rDisplay;
	TINFO( "Graphics settings: display %dx%d %s%s\n", a_rDisplay.iWidth, a_rDisplay.iHeight, s_aszDisplayModeNames[ a_rDisplay.iDisplayMode ], bSaved ? ", saved" : ", NOT SAVED" );

	if ( !CanApplyDisplayNow( a_rDisplay ) )
	{
		TINFO( "Graphics settings: the display changes at the next start\n" );
		return TFALSE;
	}

	// The running display (or the one a change waits for) with what changed
	TRenderInterface::DISPLAYPARAMS oParams = ARenderer::GetSingleton()->GetRequestedDisplayParams();
	oParams.uiWidth                         = TUINT32( a_rDisplay.iWidth );
	oParams.uiHeight                        = TUINT32( a_rDisplay.iHeight );

	if ( a_rDisplay.iDisplayMode != oOld.iDisplayMode )
	{
		oParams.bWindowed   = a_rDisplay.iDisplayMode != DISPLAYMODE_FULLSCREEN || IsWindowedForced();
		oParams.bBorderless = a_rDisplay.iDisplayMode == DISPLAYMODE_BORDERLESS;
	}

	FillDisplayParams( oParams );

	// Outside of the scene, at the start of the next frame
	ARenderer::GetSingleton()->RequestDisplayChange( oParams );
	return TTRUE;
}

TINT AGraphicsSettings::GetResolutions( TINT ( *a_aSizes )[ 2 ], TINT a_iMaxSizes, const TINT ( *a_aExtraSizes )[ 2 ], TINT a_iNumExtraSizes )
{
	TINT iNumSizes = 0;

	auto fnAdd = [ & ]( TINT a_iWidth, TINT a_iHeight ) {
		if ( a_iWidth <= 0 || a_iHeight <= 0 || a_iMaxSizes <= 0 )
			return;

		for ( TINT i = 0; i < iNumSizes; i++ )
		{
			if ( a_aSizes[ i ][ 0 ] == a_iWidth && a_aSizes[ i ][ 1 ] == a_iHeight )
				return;
		}

		if ( iNumSizes >= a_iMaxSizes )
		{
			// Full: the smallest size makes room for a larger one (the large ones are the ones worth listing)
			if ( TINT64( a_aSizes[ 0 ][ 0 ] ) * a_aSizes[ 0 ][ 1 ] >= TINT64( a_iWidth ) * a_iHeight )
				return;

			for ( TINT i = 1; i < iNumSizes; i++ )
			{
				a_aSizes[ i - 1 ][ 0 ] = a_aSizes[ i ][ 0 ];
				a_aSizes[ i - 1 ][ 1 ] = a_aSizes[ i ][ 1 ];
			}

			iNumSizes--;
		}

		// Fewest pixels first
		TINT iIndex = iNumSizes++;

		while ( iIndex > 0 && TINT64( a_aSizes[ iIndex - 1 ][ 0 ] ) * a_aSizes[ iIndex - 1 ][ 1 ] > TINT64( a_iWidth ) * a_iHeight )
		{
			a_aSizes[ iIndex ][ 0 ] = a_aSizes[ iIndex - 1 ][ 0 ];
			a_aSizes[ iIndex ][ 1 ] = a_aSizes[ iIndex - 1 ][ 1 ];
			iIndex--;
		}

		a_aSizes[ iIndex ][ 0 ] = a_iWidth;
		a_aSizes[ iIndex ][ 1 ] = a_iHeight;
	};

	for ( TINT i = 0; i < a_iNumExtraSizes; i++ )
		fnAdd( a_aExtraSizes[ i ][ 0 ], a_aExtraSizes[ i ][ 1 ] );

	if ( !ARenderer::IsSingletonCreated() )
		return iNumSizes;

	// Every size the display of this machine lists (they used to be limited to the aspect ratio of the desktop)
	ADisplayModes_Win& rModes = ARenderer::GetSingleton()->GetDisplayModes();

	for ( TINT i = 0; i < rModes.GetNumModes(); i++ )
	{
		const ADisplayModes_Win::Mode& rcMode = rModes.GetMode( i );

		if ( rcMode.m_uiColourDepth != 32 || rcMode.m_uiWidth < 640 || rcMode.m_uiHeight < 480 )
			continue;

		fnAdd( TINT( rcMode.m_uiWidth ), TINT( rcMode.m_uiHeight ) );
	}

	return iNumSizes;
}

TINT AGraphicsSettings::GetRefreshRates( TINT a_iWidth, TINT a_iHeight, TINT* a_aRates, TINT a_iMaxRates )
{
	TRenderInterface* pRenderer = TRenderInterface::GetSingleton();

	if ( !pRenderer || !pRenderer->GetCurrentDevice() )
		return 0;

	TINT iNumRates = 0;

	// The modes of the adapter of the device: D3D8 lists each size once for every refresh rate
	T2_FOREACH( *pRenderer->GetCurrentDevice()->GetMode()->GetAdapter()->GetModeList(), it )
	{
		TRenderAdapter::Mode* pMode = it.Get();
		const TINT            iRate = TINT( pMode->GetRefreshRate() );

		if ( TINT( pMode->GetWidth() ) != a_iWidth || TINT( pMode->GetHeight() ) != a_iHeight || !pMode->Is32Bit() || iRate <= 1 )
			continue;

		TBOOL bListed = TFALSE;

		for ( TINT i = 0; i < iNumRates && !bListed; i++ )
			bListed = a_aRates[ i ] == iRate;

		if ( bListed || iNumRates >= a_iMaxRates )
			continue;

		// Lowest first
		TINT iIndex = iNumRates++;

		while ( iIndex > 0 && a_aRates[ iIndex - 1 ] > iRate )
		{
			a_aRates[ iIndex ] = a_aRates[ iIndex - 1 ];
			iIndex--;
		}

		a_aRates[ iIndex ] = iRate;
	}

	return iNumRates;
}

TINT AGraphicsSettings::GetSupportedMultiSamples( TINT a_iSamples, TBOOL a_bWindowed )
{
#  ifdef TRENDERINTERFACE_DX8
	if ( TRenderInterface::GetSingleton() && TRenderInterface::GetSingleton()->IsDisplayCreated() )
		return TMath::Max( TINT( TRenderInterfaceHAL::Interface()->GetSupportedMultiSamples( TUINT32( TMath::Max( a_iSamples, 0 ) ), a_bWindowed ) ), 1 );

	return 1;
#  else
	// GRAPHICS-DX11: every count is offered, the DX11 effects keep their scene target within what the device has
	// (TRenderD3DInterface::GetSupportedSampleCount of the effects work, it can filter the choices once merged)
	return a_iSamples;
#  endif
}

TINT AGraphicsSettings::GetDeviceMaxAnisotropy()
{
#  ifdef TRENDERINTERFACE_DX8
	if ( TRenderInterface::GetSingleton() && TRenderInterface::GetSingleton()->IsDisplayCreated() )
		return TINT( TRenderInterfaceHAL::Interface()->GetDeviceMaxAnisotropy() );

	return 1;
#  else
	// D3D11 hardware has 16x
	return 16;
#  endif
}

#  if defined( TRENDERINTERFACE_DX8 ) || defined( TRENDERINTERFACE_DX11 )

void AGraphicsSettings::OnDisplayVarChanged()
{
	// At boot the console runs before the display is created, ARenderer::CreateTRender reads the variables then
	if ( !ARenderer::IsSingletonCreated() || !TRenderInterface::GetSingleton() || !TRenderInterface::GetSingleton()->IsDisplayCreated() )
		return;

	TRenderInterface::DISPLAYPARAMS oParams = ARenderer::GetSingleton()->GetRequestedDisplayParams();
	FillDisplayParams( oParams );
	ARenderer::GetSingleton()->RequestDisplayChange( oParams );
}

#  endif // TRENDERINTERFACE_DX8 || TRENDERINTERFACE_DX11

// The distances of the original (the constructors of the managers), the draw distance scales them
static constexpr TFLOAT TREE_RENDER_DISTANCE        = 110.0f;
static constexpr TFLOAT TREE_BLENDING_DISTANCE      = 98.0f;
static constexpr TFLOAT INSTANCE_BLEND_END_DISTANCE = 85.0f;
static constexpr TFLOAT INSTANCE_BLEND_START        = 75.0f;
static constexpr TFLOAT REGROWTH_RENDER_DISTANCE    = 60.0f;
static constexpr TFLOAT REGROWTH_FADE_DISTANCE      = 50.0f;

TBOOL AGraphicsSettings::IsDrawDistanceLocked()
{
	return AGameLoader::IsInMiniGame();
}

void AGraphicsSettings::ApplyDrawDistance()
{
	// At boot the managers don't exist yet, they take it when they're created
	if ( IsDrawDistanceLocked() )
		return;

	const TFLOAT fScale = GetDrawDistanceScale();

	if ( ATreeManager::IsSingletonCreated() )
	{
		TFLOAT fLODSwitchDistance, fRenderDistance, fBlendingDistance;
		ATreeManager::GetSingleton()->GetDistances( fLODSwitchDistance, fRenderDistance, fBlendingDistance );
		ATreeManager::GetSingleton()->SetDistances( fLODSwitchDistance, TREE_RENDER_DISTANCE * fScale, TREE_BLENDING_DISTANCE * fScale );
	}

	if ( AInstanceManager::IsSingletonCreated() )
	{
		TFLOAT fLOD0Distance, fBlendEnd, fBlendStart;
		AInstanceManager::GetSingleton()->GetVisibilityDistances( fLOD0Distance, fBlendEnd, fBlendStart );
		AInstanceManager::GetSingleton()->SetVisibilityDistances( fLOD0Distance, INSTANCE_BLEND_END_DISTANCE * fScale, INSTANCE_BLEND_START * fScale );
	}

	if ( ARegrowthManager::IsSingletonCreated() )
		ARegrowthManager::GetSingleton()->SetDrawDistances( REGROWTH_RENDER_DISTANCE * fScale, REGROWTH_FADE_DISTANCE * fScale );

	TINFO( "Draw distance %d percent: trees %.0f, instances %.0f, regrowth %.0f\n", ms_iDrawDistance, TREE_RENDER_DISTANCE * fScale, INSTANCE_BLEND_END_DISTANCE * fScale, REGROWTH_RENDER_DISTANCE * fScale );
}

#  ifdef TRENDERINTERFACE_DX8

void AGraphicsSettings::ApplyAnisotropy()
{
	if ( TRenderInterface::GetSingleton() )
		TRenderInterfaceHAL::Interface()->SetMaxAnisotropy( TUINT32( TMath::Max( ms_iAnisotropy, 1 ) ) );
}

#  endif // TRENDERINTERFACE_DX8

#endif // BARNYARD_COMMUNITY_PATCH
