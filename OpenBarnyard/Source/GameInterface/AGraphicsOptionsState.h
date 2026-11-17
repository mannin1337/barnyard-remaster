#pragma once
#include "ABYardMenuState.h"
#include "GUI/AGUI2MenuGridTextItem.h"
#include "AControlOptionsState.h"

#ifdef BARNYARD_COMMUNITY_PATCH

//-----------------------------------------------------------------------------
// Purpose: the Graphics and Effects pages of the options (AOptionsState), a row
// for each setting of AGraphicsSettings: a label and a choice between arrows.
// NOTE: not in any version of the game. Made from ACameraOptionsState (rows of
// AToggleSlider choices). The locale tables don't have its texts: they're texts
// of the mod (AModStrings, MODSTRING_GRAPHICS_...). Left/Right change the shown
// value, OK applies and saves the changed ones, Cancel leaves everything as it
// was. A line below the rows tells what waits for the next start.
// Graphics: the display and the filtering. Detail: draw distance and grass.
// Effects: the DX11 effects.
// Rows of console variables the backend doesn't have are hidden: the Effects
// page (the DX11 effects) has no rows on DX8, its button isn't shown then.
//-----------------------------------------------------------------------------
class AGraphicsOptionsState : public ABYardMenuState
{
public:
	TDECLARE_CLASS( AGraphicsOptionsState, ABYardMenuState );

	enum PAGE
	{
		PAGE_GRAPHICS,
		PAGE_DETAIL,
		PAGE_EFFECTS,
		PAGE_NUMOF
	};

	enum ROW
	{
		// PAGE_GRAPHICS
		ROW_RESOLUTION,
		ROW_DISPLAYMODE,
		ROW_REFRESHRATE,
		ROW_VSYNC,
		ROW_FPSLIMIT,
		ROW_ANTIALIASING,
		ROW_TEXTUREFILTER,
		ROW_HDTEXTURES,
		// PAGE_DETAIL
		ROW_DRAWDISTANCE,
		ROW_GRASSDENSITY,
		// PAGE_EFFECTS
		ROW_AO,
		ROW_AOQUALITY,
		ROW_BLOOM,
		ROW_DYNAMICLIGHTS,
		ROW_DYNAMICLIGHTSHADOWS,
		ROW_SUNSHADOWS,
		ROW_SUNSHAFTS,
		ROW_VOLUMETRICFOG,
		ROW_NUMOF
	};

	static constexpr TINT MAX_ROWS    = 8;
	static constexpr TINT MAX_CHOICES = 48;
	static constexpr TINT MAX_TEXT    = 24;

public:
	AGraphicsOptionsState( PAGE a_ePage = PAGE_GRAPHICS );
	~AGraphicsOptionsState();

	// Text of the button of a_ePage in the options menu
	static const TWCHAR* GetButtonText( PAGE a_ePage );

	// TTRUE if a_ePage has rows to show
	static TBOOL HasRows( PAGE a_ePage );

	//-----------------------------------------------------------------------------
	// AGameState
	//-----------------------------------------------------------------------------
	virtual TBOOL ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent ) OVERRIDE;
	virtual TBOOL ProcessCommand( AInputCommand a_eInputCommand, const Toshi::TInputInterface::InputEvent* a_pInputEvent, TBOOL& a_rStopEvents ) OVERRIDE;
	virtual TBOOL OnUpdate( TFLOAT a_fDeltaTime ) OVERRIDE;
	virtual void  OnInsertion() OVERRIDE;
	virtual void  OnRemoval() OVERRIDE;
	virtual void  OnActivate() OVERRIDE;

	//-----------------------------------------------------------------------------
	// ABYardMenuState
	//-----------------------------------------------------------------------------
	virtual void OnMenuItemActivated( AGUI2MenuItem* a_pMenuItem ) OVERRIDE;
	virtual void OnMenuItemFocused( AGUI2MenuItem& a_rOldFocus, AGUI2MenuItem& a_rNewFocus ) OVERRIDE;
	virtual void OnMenuClose() OVERRIDE;

private:
	struct Row
	{
		// NOTE: no choices until SetChoices has filled the row. SetChoices of the first row reads the rows after it
		// (GetValue of the display mode) before they're set up: left as they were allocated, that read crashed
		ROW                   eRow          = ROW_RESOLUTION;
		TINT                  iNumChoices   = 0;
		TINT                  aiValues[ MAX_CHOICES ];
		TWCHAR                awszTexts[ MAX_CHOICES ][ MAX_TEXT ];
		TINT                  iChoice       = 0;     // shown
		TINT                  iInitialValue = 0;     // when the page was opened
		TBOOL                 bEnabled      = TTRUE;
		AGUI2MenuGridTextItem oLabel;
		AToggleSlider         oValue;
	};

	// Fills the choices of a row and selects a_iValue (the nearest choice if it isn't one of them)
	void SetChoices( Row& a_rRow, TINT a_iValue );

	// The choices of the refresh rate and the anti-aliasing depend on the resolution and the display mode
	void UpdateDependentRows();

	// Changes the value of the focused row
	void ChangeValue( TINT a_iDirection );

	// The line that tells what waits for the next start
	void UpdateInfo();

	// Saves and applies the rows that changed
	void Apply();

	Row*  FindRow( ROW a_eRow );
	TINT  GetValue( ROW a_eRow, TINT a_iDefault );
	TBOOL IsChanged( const Row& a_rcRow ) const { return a_rcRow.aiValues[ a_rcRow.iChoice ] != a_rcRow.iInitialValue; }

	void UpdateLabelScale( AGUI2MenuGridTextItem& a_rLabel, TFLOAT a_fDeltaTime );

	void OnMenuGridItemFocused( AGUI2MenuGridItem& a_rOldFocus, AGUI2MenuGridItem& a_rNewFocus );

private:
	PAGE          m_ePage;
	AGUI2MenuGrid m_oGridMenu;
	Row           m_aRows[ MAX_ROWS ];
	TINT          m_iNumRows;
	TINT          m_iFocusedRow; // -1 once cancelled
	AGUI2TextBox  m_oInfo;
	TWCHAR        m_wszInfo[ 160 ];

	// The sizes of ROW_RESOLUTION, its values are indices into it
	TINT m_aiSizes[ MAX_CHOICES ][ 2 ];
	TINT m_iNumSizes;
};

#endif // BARNYARD_COMMUNITY_PATCH
