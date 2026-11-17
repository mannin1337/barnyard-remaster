#pragma once
#include "ABYardMenuState.h"
#include "GUI/AGUI2Button.h"
#include "GUI/AGUI2TextBox.h"

//-----------------------------------------------------------------------------
// Purpose: the Co-op menu of the frontend, opened by the Co-op button of the
// main menu. It has two pages, each one is a state of this class:
//  - PAGE_MODE: where the second player plays: in its own game on the LAN
//    (ACoopNet, this game hosts or joins) or in this one, on the same screen
//    (ACoop).
//  - PAGE_START: New Game and Load Game start what the same buttons of the
//    main menu start, with the second player of the mode chosen on PAGE_MODE.
// NOTE: not in any build of the original game.
//-----------------------------------------------------------------------------
class AFrontEndCoopState
    : public ABYardMenuState
{
public:
	TDECLARE_CLASS( AFrontEndCoopState, ABYardMenuState );

	enum PAGE
	{
		PAGE_MODE,
		PAGE_START
	};

	// Where the second player plays. PAGE_MODE has a button for each one, in this order
	enum MODE
	{
		MODE_HOST,       // in its own game on the LAN, this game hosts (ACoopNet)
		MODE_JOIN,       // in its own game on the LAN, this game looks for the host (ACoopNet)
		MODE_SAMESCREEN, // in this game, with its own controller (ACoop)
		MODE_NUMOF
	};

	// The buttons of PAGE_START
	enum COOPBUTTON
	{
		COOPBUTTON_NEWGAME,
		COOPBUTTON_LOADGAME,
		COOPBUTTON_NUMOF
	};

	// The locale strings the main menu has on the same buttons
	inline constexpr static TINT COOPBUTTON_TEXTS[ COOPBUTTON_NUMOF ] = {
		292,
		293
	};

	// PAGE_MODE is the page with the most buttons
	inline constexpr static TINT MAX_NUM_BUTTONS = MODE_NUMOF;

	// Width of the line below the buttons of PAGE_START: the controller of the second player or the state of the
	// network session. With Rekord18 the longer controller text wraps after its first sentence between 362 and 421
	inline constexpr static TFLOAT INFO_WIDTH = 412.0f;

	// Space above and below that line
	inline constexpr static TFLOAT INFO_GAP = 6.0f;

	// Size of the text of that line, the state of the network session is the longest one
	inline constexpr static TINT INFO_SIZE = 160;

public:
	// NOTE: the main menu opens PAGE_MODE, which opens PAGE_START in the mode of the activated button
	AFrontEndCoopState( PAGE a_ePage = PAGE_MODE, MODE a_eMode = MODE_SAMESCREEN );
	~AFrontEndCoopState();

	//-----------------------------------------------------------------------------
	// AGameState
	//-----------------------------------------------------------------------------
	virtual TBOOL ProcessCommand( AInputCommand a_eInputCommand, const Toshi::TInputInterface::InputEvent* a_pInputEvent, TBOOL& a_rStopEvents ) OVERRIDE;
	virtual TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual void  OnInsertion() OVERRIDE;
	virtual void  OnRemoval() OVERRIDE;
	virtual void  OnActivate() OVERRIDE;

	//-----------------------------------------------------------------------------
	// ABYardMenuState
	//-----------------------------------------------------------------------------
	virtual void OnMenuItemActivated( AGUI2MenuItem* a_pMenuItem ) OVERRIDE;
	virtual void OnMenuClose() OVERRIDE;

private:
	TINT GetNumButtons() const { return ( m_ePage == PAGE_MODE ) ? TINT( MODE_NUMOF ) : TINT( COOPBUTTON_NUMOF ); }

	// PAGE_START: shows the controller the second player would get if the game started now (MODE_SAMESCREEN)
	// or the state of the network session (MODE_HOST, MODE_JOIN)
	void UpdateInfo();

private:
	AGUI2Button  m_aButtons[ MAX_NUM_BUTTONS ];
	TINT         m_iActivatedButtonID;
	PAGE         m_ePage;
	MODE         m_eMode;                // only means something on PAGE_START
	AGUI2TextBox m_oInfo;                // PAGE_START only
	TWCHAR       m_wszInfo[ INFO_SIZE ]; // the text m_oInfo shows, unless it's one of the constant ones
	TINT         m_iControllerNumber;    // MODE_SAMESCREEN: the one m_oInfo shows, -1 before the first update
};

TSTATICASSERT( AFrontEndCoopState::COOPBUTTON_NUMOF <= AFrontEndCoopState::MAX_NUM_BUTTONS );
