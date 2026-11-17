#include "pch.h"
#include "AOptionsState.h"
#include "Sound/ASoundManager.h"
#include "SoundBank/ui.h"
#include "GUI/T2GUIFontManager.h"
#include "Locale/ALocaleManager.h"
#include "AGameStateController.h"
#include "AControlOptionsState.h"
#include "AAudioOptionsState.h"
#include "ACameraOptionsState.h"
#include "ADifficultyOptionState.h"
#include "AFrontEndMainMenuState2.h"
#include "AGraphicsOptionsState.h"
#include "SaveLoadSKU.h"
#include "Helpers/APlayerProgress.h"

#ifdef BARNYARD_COMMUNITY_PATCH
#  include "AFrontEndLanguageState.h"
#  include "AOptions.h"
#  include "Locale/AModStrings.h"
#endif

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( AOptionsState );

#ifdef BARNYARD_COMMUNITY_PATCH

// NOTE: there are no locale strings for the HD textures button, its texts are texts of the mod (AModStrings).
// "(restart)": the saved setting differs from the one this run loaded its textures with
static const MODSTRING s_aeHDTexturesTexts[ 4 ] = {
	MODSTRING_OPTIONS_HD_OFF,
	MODSTRING_OPTIONS_HD_ON,
	MODSTRING_OPTIONS_HD_OFF_RESTART,
	MODSTRING_OPTIONS_HD_ON_RESTART
};

// The HD textures and the language buttons are only in the options of the frontend (the main menu is below them then):
// a library takes the HD textures setting when it's loaded, so in a game it would only change what is loaded after it,
// and the texts of a game can't be switched (they're everywhere)
static TBOOL IsButtonShown( TINT a_iButton )
{
	// The Effects page has rows only when the backend has effects (DX11), Graphics is always there
	if ( a_iButton == AOptionsState::OPTIONSBUTTON_EFFECTS )
		return AGraphicsOptionsState::HasRows( AGraphicsOptionsState::PAGE_EFFECTS );

	return ( a_iButton != AOptionsState::OPTIONSBUTTON_HDTEXTURES && a_iButton != AOptionsState::OPTIONSBUTTON_LANGUAGE ) || AFrontEndMainMenuState2::ms_bIsInserted;
}

// "Language: Magyar": the picked language (the language of the texts if none was picked), with "(restart)" while the
// texts aren't in the picked language yet and "(hun.trb is missing)" if its file isn't there
static const TWCHAR* GetLanguageButtonText()
{
	static TWCHAR s_wszText[ 128 ];

	T2Locale::Lang eLanguage = AOptions::GetLanguageSetting();

	if ( !ALocaleManager::IsLanguageSupported( eLanguage ) )
		eLanguage = g_pLocaleManager->GetTextLanguage();

	TStringManager::String16Format( s_wszText, TARRAYSIZE( s_wszText ), L"%s: %s", g_pLocaleManager->GetString( 1120 ), ALocaleManager::GetLanguageNativeName( eLanguage ) );

	TWCHAR wszNote[ 64 ];
	wszNote[ 0 ] = L'\0';

	if ( !ALocaleManager::IsLanguageFilePresent( eLanguage ) )
	{
		// The name of the file without the folder
		const TCHAR* szFile  = ALocaleManager::s_pLangDataFileNames[ eLanguage ];
		const TCHAR* szSlash = strrchr( szFile, '/' );

		TWCHAR wszFile[ 32 ];
		TStringManager::StringCharToUnicode( wszFile, szSlash ? szSlash + 1 : szFile, -1 );
		TStringManager::String16Format( wszNote, TARRAYSIZE( wszNote ), AModStrings::Get( MODSTRING_OPTIONS_LANGUAGE_MISSING ), wszFile );
	}
	else if ( eLanguage != g_pLocaleManager->GetTextLanguage() )
	{
		TStringManager::String16CopySafe( wszNote, AModStrings::Get( MODSTRING_OPTIONS_LANGUAGE_RESTART ), TARRAYSIZE( wszNote ) );
	}

	if ( wszNote[ 0 ] != L'\0' )
	{
		const TINT iLength = TINT( TStringManager::String16Length( s_wszText ) );
		TStringManager::String16Format( s_wszText + iLength, TARRAYSIZE( s_wszText ) - iLength, L" %s", wszNote );
	}

	return s_wszText;
}

#endif // BARNYARD_COMMUNITY_PATCH

// $Barnyard: FUNCTION 00444ef0
AOptionsState::AOptionsState()
{
}

// $Barnyard: FUNCTION 00444f50
// $Barnyard: FUNCTION 00445170
AOptionsState::~AOptionsState()
{
}

// $Barnyard: FUNCTION 00407aa0
TBOOL AOptionsState::ProcessCommand( AInputCommand a_eInputCommand, const Toshi::TInputInterface::InputEvent* a_pInputEvent, TBOOL& a_rStopEvents )
{
	if ( m_eMenuState == MENUSTATE_MENU_VISIBLE && a_eInputCommand == AInputCommand_Cancel )
		ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUBACK );

	return BaseClass::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );
}

TBOOL AOptionsState::OnUpdate( TFLOAT a_fDeltaTime )
{
	return BaseClass::OnUpdate( a_fDeltaTime );
}

// NOTE: inlined in OnInsertion
const TWCHAR* AOptionsState::GetButtonText( TINT a_iButton )
{
#ifdef BARNYARD_LATEST
	// NOTE: the Wii skips OPTIONSBUTTON_CONTROLS here (no mouse), the PC keeps it
	if ( a_iButton == OPTIONSBUTTON_CAMERA )
		return ACameraOptionsState::GetCameraText();
	else if ( a_iButton == OPTIONSBUTTON_DIFFICULTY )
		return ADifficultyOptionState::GetText( OPTIONSBUTTON_TEXTS[ a_iButton ] );
#endif

#ifdef BARNYARD_COMMUNITY_PATCH
	// The setting the button switches
	if ( a_iButton == OPTIONSBUTTON_HDTEXTURES )
	{
		const TINT iText = ( AMaterialLibrary::IsTextureOverridesEnabled() ? 1 : 0 ) + ( AMaterialLibrary::IsTextureOverridesRestartNeeded() ? 2 : 0 );
		return AModStrings::Get( s_aeHDTexturesTexts[ iText ] );
	}

	if ( a_iButton == OPTIONSBUTTON_GRAPHICS )
		return AGraphicsOptionsState::GetButtonText( AGraphicsOptionsState::PAGE_GRAPHICS );

	if ( a_iButton == OPTIONSBUTTON_DETAIL )
		return AGraphicsOptionsState::GetButtonText( AGraphicsOptionsState::PAGE_DETAIL );

	if ( a_iButton == OPTIONSBUTTON_EFFECTS )
		return AGraphicsOptionsState::GetButtonText( AGraphicsOptionsState::PAGE_EFFECTS );
	if ( a_iButton == OPTIONSBUTTON_LANGUAGE )
		return GetLanguageButtonText();
#endif

	return g_pLocaleManager->GetString( OPTIONSBUTTON_TEXTS[ a_iButton ] );
}

TPSTRING8_DECLARE( DialogInputMap );

// $Barnyard: FUNCTION 00444fc0
// $BarnyardWiiJP: FUNCTION 8015736c
void AOptionsState::OnInsertion()
{
	SetInputMap( TPS8( DialogInputMap ) );
	T2GUIFont* pFont = T2GUIFontManager::FindFont( AGUI2STYLE_FONT_PRIMARY );

	for ( TINT i = 0; i < OPTIONSBUTTON_NUMOF; i++ )
	{
#ifdef BARNYARD_COMMUNITY_PATCH
		if ( !IsButtonShown( i ) )
			continue;
#endif

		const TWCHAR* wszText = GetButtonText( i );

		m_aButtons[ i ].Create( pFont, pFont->GetTextWidth( wszText ), wszText, 10.0f );
		m_aButtons[ i ].SetFocusedColour( AGUI2STYLE_COLOR_FOCUSED );
		m_aButtons[ i ].SetEnabledColour( AGUI2STYLE_COLOR_SECONDARY );
		m_aButtons[ i ].SetDisabledColour( AGUI2STYLE_COLOR_DISABLED );
		m_aButtons[ i ].SetFocusedScale( 1.0f );
		m_aButtons[ i ].OnFocusLost();
		m_aButtons[ i ].SetId( i );
		m_aButtons[ i ].SetTextShadow( TTRUE, 0xa0000000, 0.6f );
		m_aButtons[ i ].SetTextShadowOffset( 5.0f );
		m_oMenu.AddMenuItem( m_aButtons[ i ] );
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	if ( IsButtonShown( OPTIONSBUTTON_HDTEXTURES ) )
	{
		// Wide enough for all of its texts: the menu and the dialog keep their size when the button is switched
		TFLOAT fWidth = 0.0f;

		for ( TINT i = 0; i < TARRAYSIZE( s_aeHDTexturesTexts ); i++ )
			fWidth = TMath::Max( fWidth, pFont->GetTextWidth( AModStrings::Get( s_aeHDTexturesTexts[ i ] ) ) );

		m_aButtons[ OPTIONSBUTTON_HDTEXTURES ].GetTextBox().SetWidth( fWidth );
	}
#endif

	m_oMenu.SetFocusAt( m_aButtons[ 0 ] );
	m_oDialogTitle.SetText( g_pLocaleManager->GetString( 284 ) );

	BaseClass::OnInsertion();
}

// $Barnyard: FUNCTION 004450f0
void AOptionsState::OnRemoval()
{
	for ( TINT i = 0; i < OPTIONSBUTTON_NUMOF; i++ )
	{
		m_aButtons[ i ].RemoveSelf();
	}

	BaseClass::OnRemoval();
}

// $Barnyard: FUNCTION 00445130
void AOptionsState::OnActivate()
{
	m_iActivatedButtonID = -1;
	BaseClass::OnActivate();
}

// $Barnyard: FUNCTION 00445150
void AOptionsState::OnMenuItemActivated( AGUI2MenuItem* a_pMenuItem )
{
	AGUI2Button* pButton = TSTATICCAST( AGUI2Button, a_pMenuItem );

#ifdef BARNYARD_COMMUNITY_PATCH
	if ( pButton->GetId() == OPTIONSBUTTON_HDTEXTURES )
	{
		// The saved setting is switched (it applies from the next start), the menu stays open and the button
		// shows the new setting, with "(restart)" while it differs from the one this run loaded with
		AMaterialLibrary::SetTextureOverridesEnabled( !AMaterialLibrary::IsTextureOverridesEnabled() );
		pButton->SetText( GetButtonText( OPTIONSBUTTON_HDTEXTURES ), TFALSE );
		return;
	}
#endif

	m_iActivatedButtonID = pButton->GetId();

	ABYardMenuState::OnButtonActivated( pButton );
}

// $Barnyard: FUNCTION 00445190
// $BarnyardWiiJP: FUNCTION 80157614
void AOptionsState::OnMenuClose()
{
	if ( m_iActivatedButtonID != -1 )
	{
		AGameState* pState = TNULL;

		if ( m_iActivatedButtonID == OPTIONSBUTTON_AUDIO )
			pState = new AAudioOptionsState();
		else if ( m_iActivatedButtonID == OPTIONSBUTTON_CONTROLS )
			pState = new AControlOptionsState();
#ifdef BARNYARD_COMMUNITY_PATCH
		else if ( m_iActivatedButtonID == OPTIONSBUTTON_LANGUAGE )
			pState = new AFrontEndLanguageState( TNULL, TFALSE );
#endif
#ifdef BARNYARD_LATEST
		else if ( m_iActivatedButtonID == OPTIONSBUTTON_CAMERA )
			pState = new ACameraOptionsState();
		else if ( m_iActivatedButtonID == OPTIONSBUTTON_DIFFICULTY )
			pState = new ADifficultyOptionState();
#endif
#ifdef BARNYARD_COMMUNITY_PATCH
		else if ( m_iActivatedButtonID == OPTIONSBUTTON_GRAPHICS )
			pState = new AGraphicsOptionsState( AGraphicsOptionsState::PAGE_GRAPHICS );
		else if ( m_iActivatedButtonID == OPTIONSBUTTON_DETAIL )
			pState = new AGraphicsOptionsState( AGraphicsOptionsState::PAGE_DETAIL );
		else if ( m_iActivatedButtonID == OPTIONSBUTTON_EFFECTS )
			pState = new AGraphicsOptionsState( AGraphicsOptionsState::PAGE_EFFECTS );
#endif

		AGameStateController::GetSingleton()->PushState( pState );
		return;
	}

	AGameState::Remove();

	if ( ms_bSettingsChanged )
	{
		// Save the changed settings
		AGameStateController::GetSingleton()->SaveCurrentGameState( TFALSE, TFALSE, TTRUE );

		ms_bSettingsChanged = TFALSE;
	}
}

#ifdef BARNYARD_COMMUNITY_PATCH

// $BarnyardPS2EU: FUNCTION 001609b8
void AOptionsState::UpdateTexts()
{
	for ( TINT i = 0; i < OPTIONSBUTTON_NUMOF; i++ )
	{
		// NOTE: the PS2 has no HD textures button
		if ( !IsButtonShown( i ) )
			continue;

		m_aButtons[ i ].SetText( GetButtonText( i ), TTRUE );

		// NOTE: the title is set again for every button
		m_oDialogTitle.SetTextAndFitWidth( g_pLocaleManager->GetString( 284 ) );
		m_oDialogTitleBackground.SetWidth( m_oDialogTitle.GetWidth() * 1.25f );
	}

	m_oMenu.ReflowChildrenVertically();

	TFLOAT fMenuWidth, fMenuHeight;
	m_oMenu.GetDimensions( fMenuWidth, fMenuHeight );

	if ( fMenuHeight < 128.0f )
		fMenuHeight = 128.0f;

	if ( fMenuWidth < 384.0f )
		fMenuWidth = 384.0f;

	m_oDialog.SetDialogSize( fMenuWidth + 128.0f, fMenuHeight + 128.0f );
}

#endif // BARNYARD_COMMUNITY_PATCH
