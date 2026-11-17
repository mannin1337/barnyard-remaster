#include "pch.h"
#include "AGraphicsOptionsState.h"

#ifdef BARNYARD_COMMUNITY_PATCH

#  include "AOptionsState.h"
#  include "AGraphicsSettings.h"
#  include "Assets/AMaterialLibrary.h"
#  include "Sound/ASoundManager.h"
#  include "SoundBank/ui.h"
#  include "GUI/T2GUIFontManager.h"
#  include "GUI/AGUI2Style.h"
#  include "Locale/ALocaleManager.h"
#  include "Locale/AModStrings.h"
#  include "HUD/AHudManager.h"
#  include "HUD/AMenuNavHudElement.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#  include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( AGraphicsOptionsState );

// Gap between the rows (the retail option pages have 20) and the room around the grid (150 there, it makes room
// for the title): eight rows and the line stay clear of the hint bar and of the title of the pause menu
static constexpr TFLOAT ROW_GAP          = 4.0f;
static constexpr TFLOAT DIALOG_PADDING_X = 128.0f;
static constexpr TFLOAT DIALOG_PADDING_Y = 112.0f;

// The line below the rows: gap and room for a line of the secondary font
static constexpr TFLOAT INFO_GAP    = 14.0f;
static constexpr TFLOAT INFO_HEIGHT = 24.0f;

// NOTE: there are no locale strings for these pages. The names of the pages, of the rows and of the values that are
// words are texts of the mod (AModStrings: English, or the translation of the language of the game); the numbers and
// the names of the techniques are the same in every language
struct Choice
{
	TINT          iValue;
	const TWCHAR* wszText; // the same in every language, TNULL: eText
	MODSTRING     eText;   // a text of the mod, the value is its %d if it has one ("Aniso %dx")
};

static constexpr Choice Same( TINT a_iValue, const TWCHAR* a_wszText )
{
	return { a_iValue, a_wszText, MODSTRING_NUMOF };
}

static constexpr Choice Word( TINT a_iValue, MODSTRING a_eText )
{
	return { a_iValue, TNULL, a_eText };
}

static constexpr Choice s_aOffOn[]        = { Word( 0, MODSTRING_GRAPHICS_VALUE_OFF ), Word( 1, MODSTRING_GRAPHICS_VALUE_ON ) };
static constexpr Choice s_aDisplayModes[] = {
	Word( AGraphicsSettings::DISPLAYMODE_FULLSCREEN, MODSTRING_GRAPHICS_VALUE_FULLSCREEN ),
	Word( AGraphicsSettings::DISPLAYMODE_BORDERLESS, MODSTRING_GRAPHICS_VALUE_BORDERLESS ),
	Word( AGraphicsSettings::DISPLAYMODE_WINDOWED, MODSTRING_GRAPHICS_VALUE_WINDOWED ),
};
static constexpr Choice s_aFPSLimits[]    = { Word( 0, MODSTRING_GRAPHICS_VALUE_OFF ), Same( 30, L"30" ), Same( 60, L"60" ), Same( 120, L"120" ), Same( 144, L"144" ), Same( 240, L"240" ) };
static constexpr Choice s_aMultiSamples[] = { Word( 1, MODSTRING_GRAPHICS_VALUE_OFF ), Same( 2, L"2x" ), Same( 4, L"4x" ), Same( 8, L"8x" ) };
static constexpr Choice s_aAnisotropies[] = {
	Word( 1, MODSTRING_GRAPHICS_VALUE_TRILINEAR ),
	Word( 4, MODSTRING_GRAPHICS_VALUE_ANISO ),
	Word( 8, MODSTRING_GRAPHICS_VALUE_ANISO ),
	Word( 16, MODSTRING_GRAPHICS_VALUE_ANISO ),
};
static constexpr Choice s_aAO[]             = { Word( 0, MODSTRING_GRAPHICS_VALUE_OFF ), Same( 1, L"HBAO" ), Same( 2, L"GTAO" ) };
static constexpr Choice s_aAOQualities[]    = { Word( 0, MODSTRING_GRAPHICS_VALUE_AO_HALF ), Word( 1, MODSTRING_GRAPHICS_VALUE_AO_FULL ) };
static constexpr Choice s_aLightShadows[]   = { Word( 0, MODSTRING_GRAPHICS_VALUE_OFF ), Same( 1, L"512" ), Same( 2, L"1024" ) };
static constexpr Choice s_aSunShadows[]     = { Word( 0, MODSTRING_GRAPHICS_VALUE_OFF ), Same( 1, L"1024" ), Same( 2, L"2048" ), Same( 3, L"4096" ) };
static constexpr Choice s_aVolumetricFogs[] = { Word( 0, MODSTRING_GRAPHICS_VALUE_OFF ), Word( 1, MODSTRING_GRAPHICS_VALUE_FOG_ADDITIVE ), Word( 2, MODSTRING_GRAPHICS_VALUE_FOG_DARKEN ) };
static constexpr Choice s_aDrawDistances[]  = {
	Word( 50, MODSTRING_GRAPHICS_VALUE_DISTANCE_LOW ),
	Word( 75, MODSTRING_GRAPHICS_VALUE_DISTANCE_MEDIUM ),
	Word( 100, MODSTRING_GRAPHICS_VALUE_DISTANCE_NORMAL ),
	Word( 125, MODSTRING_GRAPHICS_VALUE_DISTANCE_FAR ),
	Word( 150, MODSTRING_GRAPHICS_VALUE_DISTANCE_VERYFAR ),
};
static constexpr Choice s_aGrassDensities[] = {
	Word( 0, MODSTRING_GRAPHICS_VALUE_OFF ),
	Word( 1, MODSTRING_GRAPHICS_VALUE_GRASS_LOW ),
	Word( 2, MODSTRING_GRAPHICS_VALUE_GRASS_NORMAL ),
	Word( 3, MODSTRING_GRAPHICS_VALUE_GRASS_HIGH ),
	Word( 4, MODSTRING_GRAPHICS_VALUE_GRASS_ULTRA ),
};

struct RowInfo
{
	MODSTRING                   eLabel;     // the name of the row, a text of the mod
	const TCHAR*                szVariable; // the console variable (AGraphicsSettings::CONSOLE_SETTINGS), TNULL if none
	AGraphicsOptionsState::PAGE ePage;
	const Choice*               pChoices; // fixed choices, the others are made by SetChoices
	TINT                        iNumChoices;
	const TWCHAR*               wszFormat; // text of a value that isn't one of the choices (from the command line)...
	MODSTRING                   eFormat = MODSTRING_NUMOF; // ...or the text of the mod it is made with
};

#  define CHOICES( ARRAY ) ARRAY, TARRAYSIZE( ARRAY )

// NOTE: MODSTRING_GRAPHICS_VALUE_PERCENT is "%d percent", the fonts have no % sign
static const RowInfo s_aRowInfos[ AGraphicsOptionsState::ROW_NUMOF ] = {
	{ MODSTRING_GRAPHICS_ROW_RESOLUTION, TNULL, AGraphicsOptionsState::PAGE_GRAPHICS, TNULL, 0, L"%d" },
	{ MODSTRING_GRAPHICS_ROW_DISPLAYMODE, TNULL, AGraphicsOptionsState::PAGE_GRAPHICS, CHOICES( s_aDisplayModes ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_REFRESHRATE, "render.refresh_rate", AGraphicsOptionsState::PAGE_GRAPHICS, TNULL, 0, L"%d Hz" },
	{ MODSTRING_GRAPHICS_ROW_VSYNC, "render.vsync", AGraphicsOptionsState::PAGE_GRAPHICS, CHOICES( s_aOffOn ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_FPSLIMIT, "game.fps_limit", AGraphicsOptionsState::PAGE_GRAPHICS, CHOICES( s_aFPSLimits ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_ANTIALIASING, "render.msaa", AGraphicsOptionsState::PAGE_GRAPHICS, TNULL, 0, L"%dx" },
	{ MODSTRING_GRAPHICS_ROW_TEXTUREFILTER, "render.anisotropy", AGraphicsOptionsState::PAGE_GRAPHICS, TNULL, 0, TNULL, MODSTRING_GRAPHICS_VALUE_ANISO },
	{ MODSTRING_GRAPHICS_ROW_HDTEXTURES, TNULL, AGraphicsOptionsState::PAGE_GRAPHICS, CHOICES( s_aOffOn ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_DRAWDISTANCE, "render.draw_distance", AGraphicsOptionsState::PAGE_DETAIL, CHOICES( s_aDrawDistances ), TNULL, MODSTRING_GRAPHICS_VALUE_PERCENT },
	{ MODSTRING_GRAPHICS_ROW_GRASSDENSITY, "render.grass_density", AGraphicsOptionsState::PAGE_DETAIL, CHOICES( s_aGrassDensities ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_AO, "render.ao", AGraphicsOptionsState::PAGE_EFFECTS, CHOICES( s_aAO ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_AOQUALITY, "render.ao_quality", AGraphicsOptionsState::PAGE_EFFECTS, CHOICES( s_aAOQualities ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_BLOOM, "render.bloom", AGraphicsOptionsState::PAGE_EFFECTS, CHOICES( s_aOffOn ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_DYNAMICLIGHTS, "render.dynamic_lights", AGraphicsOptionsState::PAGE_EFFECTS, CHOICES( s_aOffOn ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_LIGHTSHADOWS, "render.dynamic_light_shadows", AGraphicsOptionsState::PAGE_EFFECTS, CHOICES( s_aLightShadows ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_SUNSHADOWS, "render.sun_shadows", AGraphicsOptionsState::PAGE_EFFECTS, CHOICES( s_aSunShadows ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_SUNSHAFTS, "render.sun_shafts", AGraphicsOptionsState::PAGE_EFFECTS, CHOICES( s_aOffOn ), L"%d" },
	{ MODSTRING_GRAPHICS_ROW_VOLUMETRICFOG, "render.volumetric_fog", AGraphicsOptionsState::PAGE_EFFECTS, CHOICES( s_aVolumetricFogs ), L"%d" },
};

#  undef CHOICES

// The text of a fixed choice, the format of a value that isn't one of them
static const TWCHAR* GetChoiceText( const Choice& a_rcChoice )
{
	return a_rcChoice.wszText ? a_rcChoice.wszText : AModStrings::Get( a_rcChoice.eText );
}

static const TWCHAR* GetValueFormat( const RowInfo& a_rcInfo )
{
	return ( a_rcInfo.eFormat != MODSTRING_NUMOF ) ? AModStrings::Get( a_rcInfo.eFormat ) : a_rcInfo.wszFormat;
}

// The widest value a row can show, every value box has its width (the boxes don't resize)
static const TWCHAR* s_wszWidestValue = L"0000 x 0000";

// For the log
static const TCHAR* s_aszPageNames[ AGraphicsOptionsState::PAGE_NUMOF ] = { "Graphics", "Detail", "Effects" };

// A row is shown if it's on the page and the backend has its variable
static TBOOL IsRowShown( TINT a_iRow, AGraphicsOptionsState::PAGE a_ePage )
{
	const RowInfo& rcInfo = s_aRowInfos[ a_iRow ];
	return rcInfo.ePage == a_ePage && ( !rcInfo.szVariable || AGraphicsSettings::HasConsoleSetting( rcInfo.szVariable ) );
}

AGraphicsOptionsState::AGraphicsOptionsState( PAGE a_ePage )
    : m_ePage( a_ePage )
    , m_iNumRows( 0 )
    , m_iFocusedRow( 0 )
    , m_iNumSizes( 0 )
{
	m_wszInfo[ 0 ] = L'\0';
}

AGraphicsOptionsState::~AGraphicsOptionsState()
{
}

const TWCHAR* AGraphicsOptionsState::GetButtonText( PAGE a_ePage )
{
	// Also the title of the page
	static constexpr MODSTRING s_aeTexts[ PAGE_NUMOF ] = { MODSTRING_GRAPHICS_PAGE_GRAPHICS, MODSTRING_GRAPHICS_PAGE_DETAIL, MODSTRING_GRAPHICS_PAGE_EFFECTS };
	return AModStrings::Get( s_aeTexts[ a_ePage ] );
}

TBOOL AGraphicsOptionsState::HasRows( PAGE a_ePage )
{
	for ( TINT i = 0; i < ROW_NUMOF; i++ )
	{
		if ( IsRowShown( i, a_ePage ) )
			return TTRUE;
	}

	return TFALSE;
}

AGraphicsOptionsState::Row* AGraphicsOptionsState::FindRow( ROW a_eRow )
{
	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		if ( m_aRows[ i ].eRow == a_eRow )
			return &m_aRows[ i ];
	}

	return TNULL;
}

TINT AGraphicsOptionsState::GetValue( ROW a_eRow, TINT a_iDefault )
{
	Row* pRow = FindRow( a_eRow );
	return ( pRow && pRow->iNumChoices > 0 ) ? pRow->aiValues[ pRow->iChoice ] : a_iDefault;
}

void AGraphicsOptionsState::SetChoices( Row& a_rRow, TINT a_iValue )
{
	const RowInfo& rcInfo = s_aRowInfos[ a_rRow.eRow ];
	a_rRow.iNumChoices    = 0;

	auto fnAdd = [ &a_rRow ]( TINT a_iChoiceValue, const TWCHAR* a_wszFormat, TINT a_iFormatValue1, TINT a_iFormatValue2 ) {
		if ( a_rRow.iNumChoices >= MAX_CHOICES )
			return;

		// NOTE: cut to MAX_TEXT if a translation is longer
		a_rRow.aiValues[ a_rRow.iNumChoices ] = a_iChoiceValue;
		AModStrings::Format( a_rRow.awszTexts[ a_rRow.iNumChoices ], MAX_TEXT, a_wszFormat, a_iFormatValue1, a_iFormatValue2 );
		a_rRow.iNumChoices++;
	};

	// A fixed choice: the value is the %d of its text if it has one
	auto fnAddChoice = [ &fnAdd ]( const Choice& a_rcChoice ) {
		fnAdd( a_rcChoice.iValue, GetChoiceText( a_rcChoice ), a_rcChoice.iValue, 0 );
	};

	// The resolution and the display mode of the page (the rows before)
	const TINT  iSize     = GetValue( ROW_RESOLUTION, 0 );
	const TBOOL bWindowed = GetValue( ROW_DISPLAYMODE, AGraphicsSettings::DISPLAYMODE_FULLSCREEN ) != AGraphicsSettings::DISPLAYMODE_FULLSCREEN || AGraphicsSettings::IsWindowedForced();

	switch ( a_rRow.eRow )
	{
		case ROW_RESOLUTION:
			// The values are indices into m_aiSizes
			for ( TINT i = 0; i < m_iNumSizes; i++ )
				fnAdd( i, L"%d x %d", m_aiSizes[ i ][ 0 ], m_aiSizes[ i ][ 1 ] );
			break;

		case ROW_REFRESHRATE:
		{
			// The rates the adapter lists for the resolution, fullscreen only
			fnAdd( 0, AModStrings::Get( MODSTRING_GRAPHICS_VALUE_DEFAULT ), 0, 0 );

			TINT aiRates[ MAX_CHOICES ];
			TINT iNumRates = ( m_iNumSizes > 0 ) ? AGraphicsSettings::GetRefreshRates( m_aiSizes[ iSize ][ 0 ], m_aiSizes[ iSize ][ 1 ], aiRates, MAX_CHOICES - 2 ) : 0;

			for ( TINT i = 0; i < iNumRates; i++ )
				fnAdd( aiRates[ i ], L"%d Hz", aiRates[ i ], 0 );

			break;
		}

		case ROW_ANTIALIASING:
			// The counts the device has for the display mode
			for ( const Choice& rcChoice : s_aMultiSamples )
			{
				if ( rcChoice.iValue <= 1 || AGraphicsSettings::GetSupportedMultiSamples( rcChoice.iValue, bWindowed ) == rcChoice.iValue )
					fnAddChoice( rcChoice );
			}
			break;

		case ROW_TEXTUREFILTER:
			for ( const Choice& rcChoice : s_aAnisotropies )
			{
				if ( rcChoice.iValue <= AGraphicsSettings::GetDeviceMaxAnisotropy() )
					fnAddChoice( rcChoice );
			}
			break;

		default:
			for ( TINT i = 0; i < rcInfo.iNumChoices; i++ )
				fnAddChoice( rcInfo.pChoices[ i ] );
			break;
	}

	// The value, it can be one the page doesn't offer (the command line, the dev config)
	a_rRow.iChoice = -1;

	for ( TINT i = 0; i < a_rRow.iNumChoices && a_rRow.iChoice < 0; i++ )
	{
		if ( a_rRow.aiValues[ i ] == a_iValue )
			a_rRow.iChoice = i;
	}

	if ( a_rRow.iChoice < 0 )
	{
		if ( a_rRow.eRow == ROW_REFRESHRATE || a_rRow.eRow == ROW_ANTIALIASING || a_rRow.iNumChoices >= MAX_CHOICES )
		{
			// The refresh rate of another resolution isn't kept: the default. Multisampling the device doesn't have
			// for the mode: the highest count below it that it has (the device does the same)
			a_rRow.iChoice = 0;

			for ( TINT i = 1; i < a_rRow.iNumChoices && a_rRow.eRow == ROW_ANTIALIASING; i++ )
			{
				if ( a_rRow.aiValues[ i ] <= a_iValue )
					a_rRow.iChoice = i;
			}
		}
		else
		{
			// A value of the command line or the dev config
			fnAdd( a_iValue, GetValueFormat( rcInfo ), a_iValue, 0 );
			a_rRow.iChoice = a_rRow.iNumChoices - 1;
		}
	}

	a_rRow.oValue.SetText( a_rRow.awszTexts[ a_rRow.iChoice ] );
}

void AGraphicsOptionsState::UpdateDependentRows()
{
	const TBOOL bFullscreen = GetValue( ROW_DISPLAYMODE, AGraphicsSettings::DISPLAYMODE_FULLSCREEN ) == AGraphicsSettings::DISPLAYMODE_FULLSCREEN;

	// The rates of the new resolution (the same rate if it has it), only used by a fullscreen display
	if ( Row* pRow = FindRow( ROW_REFRESHRATE ) )
	{
		SetChoices( *pRow, pRow->aiValues[ pRow->iChoice ] );
		pRow->bEnabled = bFullscreen;
		pRow->oLabel.SetEnabled( pRow->bEnabled );
	}

#  ifdef TRENDERINTERFACE_DX8
	// DX8 syncs a fullscreen display only, the FPS limit caps a window (TRenderD3DInterface::FillPresentParams)
	if ( Row* pRow = FindRow( ROW_VSYNC ) )
	{
		pRow->bEnabled = bFullscreen;
		pRow->oLabel.SetEnabled( pRow->bEnabled );
	}
#  endif

	// The counts the device has for the mode (the same or the highest below it)
	if ( Row* pRow = FindRow( ROW_ANTIALIASING ) )
		SetChoices( *pRow, pRow->aiValues[ pRow->iChoice ] );
}

void AGraphicsOptionsState::ChangeValue( TINT a_iDirection )
{
	if ( m_iFocusedRow < 0 || m_iFocusedRow >= m_iNumRows )
		return;

	Row& rRow = m_aRows[ m_iFocusedRow ];

	if ( !rRow.bEnabled || rRow.iNumChoices <= 1 )
		return;

	ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUCLICK );

	// Round and round
	rRow.iChoice = ( rRow.iChoice + a_iDirection + rRow.iNumChoices ) % rRow.iNumChoices;
	rRow.oValue.SetText( rRow.awszTexts[ rRow.iChoice ] );

	if ( rRow.eRow == ROW_RESOLUTION || rRow.eRow == ROW_DISPLAYMODE )
		UpdateDependentRows();

	UpdateInfo();
}

void AGraphicsOptionsState::UpdateInfo()
{
	m_wszInfo[ 0 ] = L'\0';

	// What OK can't apply now
	const TWCHAR* awszRestart[ 3 ];
	TINT          iNumRestart = 0;

	if ( Row* pRow = FindRow( ROW_RESOLUTION ) )
	{
		AGraphicsSettings::Display oDisplay;
		oDisplay.iWidth       = m_aiSizes[ pRow->aiValues[ pRow->iChoice ] ][ 0 ];
		oDisplay.iHeight      = m_aiSizes[ pRow->aiValues[ pRow->iChoice ] ][ 1 ];
		oDisplay.iDisplayMode = GetValue( ROW_DISPLAYMODE, AGraphicsSettings::DISPLAYMODE_FULLSCREEN );

		if ( AGraphicsSettings::IsDisplayDifferent( oDisplay ) && !AGraphicsSettings::CanApplyDisplayNow( oDisplay ) )
			awszRestart[ iNumRestart++ ] = AModStrings::Get( s_aRowInfos[ ROW_RESOLUTION ].eLabel );
	}

	if ( Row* pRow = FindRow( ROW_HDTEXTURES ) )
	{
		if ( ( pRow->aiValues[ pRow->iChoice ] != 0 ) != ( AMaterialLibrary::ms_iTextureOverrides != 0 ) )
			awszRestart[ iNumRestart++ ] = AModStrings::Get( s_aRowInfos[ ROW_HDTEXTURES ].eLabel );
	}

	// NOTE: the lines are cut to the buffer if a translation is longer
	if ( iNumRestart > 0 )
	{
		// "Applies at the next start: Resolution, HD Textures"
		TWCHAR wszRows[ TARRAYSIZE( m_wszInfo ) ];
		TINT   iLength = 0;
		wszRows[ 0 ]   = L'\0';

		for ( TINT i = 0; i < iNumRestart; i++ )
			iLength += AModStrings::Format( wszRows + iLength, TINT( TARRAYSIZE( wszRows ) ) - iLength, ( i > 0 ) ? L", %s" : L"%s", awszRestart[ i ] );

		AModStrings::Format( m_wszInfo, TARRAYSIZE( m_wszInfo ), AModStrings::Get( MODSTRING_GRAPHICS_INFO_RESTART ), wszRows );
	}
	else if ( m_ePage == PAGE_GRAPHICS && GetValue( ROW_DISPLAYMODE, -1 ) == AGraphicsSettings::DISPLAYMODE_FULLSCREEN && AGraphicsSettings::IsWindowedForced() )
	{
		TStringManager::String16CopySafe( m_wszInfo, AModStrings::Get( MODSTRING_GRAPHICS_INFO_WINDOWED ), TARRAYSIZE( m_wszInfo ) );
	}
	else if ( FindRow( ROW_DRAWDISTANCE ) && AGraphicsSettings::IsDrawDistanceLocked() )
	{
		TStringManager::String16CopySafe( m_wszInfo, AModStrings::Get( MODSTRING_GRAPHICS_INFO_MINIGAME ), TARRAYSIZE( m_wszInfo ) );
	}

	m_oInfo.SetText( m_wszInfo );
}

void AGraphicsOptionsState::Apply()
{
	TINFO( "[graphics] %s page: OK\n", s_aszPageNames[ m_ePage ] );

	// The console variables first: on DX8 the callbacks of the multisampling, vsync and refresh rate request the
	// display change, the size and mode below are added to that request
	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		const Row&     rcRow  = m_aRows[ i ];
		const RowInfo& rcInfo = s_aRowInfos[ rcRow.eRow ];

		if ( rcInfo.szVariable && IsChanged( rcRow ) )
			AGraphicsSettings::SetConsoleSetting( rcInfo.szVariable, rcRow.aiValues[ rcRow.iChoice ] );
	}

	Row* pResolution  = FindRow( ROW_RESOLUTION );
	Row* pDisplayMode = FindRow( ROW_DISPLAYMODE );

	if ( ( pResolution && IsChanged( *pResolution ) ) || ( pDisplayMode && IsChanged( *pDisplayMode ) ) )
	{
		AGraphicsSettings::Display oDisplay = AGraphicsSettings::GetSavedDisplay();

		if ( pResolution )
		{
			oDisplay.iWidth  = m_aiSizes[ pResolution->aiValues[ pResolution->iChoice ] ][ 0 ];
			oDisplay.iHeight = m_aiSizes[ pResolution->aiValues[ pResolution->iChoice ] ][ 1 ];
		}

		if ( pDisplayMode )
			oDisplay.iDisplayMode = pDisplayMode->aiValues[ pDisplayMode->iChoice ];

		AGraphicsSettings::ApplyDisplay( oDisplay );
	}

	// From the next start (see AOptionsState)
	if ( Row* pRow = FindRow( ROW_HDTEXTURES ) )
	{
		if ( IsChanged( *pRow ) )
			AMaterialLibrary::SetTextureOverridesEnabled( pRow->aiValues[ pRow->iChoice ] != 0 );
	}
}

TBOOL AGraphicsOptionsState::ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent )
{
	if ( m_eMenuState == MENUSTATE_MENU_VISIBLE )
	{
		if ( m_oGridMenu.ProcessInputEvent( a_pInputEvent ) )
			return TTRUE;

		// The arrows of the values (a click is a Left/Right key press)
		for ( TINT i = 0; i < m_iNumRows; i++ )
		{
			if ( m_aRows[ i ].oValue.ProcessInputEvent( a_pInputEvent ) )
				return TTRUE;
		}
	}

	return BaseClass::ProcessInput( a_pInputEvent );
}

TBOOL AGraphicsOptionsState::ProcessCommand( AInputCommand a_eInputCommand, const Toshi::TInputInterface::InputEvent* a_pInputEvent, TBOOL& a_rStopEvents )
{
	if ( m_eMenuState != MENUSTATE_MENU_VISIBLE )
		return BaseClass::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );

	if ( a_eInputCommand == AInputCommand_Cancel )
	{
		// Cancelled, nothing is applied
		ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUBACK );
		m_eMenuState  = MENUSTATE_MENU_DISAPPEAR;
		m_iFocusedRow = -1;
		return TTRUE;
	}

	if ( a_eInputCommand == AInputCommand_Left || a_eInputCommand == AInputCommand_Right )
	{
		ChangeValue( a_eInputCommand == AInputCommand_Left ? -1 : 1 );
		a_pInputEvent->StartRepeat( 0.5f, 0.1f );
		return TTRUE;
	}

	if ( m_oGridMenu.ProcessInputCommand( a_eInputCommand, a_pInputEvent ) )
		return TTRUE;

	return BaseClass::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );
}

void AGraphicsOptionsState::UpdateLabelScale( AGUI2MenuGridTextItem& a_rLabel, TFLOAT a_fDeltaTime )
{
	// Grow the focused label and shrink it back (ACameraOptionsState)
	T2GUITransform& rTransform = a_rLabel.GetTransform();

	TFLOAT   fScaleX      = rTransform.GetScaleX();
	TFLOAT   fScaleY      = rTransform.GetScaleY();
	TVector2 vTranslation = rTransform.GetTranslation();
	rTransform.SetIdentity();

	if ( &a_rLabel == m_oGridMenu.GetFocusedMenuItem() )
	{
		fScaleX = TMath::LERPClamped( fScaleX, 1.1f, a_fDeltaTime * 10.0f );
		fScaleY = TMath::LERPClamped( fScaleY, 1.1f, a_fDeltaTime * 10.0f );
	}
	else
	{
		fScaleX = TMath::LERPClamped( fScaleX, 1.0f, a_fDeltaTime * 20.0f );
		fScaleY = TMath::LERPClamped( fScaleY, 1.0f, a_fDeltaTime * 20.0f );
	}

	T2GUITransform oScale;
	oScale.Set( TVector2( fScaleX, 0.0f ), TVector2( 0.0f, fScaleY ), TVector2::VEC_ZERO );
	rTransform.PreMultiply( oScale );
	rTransform.SetTranslation( vTranslation );
}

TBOOL AGraphicsOptionsState::OnUpdate( TFLOAT a_fDeltaTime )
{
	// The values can only be focused with the mouse: the focus goes to the label of the row
	if ( AGUI2MenuGridItem* pFocused = m_oGridMenu.GetFocusedMenuItem() )
	{
		for ( TINT i = 0; i < m_iNumRows && m_iFocusedRow != -1; i++ )
		{
			if ( pFocused == &m_aRows[ i ].oValue )
			{
				m_iFocusedRow = i;
				m_oGridMenu.SetFocusAt( &m_aRows[ i ].oLabel );
			}
		}
	}

	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		UpdateLabelScale( m_aRows[ i ].oLabel, a_fDeltaTime );

		// A disabled label keeps its colour when the mouse goes over it
		if ( !m_aRows[ i ].bEnabled && &m_aRows[ i ].oLabel != m_oGridMenu.GetFocusedMenuItem() )
			m_aRows[ i ].oLabel.SetEnabled( TFALSE );
	}

	m_oGridMenu.Update( a_fDeltaTime );
	return BaseClass::OnUpdate( a_fDeltaTime );
}

void AGraphicsOptionsState::OnInsertion()
{
	T2GUIFont* pFont = T2GUIFontManager::FindFont( AGUI2STYLE_FONT_PRIMARY );

	// The resolutions: the modes with the aspect ratio of the desktop, the saved size and the current one
	const AGraphicsSettings::Display&      rcSaved  = AGraphicsSettings::GetSavedDisplay();
	const TRenderInterface::DISPLAYPARAMS* pCurrent = TRenderInterface::GetSingleton()->GetCurrentDisplayParams();
	const TINT                             aiExtraSizes[ 2 ][ 2 ] = { { rcSaved.iWidth, rcSaved.iHeight }, { TINT( pCurrent->uiWidth ), TINT( pCurrent->uiHeight ) } };

	m_iNumSizes = AGraphicsSettings::GetResolutions( m_aiSizes, MAX_CHOICES, aiExtraSizes, TARRAYSIZE( aiExtraSizes ) );

	// The rows of the page, in the order of ROW (the resolution and the display mode before the rows that depend on them)
	m_iNumRows = 0;

	for ( TINT i = 0; i < ROW_NUMOF && m_iNumRows < MAX_ROWS; i++ )
	{
		if ( !IsRowShown( i, m_ePage ) )
			continue;

		Row& rRow     = m_aRows[ m_iNumRows++ ];
		rRow.eRow     = ROW( i );
		rRow.bEnabled = TTRUE;
	}

	TASSERT( m_iNumRows > 0 );

	// The value boxes are as wide as the widest value a row can show
	TFLOAT        fWidestValue = pFont->GetTextWidth( s_wszWidestValue );
	const TWCHAR* wszWidest    = s_wszWidestValue;

	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		Row&           rRow   = m_aRows[ i ];
		const RowInfo& rcInfo = s_aRowInfos[ rRow.eRow ];

		// The value shown when the page opens: the saved display, the console variables as they are now
		TINT iValue = 0;

		switch ( rRow.eRow )
		{
			case ROW_RESOLUTION:
				for ( TINT k = 0; k < m_iNumSizes; k++ )
				{
					if ( m_aiSizes[ k ][ 0 ] == rcSaved.iWidth && m_aiSizes[ k ][ 1 ] == rcSaved.iHeight )
						iValue = k;
				}
				break;
			case ROW_DISPLAYMODE:
				iValue = rcSaved.iDisplayMode;
				break;
			case ROW_HDTEXTURES:
				iValue = AMaterialLibrary::IsTextureOverridesEnabled() ? 1 : 0;
				break;
			default:
				AGraphicsSettings::GetConsoleSetting( rcInfo.szVariable, iValue );
				break;
		}

		SetChoices( rRow, iValue );
		rRow.iInitialValue = rRow.aiValues[ rRow.iChoice ];

		for ( TINT k = 0; k < rRow.iNumChoices; k++ )
		{
			const TFLOAT fWidth = pFont->GetTextWidth( rRow.awszTexts[ k ] );

			if ( fWidth > fWidestValue )
			{
				fWidestValue = fWidth;
				wszWidest    = rRow.awszTexts[ k ];
			}
		}
	}

	m_oGridMenu.CreateGrid( m_iNumRows, 2, 40.0f, ROW_GAP );
	m_oGridMenu.SetCallbackUserData( this );
	m_oGridMenu.SetFocusSound( soundbank::UI_MENUCLICK );
	m_oGridMenu.SetBackSound( soundbank::UI_MENUBACK );
	m_oGridMenu.SetSelectSound( soundbank::UI_MENUOK );

	// A click on a label doesn't close the page (like the Controls page), OK does
	m_oGridMenu.BlockActivation( TTRUE );

	m_oGridMenu.SetItemActivationCallback( []( void* a_pUserData, AGUI2MenuGridItem* a_pItem ) {
		TSTATICCAST( AGraphicsOptionsState, a_pUserData )->m_eMenuState = MENUSTATE_MENU_DISAPPEAR;
	} );

	m_oGridMenu.SetItemFocusCallback( []( void* a_pUserData, AGUI2MenuGridItem* a_pOldFocus, AGUI2MenuGridItem* a_pNewFocus ) {
		TSTATICCAST( AGraphicsOptionsState, a_pUserData )->OnMenuGridItemFocused( *a_pOldFocus, *a_pNewFocus );
	} );

	auto fnArrowFocused = []( void* a_pUserData, TINT a_iRow ) {
		AGraphicsOptionsState* pState = TSTATICCAST( AGraphicsOptionsState, a_pUserData );
		pState->m_oGridMenu.SetFocusAt( &pState->m_aRows[ a_iRow ].oLabel );
		pState->m_iFocusedRow = a_iRow;
	};

	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		Row&           rRow     = m_aRows[ i ];
		const TWCHAR*  wszLabel = AModStrings::Get( s_aRowInfos[ rRow.eRow ].eLabel );
		const TWCHAR*  wszValue = rRow.awszTexts[ rRow.iChoice ];

		rRow.oLabel.Create( pFont, pFont->GetTextWidth( wszLabel ), wszLabel, 10.0f );
		rRow.oLabel.SetFocusedColour( AGUI2STYLE_COLOR_FOCUSED );
		rRow.oLabel.SetEnabledColour( AGUI2STYLE_COLOR_SECONDARY );
		rRow.oLabel.SetDisabledColour( AGUI2STYLE_COLOR_DISABLED );
		rRow.oLabel.SetFocusedScale( 1.0f );
		rRow.oLabel.OnFocusLost();
		rRow.oLabel.SetId( i );
		rRow.oLabel.SetTextShadow( TTRUE, 0xa0000000, s_fShadowAlpha );
		rRow.oLabel.SetTextShadowOffset( 5.0f );
		m_oGridMenu.AddMenuGridItem( rRow.oLabel );

		// Sized for the widest value, then the value of the row
		rRow.oValue.Create( "left_arrow", "left_arrow2", wszWidest );
		rRow.oValue.SetText( wszValue );
		rRow.oValue.SetEnabled( TFALSE );
		rRow.oValue.SetId( i );
		rRow.oValue.SetCallbackUserData( this );
		rRow.oValue.SetArrowFocusedCallback( fnArrowFocused );
		m_oGridMenu.AddMenuGridItem( rRow.oValue );
	}

	// Up and down go through the labels (the values can't be focused)
	for ( TINT i = 0; i < m_iNumRows; i++ )
		m_aRows[ i ].oLabel.SetSideElements( TNULL, TNULL, &m_aRows[ ( i + m_iNumRows - 1 ) % m_iNumRows ].oLabel, &m_aRows[ ( i + 1 ) % m_iNumRows ].oLabel );

	// The widths of the columns are relative to the widest item: the labels get their width, the values theirs
	// NOTE: ACameraOptionsState gives the narrower width to the second column, the values here are wider than the labels
	TFLOAT fMaxLabelWidth  = 0.0f;
	TFLOAT fMaxWidgetWidth = 0.0f;

	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		fMaxLabelWidth  = TMath::Max( fMaxLabelWidth, m_aRows[ i ].oLabel.GetWidth() );
		fMaxWidgetWidth = TMath::Max( fMaxWidgetWidth, m_aRows[ i ].oValue.GetWidth() );
	}

	const TFLOAT fMaxItemWidth = TMath::Max( fMaxLabelWidth, fMaxWidgetWidth );
	m_oGridMenu.SetColumnWidth( 0, fMaxLabelWidth / fMaxItemWidth );
	m_oGridMenu.SetColumnWidth( 1, fMaxWidgetWidth / fMaxItemWidth );

	m_oGridMenu.ReflowChildren();

	// Center the labels in the first column
	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		AGUI2MenuGridTextItem& rLabel = m_aRows[ i ].oLabel;
		rLabel.SetTransform( fMaxLabelWidth * 0.5f + rLabel.GetTransform().GetTranslation().x, rLabel.GetTransform().GetTranslation().y );
		rLabel.SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPCENTER );
	}

	m_oDialog.AddChildTail( m_oGridMenu );

	// What waits for the next start, below the rows. A child of the grid, not one of its items
	m_oInfo.Create( T2GUIFontManager::FindFont( AGUI2STYLE_FONT_SECONDARY ), m_oGridMenu.GetWidth() );
	m_oInfo.SetColour( 0xffffffff );
	m_oInfo.SetShadow( TTRUE, 0xa0000000 );
	m_oInfo.SetShadowOffset( 5.0f, 5.0f );
	m_oInfo.SetAttachment( T2GUIATTACHMENT_TOPCENTER, T2GUIATTACHMENT_TOPCENTER );
	m_oInfo.SetTransform( 0.0f, m_oGridMenu.GetHeight() + INFO_GAP );
	m_oGridMenu.AddChildTail( m_oInfo );

	// A minigame keeps its own draw distance (AGraphicsSettings::IsDrawDistanceLocked)
	if ( Row* pRow = FindRow( ROW_DRAWDISTANCE ) )
	{
		pRow->bEnabled = !AGraphicsSettings::IsDrawDistanceLocked();
		pRow->oLabel.SetEnabled( pRow->bEnabled );
	}

	UpdateDependentRows();
	UpdateInfo();

	// The first row that can be changed
	m_iFocusedRow = 0;

	while ( m_iFocusedRow < m_iNumRows - 1 && !m_aRows[ m_iFocusedRow ].bEnabled )
		m_iFocusedRow++;

	m_oGridMenu.SetFocusAt( &m_aRows[ m_iFocusedRow ].oLabel );

	m_oDialogTitle.SetText( GetButtonText( m_ePage ) );
	m_bFocusedElementBloated = TFALSE;

	AHudManager::GetSingleton()->FindElement<AMenuNavHudElement>()->SetTexts( 1050, 1051, -1 );

	BaseClass::OnInsertion();

	m_oDialog.SetDimensions( m_oGridMenu.GetWidth() + DIALOG_PADDING_X, m_oGridMenu.GetHeight() + INFO_GAP + INFO_HEIGHT + DIALOG_PADDING_Y );

	// The rows and the line together in the middle of the dialog
	m_oGridMenu.SetTransform( 0.0f, -( INFO_GAP + INFO_HEIGHT ) * 0.5f );

	TINFO( "[graphics] %s page opened: %d rows, %d resolutions, label width %.0f, value width %.0f, dialog %.0f x %.0f\n", s_aszPageNames[ m_ePage ], m_iNumRows, m_iNumSizes, fMaxLabelWidth, fMaxWidgetWidth, m_oGridMenu.GetWidth() + DIALOG_PADDING_X, m_oGridMenu.GetHeight() + INFO_GAP + INFO_HEIGHT + DIALOG_PADDING_Y );
}

void AGraphicsOptionsState::OnRemoval()
{
	m_oInfo.RemoveSelf();

	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		m_aRows[ i ].oValue.RemoveElements();
		m_aRows[ i ].oLabel.RemoveSelf();
	}

	m_oGridMenu.RemoveSelf();

	AMenuNavHudElement::UpdateText();

	BaseClass::OnRemoval();
}

void AGraphicsOptionsState::OnActivate()
{
	// NOTE: OnInsertion has focused the first row that can be changed
	BaseClass::OnActivate();
}

void AGraphicsOptionsState::OnMenuItemActivated( AGUI2MenuItem* a_pMenuItem )
{
	ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUOK );
	ABYardMenuState::OnButtonActivated( TSTATICCAST( AGUI2Button, a_pMenuItem ) );
}

void AGraphicsOptionsState::OnMenuItemFocused( AGUI2MenuItem& a_rOldFocus, AGUI2MenuItem& a_rNewFocus )
{
}

void AGraphicsOptionsState::OnMenuGridItemFocused( AGUI2MenuGridItem& a_rOldFocus, AGUI2MenuGridItem& a_rNewFocus )
{
	if ( m_iFocusedRow == -1 )
		return;

	// Only the labels are focused, the values pass the focus on in OnUpdate
	for ( TINT i = 0; i < m_iNumRows; i++ )
	{
		if ( &a_rNewFocus == &m_aRows[ i ].oLabel || &a_rNewFocus == &m_aRows[ i ].oValue )
			m_iFocusedRow = i;
	}
}

void AGraphicsOptionsState::OnMenuClose()
{
	// OK: what changed is saved and applied. Cancel (m_iFocusedRow -1): nothing
	if ( m_iFocusedRow != -1 )
		Apply();
	else
		TINFO( "[graphics] %s page: cancelled\n", s_aszPageNames[ m_ePage ] );

	BaseClass::Remove();
}

#endif // BARNYARD_COMMUNITY_PATCH
