#include "pch.h"
#include "AFrontEndCoopState.h"
#include "AFrontEndNewGameSelectState.h"
#include "AFrontEndLoadState.h"
#include "AGameStateController.h"
#include "SaveLoadSKU.h"
#include "Helpers/APlayerProgress.h"
#include "Player/ACoop.h"
#include "Player/ACoopNet.h"
#include "Sound/ASoundManager.h"
#include "SoundBank/ui.h"
#include "GUI/AGUI2Style.h"
#include "GUI/T2GUIFontManager.h"
#include "Locale/ALocaleManager.h"
#include "Locale/AModStrings.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( AFrontEndCoopState );

// NOTE: the locale tables have no texts for this menu, they're texts of the mod (AModStrings)

// The buttons of PAGE_MODE, one for each mode
static const MODSTRING s_aeModeButtons[ AFrontEndCoopState::MODE_NUMOF ] = {
	MODSTRING_COOP_HOST,
	MODSTRING_COOP_JOIN,
	MODSTRING_COOP_SAMESCREEN
};

// The longest text ACoopNet::GetStatusText can give (with the longest address), the line of PAGE_START has room for it
static void GetLongestStatusText( TWCHAR* a_wszOut, TINT a_iSize )
{
	static const MODSTRING s_aeStatusTexts[] = {
		MODSTRING_COOPNET_HOSTING_CONNECTED,
		MODSTRING_COOPNET_HOSTING_ON,
		MODSTRING_COOPNET_HOSTING,
		MODSTRING_COOPNET_CONNECTED,
		MODSTRING_COOPNET_LOOKING
	};

	a_wszOut[ 0 ] = L'\0';

	for ( TINT i = 0; i < TARRAYSIZE( s_aeStatusTexts ); i++ )
	{
		TWCHAR wszText[ AFrontEndCoopState::INFO_SIZE ];
		TStringManager::String16Format( wszText, TARRAYSIZE( wszText ), AModStrings::Get( s_aeStatusTexts[ i ] ), L"255.255.255.255" );

		if ( TStringManager::String16Length( wszText ) > TStringManager::String16Length( a_wszOut ) )
			TStringManager::String16CopySafe( a_wszOut, wszText, a_iSize );
	}
}

// ...and what the log calls the modes
static const TCHAR* s_aszModeNames[ AFrontEndCoopState::MODE_NUMOF ] = {
	"Host LAN Game",
	"Join LAN Game",
	"Same Screen"
};

// Starts the network session of a_eMode (MODE_HOST or MODE_JOIN). Returns TFALSE if it can't be started.
// NOTE: a session the command line started in that mode is kept as it is: Host and Join would replace it
// with one on the default port that looks for the host on the whole LAN
static TBOOL StartNetSession( AFrontEndCoopState::MODE a_eMode )
{
	const TBOOL bHost = a_eMode == AFrontEndCoopState::MODE_HOST;

	if ( ACoopNet::IsForcedByCommandLine() && ACoopNet::GetMode() == ( bHost ? ACoopNet::MODE_HOST : ACoopNet::MODE_JOIN ) )
		return TTRUE;

	return bHost ? ACoopNet::Host() : ACoopNet::Join();
}

AFrontEndCoopState::AFrontEndCoopState( PAGE a_ePage, MODE a_eMode )
    : m_iActivatedButtonID( -1 )
    , m_ePage( a_ePage )
    , m_eMode( a_eMode )
    , m_iControllerNumber( -1 )
{
	m_wszInfo[ 0 ] = L'\0';
}

AFrontEndCoopState::~AFrontEndCoopState()
{
}

TBOOL AFrontEndCoopState::ProcessCommand( AInputCommand a_eInputCommand, const TInputInterface::InputEvent* a_pInputEvent, TBOOL& a_rStopEvents )
{
	if ( m_eMenuState == MENUSTATE_MENU_VISIBLE && a_eInputCommand == AInputCommand_Cancel )
		ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUBACK );

	return BaseClass::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );
}

TBOOL AFrontEndCoopState::OnUpdate( TFLOAT a_fDeltaTime )
{
	if ( m_ePage == PAGE_START )
	{
		// Unplugging a controller is noticed while the menu is open. One that's plugged in is only seen after a restart
		// (the input interface looks for controllers once, at startup). The other game can come and go at any time
		UpdateInfo();

		// The line isn't a menu item, so the menu doesn't fade it
		m_oInfo.SetAlpha( m_fMenuOpacity );
		m_oInfo.SetShadowAlpha( m_fMenuOpacity * s_fShadowAlpha );
	}

	// NOTE: the state can be gone once this returns (OnMenuClose removes it)
	return BaseClass::OnUpdate( a_fDeltaTime );
}

void AFrontEndCoopState::OnInsertion()
{
	if ( m_ePage == PAGE_MODE )
		TINFO( "[coop] Co-op menu opened (mode page)\n" );
	else
		TINFO( "[coop] Co-op menu opened (start page, %s, second player: controller %d)\n", s_aszModeNames[ m_eMode ], ACoop::GetSecondPlayerControllerNumber() );

	T2GUIFont* pFont = T2GUIFontManager::FindFont( AGUI2STYLE_FONT_PRIMARY );

	// NOTE: PAGE_MODE has a button for each mode, the ID of the button is the mode
	for ( TINT i = 0; i < GetNumButtons(); i++ )
	{
		const TWCHAR* wszText = ( m_ePage == PAGE_MODE ) ? AModStrings::Get( s_aeModeButtons[ i ] ) : g_pLocaleManager->GetString( COOPBUTTON_TEXTS[ i ] );

		m_aButtons[ i ].Create( pFont, pFont->GetTextWidth( wszText ), wszText, 10.0f );
		m_aButtons[ i ].SetFocusedColour( AGUI2STYLE_COLOR_FOCUSED );
		m_aButtons[ i ].SetEnabledColour( AGUI2STYLE_COLOR_SECONDARY );
		m_aButtons[ i ].SetDisabledColour( AGUI2STYLE_COLOR_DISABLED );
		m_aButtons[ i ].SetFocusedScale( 1.0f );
		m_aButtons[ i ].OnFocusLost();
		m_aButtons[ i ].SetId( i );
		m_aButtons[ i ].SetTextShadow( TTRUE, 0xa0000000, s_fShadowAlpha );
		m_aButtons[ i ].SetTextShadowOffset( 5.0f );
		m_oMenu.AddMenuItem( m_aButtons[ i ] );
	}

	m_oMenu.SetFocusAt( m_aButtons[ 0 ] );
	m_oDialogTitle.SetText( AModStrings::Get( MODSTRING_COOP_TITLE ) );

	// Fits the menu and the dialog to the buttons, which is all PAGE_MODE has
	BaseClass::OnInsertion();

	if ( m_ePage == PAGE_MODE )
		return;

	// The controller of the second player or the state of the network session, below the buttons.
	// It's a child of the menu like the message of AMessagePopupState, not one of its items
	T2GUIFont* pInfoFont = T2GUIFontManager::FindFont( AGUI2STYLE_FONT_SECONDARY );

	TFLOAT fMenuWidth, fMenuHeight;
	m_oMenu.GetDimensions( fMenuWidth, fMenuHeight );

	m_oInfo.Create( pInfoFont, INFO_WIDTH );
	m_oInfo.SetColour( 0xffffffff );
	m_oInfo.SetShadow( TTRUE, 0xa0000000 );
	m_oInfo.SetShadowOffset( 5.0f, 5.0f );
	m_oInfo.SetAttachment( T2GUIATTACHMENT_TOPCENTER, T2GUIATTACHMENT_TOPCENTER );
	m_oInfo.SetTransform( 0.0f, fMenuHeight + INFO_GAP );
	m_oMenu.AddChildTail( m_oInfo );

	// Make the menu and the dialog fit the line too. There's room for the longest text of the mode (two lines),
	// so they don't change when a controller comes or goes, or when the other game does
	TWCHAR wszLongestStatus[ INFO_SIZE ];
	GetLongestStatusText( wszLongestStatus, INFO_SIZE );

	const TWCHAR* wszLongestInfo = ( m_eMode == MODE_SAMESCREEN ) ? AModStrings::Get( MODSTRING_COOP_NOCONTROLLER ) : wszLongestStatus;

	fMenuWidth  = TMath::Max( fMenuWidth, INFO_WIDTH );
	fMenuHeight = fMenuHeight + INFO_GAP + pInfoFont->GetTextHeightWrapped( wszLongestInfo, INFO_WIDTH ) + INFO_GAP;

	m_oMenu.SetDimensions( fMenuWidth, fMenuHeight );
	m_oDialog.SetDialogSize( TMath::Max( fMenuWidth, 384.0f ) + 128.0f, TMath::Max( fMenuHeight, 128.0f ) + 128.0f );
}

void AFrontEndCoopState::OnRemoval()
{
	m_oInfo.RemoveSelf();

	for ( TINT i = 0; i < MAX_NUM_BUTTONS; i++ )
		m_aButtons[ i ].RemoveSelf();

	BaseClass::OnRemoval();
}

void AFrontEndCoopState::OnActivate()
{
	// NOTE: the co-op request isn't cleared and the network session isn't ended here. AFrontEndLoadState pops itself
	// before it starts the saved game, which activates PAGE_START on the way into the game. Backing out of PAGE_START
	// does that (OnMenuClose), and so does the main menu when it's activated
	m_iActivatedButtonID = -1;

	if ( m_ePage == PAGE_START )
		UpdateInfo();

	BaseClass::OnActivate();

	if ( m_ePage == PAGE_START )
	{
		// Hidden until the menu fades in. That never happens on the way into a saved game: the state isn't
		// updated while the screen fades out
		m_oInfo.SetAlpha( 0.0f );
		m_oInfo.SetShadowAlpha( 0.0f );
	}
}

void AFrontEndCoopState::OnMenuItemActivated( AGUI2MenuItem* a_pMenuItem )
{
	AGUI2Button* pButton = TSTATICCAST( AGUI2Button, a_pMenuItem );

	if ( m_ePage == PAGE_MODE )
	{
		const MODE eMode = MODE( pButton->GetId() );

		TINFO( "[coop] Co-op menu: %s\n", s_aszModeNames[ eMode ] );

		// The network session starts right away, the other game can be found while this one is still in the menu
		if ( eMode != MODE_SAMESCREEN && !StartNetSession( eMode ) )
		{
			// It can't be started (hosting: the port is in use, another game hosts on this computer).
			// Disallow the selection like AFrontEndPlayersState does: the menu stays as it is
			ASoundManager::GetSingleton()->PlayCue( soundbank::UI_NEGATIVE );
			return;
		}
	}

	m_iActivatedButtonID = pButton->GetId();

	ABYardMenuState::OnButtonActivated( pButton );
}

void AFrontEndCoopState::OnMenuClose()
{
	if ( m_iActivatedButtonID == -1 )
	{
		if ( m_ePage == PAGE_START )
		{
			// Back to PAGE_MODE: no game is started in this mode. The session of the command line stays
			if ( m_eMode == MODE_SAMESCREEN )
				ACoop::SetRequested( TFALSE );
			else if ( !ACoopNet::IsForcedByCommandLine() )
				ACoopNet::Stop();
		}

		// PAGE_MODE: back to the main menu
		Remove();
		return;
	}

	if ( m_ePage == PAGE_MODE )
	{
		// New Game and Load Game in the mode of the activated button.
		// Backing out of that page removes its state, which activates this one again
		AGameStateController::GetSingleton()->PushState( new AFrontEndCoopState( PAGE_START, MODE( m_iActivatedButtonID ) ) );
		return;
	}

	// The game the player starts from here has a second player on the same screen.
	// NOTE: not with a network session, which is already running: the other player is in its own game
	if ( m_eMode == MODE_SAMESCREEN )
		ACoop::SetRequested( TTRUE );

	// What the same buttons of the main menu push (AFrontEndMainMenuState2::OnMenuClose).
	// Backing out of the slots removes their state, which activates this menu again
	SaveLoadSKU* pSaveLoad;

	if ( m_iActivatedButtonID == COOPBUTTON_NEWGAME )
	{
		TINFO( "[coop] Co-op menu: New Game with a second player (%s)\n", s_aszModeNames[ m_eMode ] );

		// Load the save file, then choose the slot of the new game
		pSaveLoad = new SaveLoadSKU( APlayerProgress::GetSingleton(), TFALSE, new AFrontEndNewGameSelectState() );
		pSaveLoad->StartMode3();
	}
	else
	{
		TINFO( "[coop] Co-op menu: Load Game with a second player (%s)\n", s_aszModeNames[ m_eMode ] );

		// Load the save file, then choose the slot to continue from
		pSaveLoad = new SaveLoadSKU( APlayerProgress::GetSingleton(), TFALSE, new AFrontEndLoadState() );
		pSaveLoad->StartMode2();
	}

	AGameStateController::GetSingleton()->PushState( pSaveLoad );
}

void AFrontEndCoopState::UpdateInfo()
{
	if ( m_eMode != MODE_SAMESCREEN )
	{
		// Whether the other game is there yet
		TWCHAR wszStatus[ INFO_SIZE ];
		ACoopNet::GetStatusText( wszStatus, INFO_SIZE );

		if ( TStringManager::String16Compare( wszStatus, m_wszInfo ) != 0 )
		{
			TStringManager::String16Copy( m_wszInfo, wszStatus );
			m_oInfo.SetText( m_wszInfo );
		}

		return;
	}

	const TINT iControllerNumber = ACoop::GetSecondPlayerControllerNumber();

	if ( iControllerNumber == m_iControllerNumber )
		return;

	m_iControllerNumber = iControllerNumber;

	if ( iControllerNumber > 0 )
	{
		TStringManager::String16Format( m_wszInfo, TARRAYSIZE( m_wszInfo ), AModStrings::Get( MODSTRING_COOP_CONTROLLER ), iControllerNumber );
		m_oInfo.SetText( m_wszInfo );
	}
	else
	{
		m_oInfo.SetText( AModStrings::Get( MODSTRING_COOP_NOCONTROLLER ) );
	}
}
