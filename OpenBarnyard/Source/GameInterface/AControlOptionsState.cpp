#include "pch.h"
#include "AControlOptionsState.h"
#include "AOptionsState.h"
#include "AOptions.h"
#include "Sound/ASoundManager.h"
#include "SoundBank/ui.h"
#include "GUI/T2GUIFontManager.h"
#include "GUI/T2GUITextureSectionManager.h"
#include "Locale/ALocaleManager.h"
#include "Locale/AModStrings.h"
#include "GUI/AGUI2.h"
#include "HUD/AHudManager.h"
#include "HUD/AMenuNavHudElement.h"
#include "Input/AInputHandler.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS( AControlOptionsState );

#ifdef BARNYARD_COMMUNITY_PATCH
static const TWCHAR* PadIconsText( TINT a_iIcons )
{
	if ( a_iIcons == AOptions::PADICONS_XBOX ) return L"Xbox  \x0F21";
	if ( a_iIcons == AOptions::PADICONS_PLAYSTATION ) return L"PlayStation  \x0F0E";
	return AModStrings::Get( MODSTRING_CONTROLS_PADICONS_AUTO );
}
#endif

// $Barnyard: FUNCTION 004447c0
AControlOptionsState::AControlOptionsState()
{
	m_iFocusedItemId = 0;

	// The settings are only applied once the menu is closed
	m_bInvertMouse = AOptions::GetSingleton()->IsMouseInverted();
	m_fSensitivity = AOptions::GetSingleton()->GetSensitivity();
#ifdef BARNYARD_COMMUNITY_PATCH
	m_fControllerSensitivity = AOptions::GetSingleton()->GetControllerSensitivity();
	m_bRawMouseInput = AOptions::GetSingleton()->IsRawMouseInput();
	m_iPadIcons = AOptions::GetPadIcons();
#endif
}

// $Barnyard: FUNCTION 00443f00
// $Barnyard: FUNCTION 004443e0
AControlOptionsState::~AControlOptionsState()
{
}

// $Barnyard: FUNCTION 004446b0
TBOOL AControlOptionsState::ProcessInput( const Toshi::TInputInterface::InputEvent* a_pInputEvent )
{
	if ( m_eMenuState == MENUSTATE_MENU_VISIBLE )
	{
		if ( m_oGridMenu.ProcessInputEvent( a_pInputEvent ) )
			return TTRUE;

		for ( TINT i = 0; i < NUM_TOGGLES; i++ )
		{
			if ( m_aToggleSliders[ i ].ProcessInputEvent( a_pInputEvent ) )
				return TTRUE;
		}

		if ( m_oSensitivitySlider.GetSlider().ProcessInputEvent( a_pInputEvent ) )
			return TTRUE;
#ifdef BARNYARD_COMMUNITY_PATCH
		if ( m_oControllerSensitivitySlider.GetSlider().ProcessInputEvent( a_pInputEvent ) )
			return TTRUE;
#endif
	}

	return BaseClass::ProcessInput( a_pInputEvent );
}

// $Barnyard: FUNCTION 004444e0
TBOOL AControlOptionsState::ProcessCommand( AInputCommand a_eInputCommand, const Toshi::TInputInterface::InputEvent* a_pInputEvent, TBOOL& a_rStopEvents )
{
	if ( m_eMenuState != MENUSTATE_MENU_VISIBLE )
		return BaseClass::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );

	if ( a_eInputCommand == AInputCommand_Cancel )
	{
		// Cancelled, the changes are discarded
		ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUBACK );
		m_eMenuState     = MENUSTATE_MENU_DISAPPEAR;
		m_iFocusedItemId = -1;
		return TTRUE;
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	if ( ( m_iFocusedItemId == CONTROLOPTIONS_SENSITIVITY || m_iFocusedItemId == CONTROLOPTIONS_CONTROLLER_SENSITIVITY ) &&
	     ( a_eInputCommand == AInputCommand_Left || a_eInputCommand == AInputCommand_Right ) )
	{
		const TBOOL bMouse = m_iFocusedItemId == CONTROLOPTIONS_SENSITIVITY;
		TFLOAT& rValue = bMouse ? m_fSensitivity : m_fControllerSensitivity;
		// Integer steps avoid float accumulation crossing the 0.1 / 1.0 bounds.
		TINT iStep = TINT( rValue * 10.0f + 0.5f );
		iStep += a_eInputCommand == AInputCommand_Left ? -1 : 1;
		TMath::Clip( iStep, 1, 10 );
		const TFLOAT fValue = iStep / 10.0f;
		if ( fValue != rValue )
		{
			rValue = fValue;
			( bMouse ? m_oSensitivitySlider : m_oControllerSensitivitySlider ).GetSlider().SetValue( fValue );
			ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUCLICK );
		}
		a_pInputEvent->StartRepeat( 0.5f, 0.1f );
		return TTRUE;
	}
	else if ( m_iFocusedItemId == CONTROLOPTIONS_RAWMOUSE && ( a_eInputCommand == AInputCommand_Left || a_eInputCommand == AInputCommand_Right ) )
	{
		m_bRawMouseInput = !m_bRawMouseInput;
		m_aToggleSliders[ 1 ].SetText( g_pLocaleManager->GetString( m_bRawMouseInput ? 1097 : 1096 ) );
		ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUCLICK );
		a_pInputEvent->StartRepeat( 0.5f, 0.1f );
		return TTRUE;
	}
	else if ( m_iFocusedItemId == CONTROLOPTIONS_PADICONS && ( a_eInputCommand == AInputCommand_Left || a_eInputCommand == AInputCommand_Right ) )
	{
		const TINT iStep = a_eInputCommand == AInputCommand_Left ? -1 : 1;
		m_iPadIcons = ( m_iPadIcons + iStep + AOptions::PADICONS_NUMOF ) % AOptions::PADICONS_NUMOF;
		m_aToggleSliders[ 2 ].SetText( PadIconsText( m_iPadIcons ) );
		ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUCLICK );
		return TTRUE;
	}
#else
	if ( m_iFocusedItemId == CONTROLOPTIONS_SENSITIVITY )
	{
		// NOTE: the sensitivity changes by 0.1f and the bounds are checked on the accumulated float. From the default 0.5
		// it goes 0.4, 0.3, 0.2, 0.10000002 and then to ~0.0 (mouse look stops working), and up to 1.0000001
		if ( a_eInputCommand == AInputCommand_Left )
		{
			if ( m_fSensitivity > 0.1f )
			{
				m_fSensitivity -= 0.1f;
				ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUCLICK );
				m_oSensitivitySlider.GetSlider().SetValue( m_fSensitivity );
				a_pInputEvent->StartRepeat( 0.5f, 0.1f );
			}

			return BaseClass::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );
		}
		else if ( a_eInputCommand == AInputCommand_Right )
		{
			if ( m_fSensitivity < 1.0f )
			{
				m_fSensitivity += 0.1f;
				ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUCLICK );
				m_oSensitivitySlider.GetSlider().SetValue( m_fSensitivity );
				a_pInputEvent->StartRepeat( 0.5f, 0.1f );
			}

			return BaseClass::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );
		}
	}
	else
#endif // BARNYARD_COMMUNITY_PATCH
	if ( m_iFocusedItemId == CONTROLOPTIONS_INVERTMOUSE && ( a_eInputCommand == AInputCommand_Left || a_eInputCommand == AInputCommand_Right ) )
	{
		ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUCLICK );

		// Switch between "On" and "Off"
		m_bInvertMouse = !m_bInvertMouse;
		m_aToggleSliders[ 0 ].SetText( g_pLocaleManager->GetString( m_bInvertMouse ? 1097 : 1096 ) );

		a_pInputEvent->StartRepeat( 0.5f, 0.1f );
		return TTRUE;
	}

	if ( a_eInputCommand != AInputCommand_Left && a_eInputCommand != AInputCommand_Right && m_oGridMenu.ProcessInputCommand( a_eInputCommand, a_pInputEvent ) )
		return TTRUE;

	return BaseClass::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );
}

void AControlOptionsState::UpdateLabelScale( AGUI2MenuGridTextItem& a_rLabel, TFLOAT a_fDeltaTime )
{
	// NOTE: inlined twice in AControlOptionsState::OnUpdate
	T2GUITransform& rTransform = a_rLabel.GetTransform();

	TFLOAT   fScaleX      = rTransform.GetScaleX();
	TFLOAT   fScaleY      = rTransform.GetScaleY();
	TVector2 vTranslation = rTransform.GetTranslation();
	rTransform.SetIdentity();

	// Grow the focused label and shrink the others back
	if ( &a_rLabel == m_oGridMenu.GetFocusedMenuItem() )
	{
		fScaleX = TMath::LERPClamped( fScaleX, ms_fFocusedLabelScale, a_fDeltaTime * 10.0f );
		fScaleY = TMath::LERPClamped( fScaleY, ms_fFocusedLabelScale, a_fDeltaTime * 10.0f );
	}
	else
	{
		fScaleX = TMath::LERPClamped( fScaleX, ms_fLabelScale, a_fDeltaTime * 20.0f );
		fScaleY = TMath::LERPClamped( fScaleY, ms_fLabelScale, a_fDeltaTime * 20.0f );
	}

	T2GUITransform oScale;
	oScale.Set( TVector2( fScaleX, 0.0f ), TVector2( 0.0f, fScaleY ), TVector2::VEC_ZERO );
	rTransform.PreMultiply( oScale );
	rTransform.SetTranslation( vTranslation );
}

// $Barnyard: FUNCTION 00443fc0
TBOOL AControlOptionsState::OnUpdate( TFLOAT a_fDeltaTime )
{
	UpdateLabelScale( m_aToggleTexts[ 0 ], a_fDeltaTime );
	UpdateLabelScale( m_oSensitivityText, a_fDeltaTime );
#ifdef BARNYARD_COMMUNITY_PATCH
	UpdateLabelScale( m_aToggleTexts[ 1 ], a_fDeltaTime );
	UpdateLabelScale( m_aToggleTexts[ 2 ], a_fDeltaTime );
	UpdateLabelScale( m_oControllerSensitivityText, a_fDeltaTime );
#endif

	m_oGridMenu.Update( a_fDeltaTime );
	return BaseClass::OnUpdate( a_fDeltaTime );
}

// $Barnyard: FUNCTION 004448f0
void AControlOptionsState::OnInsertion()
{
	T2GUIFont* pFont = T2GUIFontManager::FindFont( AGUI2STYLE_FONT_PRIMARY );

#ifdef BARNYARD_COMMUNITY_PATCH
	// CreateGrid's implementation takes rows first (the header names are reversed).
	m_oGridMenu.CreateGrid( CONTROLOPTIONS_NUMOF, 2, 40.0f, 20.0f );
#else
	m_oGridMenu.CreateGrid( 2, 2, 40.0f, 20.0f );
#endif
	m_oGridMenu.SetCallbackUserData( this );
	m_oGridMenu.SetFocusSound( soundbank::UI_MENUCLICK );
	m_oGridMenu.SetBackSound( soundbank::UI_MENUBACK );
	m_oGridMenu.SetSelectSound( soundbank::UI_MENUOK );
	m_oGridMenu.BlockActivation( TTRUE );

	// NOTE: same code as the AAudioOptionsState callback (004448c0)
	m_oGridMenu.SetItemActivationCallback( []( void* a_pUserData, AGUI2MenuGridItem* a_pItem ) {
		ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUOK );
		TSTATICCAST( AControlOptionsState, a_pUserData )->m_eMenuState = MENUSTATE_MENU_DISAPPEAR;
	} );

	// NOTE: same code as the AAudioOptionsState callback (00443070)
	m_oGridMenu.SetItemFocusCallback( []( void* a_pUserData, AGUI2MenuGridItem* a_pOldFocus, AGUI2MenuGridItem* a_pNewFocus ) {
		TSTATICCAST( AControlOptionsState, a_pUserData )->OnMenuGridItemFocused( *a_pOldFocus, *a_pNewFocus );
	} );

	m_aToggleSliders[ 0 ].Create( "left_arrow", "left_arrow2", g_pLocaleManager->GetString( m_bInvertMouse ? 1097 : 1096 ) );

	// Shrink the labels if they are too wide
	TFLOAT fMaxTextWidth = 0.0f;
	fMaxTextWidth        = TMath::Max( fMaxTextWidth, pFont->GetTextWidth( g_pLocaleManager->GetString( 305 ), 1.0f ) );
	fMaxTextWidth        = TMath::Max( fMaxTextWidth, pFont->GetTextWidth( g_pLocaleManager->GetString( 286 ), 1.0f ) );

#ifdef BARNYARD_COMMUNITY_PATCH
	const TWCHAR* wszRawMouse = AModStrings::Get( MODSTRING_CONTROLS_RAWMOUSE );
	const TWCHAR* wszControllerSensitivity = AModStrings::Get( MODSTRING_CONTROLS_PADSENSITIVITY );
	const TWCHAR* wszPadIcons = AModStrings::Get( MODSTRING_CONTROLS_PADICONS );
	fMaxTextWidth = TMath::Max( fMaxTextWidth, pFont->GetTextWidth( wszRawMouse, 1.0f ) );
	fMaxTextWidth = TMath::Max( fMaxTextWidth, pFont->GetTextWidth( wszControllerSensitivity, 1.0f ) );
	fMaxTextWidth = TMath::Max( fMaxTextWidth, pFont->GetTextWidth( wszPadIcons, 1.0f ) );
	ms_fMaxLabelWidth = 210.0f;
#endif
	ms_fLabelScale        = ( fMaxTextWidth >= ms_fMaxLabelWidth ) ? ms_fMaxLabelWidth / fMaxTextWidth : 1.0f;
	ms_fFocusedLabelScale = ms_fLabelScale * 1.1f;

	// $Barnyard: FUNCTION 00444720
	auto fnArrowFocused = []( void* a_pUserData, TINT a_iSliderID ) {
		AControlOptionsState* pState = TSTATICCAST( AControlOptionsState, a_pUserData );

#ifdef BARNYARD_COMMUNITY_PATCH
		if ( a_iSliderID == CONTROLOPTIONS_CONTROLLER_SENSITIVITY )
			pState->m_oGridMenu.SetFocusAt( &pState->m_oControllerSensitivityText );
		else if ( a_iSliderID == CONTROLOPTIONS_RAWMOUSE )
			pState->m_oGridMenu.SetFocusAt( &pState->m_aToggleTexts[ 1 ] );
		else if ( a_iSliderID == CONTROLOPTIONS_PADICONS )
			pState->m_oGridMenu.SetFocusAt( &pState->m_aToggleTexts[ 2 ] );
		else if ( a_iSliderID == CONTROLOPTIONS_SENSITIVITY )
#else
		if ( a_iSliderID >= NUM_TOGGLES )
#endif
			pState->m_oGridMenu.SetFocusAt( &pState->m_oSensitivityText );
		else
			pState->m_oGridMenu.SetFocusAt( &pState->m_aToggleTexts[ a_iSliderID ] );

		pState->m_iFocusedItemId = a_iSliderID;
	};

	auto fnCreateLabel = [ pFont ]( AGUI2MenuGridTextItem& a_rLabel, const TWCHAR* a_wszText, TINT a_iID ) {
		// NOTE: inlined twice in AControlOptionsState::OnInsertion
		a_rLabel.GetTextBox().SetScale( ms_fLabelScale );
		a_rLabel.Create( pFont, pFont->GetTextWidth( a_wszText, ms_fLabelScale ), a_wszText, 10.0f );
		a_rLabel.SetFocusedColour( AGUI2STYLE_COLOR_FOCUSED );
		a_rLabel.SetEnabledColour( AGUI2STYLE_COLOR_SECONDARY );
		a_rLabel.SetDisabledColour( AGUI2STYLE_COLOR_DISABLED );
		a_rLabel.SetFocusedScale( 1.0f );
		a_rLabel.OnFocusLost();
		a_rLabel.SetId( a_iID );
		a_rLabel.SetTextShadow( TTRUE, 0xa0000000, 0.6f );
		a_rLabel.SetTextShadowOffset( 5.0f );
	};

	// Invert mouse
	fnCreateLabel( m_aToggleTexts[ 0 ], g_pLocaleManager->GetString( 305 ), CONTROLOPTIONS_INVERTMOUSE );
	m_oGridMenu.AddMenuGridItem( m_aToggleTexts[ 0 ] );

	m_aToggleSliders[ 0 ].SetEnabled( TFALSE );
	m_aToggleSliders[ 0 ].SetId( CONTROLOPTIONS_INVERTMOUSE );
	m_aToggleSliders[ 0 ].SetCallbackUserData( this );
	m_aToggleSliders[ 0 ].SetArrowFocusedCallback( fnArrowFocused );
	m_oGridMenu.AddMenuGridItem( m_aToggleSliders[ 0 ] );

	// Mouse sensitivity
	fnCreateLabel( m_oSensitivityText, g_pLocaleManager->GetString( 286 ), CONTROLOPTIONS_SENSITIVITY );
	m_oGridMenu.AddMenuGridItem( m_oSensitivityText );

	AGUI2Slider& rSlider = m_oSensitivitySlider.GetSlider();
	rSlider.Create( "left_arrow", "left_arrow2", "Statue_GoldStar", 5 );
	m_oSensitivitySlider.SetDimensions( rSlider.GetWidth(), rSlider.GetHeight() );
	m_oSensitivitySlider.AddChildTail( rSlider );
	rSlider.SetValue( m_fSensitivity );

	m_oSensitivitySlider.SetEnabled( TFALSE );
	rSlider.SetId( CONTROLOPTIONS_SENSITIVITY );
	rSlider.SetCallbackUserData( this );
	rSlider.SetArrowFocusedCallback( fnArrowFocused );
	m_oGridMenu.AddMenuGridItem( m_oSensitivitySlider );

#ifdef BARNYARD_COMMUNITY_PATCH
	// Raw input and pad sensitivity use the same arrow/slider widgets as the existing options.
	fnCreateLabel( m_aToggleTexts[ 1 ], wszRawMouse, CONTROLOPTIONS_RAWMOUSE );
	m_oGridMenu.AddMenuGridItem( m_aToggleTexts[ 1 ] );
	m_aToggleSliders[ 1 ].Create( "left_arrow", "left_arrow2", g_pLocaleManager->GetString( m_bRawMouseInput ? 1097 : 1096 ) );
	m_aToggleSliders[ 1 ].SetEnabled( TFALSE );
	m_aToggleSliders[ 1 ].SetId( CONTROLOPTIONS_RAWMOUSE );
	m_aToggleSliders[ 1 ].SetCallbackUserData( this );
	m_aToggleSliders[ 1 ].SetArrowFocusedCallback( fnArrowFocused );
	m_oGridMenu.AddMenuGridItem( m_aToggleSliders[ 1 ] );

	fnCreateLabel( m_oControllerSensitivityText, wszControllerSensitivity, CONTROLOPTIONS_CONTROLLER_SENSITIVITY );
	m_oGridMenu.AddMenuGridItem( m_oControllerSensitivityText );
	AGUI2Slider& rControllerSlider = m_oControllerSensitivitySlider.GetSlider();
	rControllerSlider.Create( "left_arrow", "left_arrow2", "Statue_GoldStar", 5 );
	m_oControllerSensitivitySlider.SetDimensions( rControllerSlider.GetWidth(), rControllerSlider.GetHeight() );
	m_oControllerSensitivitySlider.AddChildTail( rControllerSlider );
	rControllerSlider.SetValue( m_fControllerSensitivity );
	m_oControllerSensitivitySlider.SetEnabled( TFALSE );
	rControllerSlider.SetId( CONTROLOPTIONS_CONTROLLER_SENSITIVITY );
	rControllerSlider.SetCallbackUserData( this );
	rControllerSlider.SetArrowFocusedCallback( fnArrowFocused );
	m_oGridMenu.AddMenuGridItem( m_oControllerSensitivitySlider );

	fnCreateLabel( m_aToggleTexts[ 2 ], wszPadIcons, CONTROLOPTIONS_PADICONS );
	m_oGridMenu.AddMenuGridItem( m_aToggleTexts[ 2 ] );
	// The widest value supplies layout space even if the saved value is Xbox.
	const TWCHAR* wszWidest = PadIconsText( AOptions::PADICONS_AUTO );
	for ( TINT i = AOptions::PADICONS_XBOX; i < AOptions::PADICONS_NUMOF; ++i )
	{
		const TWCHAR* wszValue = PadIconsText( i );
		if ( pFont->GetTextWidth( wszValue ) > pFont->GetTextWidth( wszWidest ) ) wszWidest = wszValue;
	}
	m_aToggleSliders[ 2 ].Create( "left_arrow", "left_arrow2", wszWidest, 0.55f );
	m_aToggleSliders[ 2 ].SetText( PadIconsText( m_iPadIcons ) );
	m_aToggleSliders[ 2 ].SetEnabled( TFALSE );
	m_aToggleSliders[ 2 ].SetId( CONTROLOPTIONS_PADICONS );
	m_aToggleSliders[ 2 ].SetCallbackUserData( this );
	m_aToggleSliders[ 2 ].SetArrowFocusedCallback( fnArrowFocused );
	m_oGridMenu.AddMenuGridItem( m_aToggleSliders[ 2 ] );

	m_aToggleTexts[ 0 ].SetSideElements( TNULL, TNULL, &m_aToggleTexts[ 2 ], &m_oSensitivityText );
	m_oSensitivityText.SetSideElements( TNULL, TNULL, &m_aToggleTexts[ 0 ], &m_aToggleTexts[ 1 ] );
	m_aToggleTexts[ 1 ].SetSideElements( TNULL, TNULL, &m_oSensitivityText, &m_oControllerSensitivityText );
	m_oControllerSensitivityText.SetSideElements( TNULL, TNULL, &m_aToggleTexts[ 1 ], &m_aToggleTexts[ 2 ] );
	m_aToggleTexts[ 2 ].SetSideElements( TNULL, TNULL, &m_oControllerSensitivityText, &m_aToggleTexts[ 0 ] );
#else
	m_aToggleTexts[ 0 ].SetSideElements( TNULL, TNULL, &m_oSensitivityText, &m_oSensitivityText );
	m_oSensitivityText.SetSideElements( TNULL, TNULL, &m_aToggleTexts[ 0 ], &m_aToggleTexts[ 0 ] );
#endif
	m_oGridMenu.SetFocusAt( &m_aToggleTexts[ 0 ] );

	// The second column is sized relatively to the first one
	TFLOAT fMaxLabelWidth  = 0.0f;
	TFLOAT fMaxWidgetWidth = 0.0f;
	fMaxLabelWidth         = TMath::Max( fMaxLabelWidth, m_aToggleTexts[ 0 ].GetWidth() );
	fMaxWidgetWidth        = TMath::Max( fMaxWidgetWidth, m_aToggleSliders[ 0 ].GetWidth() );
	fMaxLabelWidth         = TMath::Max( fMaxLabelWidth, m_oSensitivityText.GetWidth() );
	fMaxWidgetWidth        = TMath::Max( fMaxWidgetWidth, m_oSensitivitySlider.GetWidth() );
#ifdef BARNYARD_COMMUNITY_PATCH
	fMaxLabelWidth = TMath::Max( fMaxLabelWidth, m_aToggleTexts[ 1 ].GetWidth() );
	fMaxLabelWidth = TMath::Max( fMaxLabelWidth, m_aToggleTexts[ 2 ].GetWidth() );
	fMaxLabelWidth = TMath::Max( fMaxLabelWidth, m_oControllerSensitivityText.GetWidth() );
	fMaxWidgetWidth = TMath::Max( fMaxWidgetWidth, m_aToggleSliders[ 1 ].GetWidth() );
	fMaxWidgetWidth = TMath::Max( fMaxWidgetWidth, m_aToggleSliders[ 2 ].GetWidth() );
	fMaxWidgetWidth = TMath::Max( fMaxWidgetWidth, m_oControllerSensitivitySlider.GetWidth() );
#endif

	if ( fMaxLabelWidth <= fMaxWidgetWidth )
		m_oGridMenu.SetColumnWidth( 1, fMaxLabelWidth / fMaxWidgetWidth );
	else
		m_oGridMenu.SetColumnWidth( 1, fMaxWidgetWidth / fMaxLabelWidth );

	m_oGridMenu.ReflowChildren();

	// Center the labels in the first column
	m_aToggleTexts[ 0 ].SetTransform(
	    fMaxLabelWidth * 0.5f + m_aToggleTexts[ 0 ].GetTransform().GetTranslation().x,
	    m_aToggleTexts[ 0 ].GetTransform().GetTranslation().y
	);
	m_aToggleTexts[ 0 ].SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPCENTER );

	m_oSensitivityText.SetTransform(
	    fMaxLabelWidth * 0.5f + m_oSensitivityText.GetTransform().GetTranslation().x,
	    m_oSensitivityText.GetTransform().GetTranslation().y
	);
	m_oSensitivityText.SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPCENTER );
#ifdef BARNYARD_COMMUNITY_PATCH
	m_aToggleTexts[ 1 ].SetTransform(
	    fMaxLabelWidth * 0.5f + m_aToggleTexts[ 1 ].GetTransform().GetTranslation().x,
	    m_aToggleTexts[ 1 ].GetTransform().GetTranslation().y );
	m_aToggleTexts[ 1 ].SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPCENTER );
	m_aToggleTexts[ 2 ].SetTransform(
	    fMaxLabelWidth * 0.5f + m_aToggleTexts[ 2 ].GetTransform().GetTranslation().x,
	    m_aToggleTexts[ 2 ].GetTransform().GetTranslation().y );
	m_aToggleTexts[ 2 ].SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPCENTER );
	m_oControllerSensitivityText.SetTransform(
	    fMaxLabelWidth * 0.5f + m_oControllerSensitivityText.GetTransform().GetTranslation().x,
	    m_oControllerSensitivityText.GetTransform().GetTranslation().y );
	m_oControllerSensitivityText.SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPCENTER );
#endif

	m_oDialog.AddChildTail( m_oGridMenu );

	m_oDialogTitle.SetText( g_pLocaleManager->GetString( 285 ) );
	m_bFocusedElementBloated = TFALSE;

	AHudManager::GetSingleton()->FindElement<AMenuNavHudElement>()->SetTexts( 1050, 1051, -1 );

	BaseClass::OnInsertion();

	m_oDialog.SetDimensions( m_oGridMenu.GetWidth() + 128.0f, m_oGridMenu.GetHeight() + 150.0f );
}

// $Barnyard: FUNCTION 00444400
void AControlOptionsState::OnRemoval()
{
	for ( TINT i = 0; i < NUM_TOGGLES; i++ )
		m_aToggleSliders[ i ].RemoveElements();

	for ( TINT i = 0; i < NUM_TOGGLES; i++ )
		m_aToggleTexts[ i ].RemoveSelf();

	m_oGridMenu.RemoveSelf();
	m_oSensitivityText.RemoveSelf();
	m_oSensitivitySlider.GetSlider().RemoveElements();
	m_oSensitivitySlider.GetSlider().RemoveSelf();
	m_oSensitivitySlider.RemoveSelf();
#ifdef BARNYARD_COMMUNITY_PATCH
	m_oControllerSensitivityText.RemoveSelf();
	m_oControllerSensitivitySlider.GetSlider().RemoveElements();
	m_oControllerSensitivitySlider.GetSlider().RemoveSelf();
	m_oControllerSensitivitySlider.RemoveSelf();
#endif

	AMenuNavHudElement::UpdateText();
	BaseClass::OnRemoval();
}

// $Barnyard: FUNCTION 00443fb0
void AControlOptionsState::OnActivate()
{
	m_iFocusedItemId = 0;
	BaseClass::OnActivate();
}

// NOTE: folded with AAudioOptionsState::OnMenuItemActivated (00443630)
void AControlOptionsState::OnMenuItemActivated( AGUI2MenuItem* a_pMenuItem )
{
	ASoundManager::GetSingleton()->PlayCue( soundbank::UI_MENUOK );
	ABYardMenuState::OnButtonActivated( TSTATICCAST( AGUI2Button, a_pMenuItem ) );
}

// $Barnyard: FUNCTION 00444210
void AControlOptionsState::OnMenuItemFocused( AGUI2MenuItem& a_rOldFocus, AGUI2MenuItem& a_rNewFocus )
{
	m_iFocusedItemId = TSTATICCAST( AGUI2Button, &a_rNewFocus )->GetId();
}

// $Barnyard: FUNCTION 00444760
void AControlOptionsState::OnMenuClose()
{
	if ( m_iFocusedItemId != -1 )
	{
		// Closed with OK, apply the new settings
		AOptions* pOptions = AOptions::GetSingleton();
		pOptions->SetMouseInverted( m_bInvertMouse );
		pOptions->SetSensitivity( m_fSensitivity );
#ifdef BARNYARD_COMMUNITY_PATCH
		pOptions->SetRawMouseInput( m_bRawMouseInput );
		pOptions->SetControllerSensitivity( m_fControllerSensitivity );
		pOptions->SetPadIcons( m_iPadIcons );
		if ( !pOptions->SaveInputSettings() )
			TERROR( "[Input] Could not save Controls settings to the registry\n" );
#endif
		pOptions->ApplySettings();

		AOptionsState::ms_bSettingsChanged = TTRUE;
	}

	BaseClass::Remove();
}

// $Barnyard: FUNCTION 00444220
void AControlOptionsState::OnMenuGridItemFocused( AGUI2MenuGridItem& a_rOldFocus, AGUI2MenuGridItem& a_rNewFocus )
{
	// NOTE: only the labels can be focused (the sliders are disabled), all of them keep their id at 0x70
	m_iFocusedItemId = TSTATICCAST( AGUI2MenuGridTextItem, &a_rNewFocus )->GetId();
}

// $Barnyard: FUNCTION 00443bf0
AToggleSlider::AToggleSlider()
{
	m_bMouseDirty            = TFALSE;
	m_iLastPressedArrowIndex = -1;
}

// $Barnyard: FUNCTION 00443c70
// $Barnyard: FUNCTION 00444230
AToggleSlider::~AToggleSlider()
{
}

// $Barnyard: FUNCTION 00443ce0
TBOOL AToggleSlider::Create( const TCHAR* a_szLeftArrow, const TCHAR* a_szRightArrow, const TWCHAR* a_wszText, TFLOAT a_fTextScale )
{
	TFLOAT fHeight = 0.0f;

	// Setup left arrow (the sections are square)
	T2GUITextureSection* pLeftArrow = T2GUITextureSectionManager::GetTextureSection( a_szLeftArrow );
	TFLOAT               fLeftSize  = pLeftArrow->GetWidth();

	m_aArrows[ 0 ].SetDimensions( fLeftSize, fLeftSize );
	m_aArrows[ 0 ].SetTextureSection( pLeftArrow );
	m_aArrows[ 0 ].SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPLEFT );
	AddChildTail( m_aArrows[ 0 ] );

	if ( fLeftSize > 0.0f )
		fHeight = fLeftSize;

	// Setup the text, it's a bit wider than the text itself
	m_oTextBox.Create( T2GUIFontManager::FindFont( AGUI2STYLE_FONT_PRIMARY ), 0.0f );
	if ( a_fTextScale < 1.0f )
	{
		m_oTextBox.SetScale( a_fTextScale );
		m_oTextBox.SetTransform( 0.0f, 0.25f * fLeftSize );
	}
	m_oTextBox.SetTextAndFitWidth( a_wszText );
	m_oTextBox.SetWidth( m_oTextBox.GetWidth() * 1.35f );
	m_oTextBox.SetAttachment( T2GUIATTACHMENT_TOPCENTER, T2GUIATTACHMENT_TOPCENTER );
	m_oTextBox.SetColour( AGUI2STYLE_COLOR_PRIMARY );
	AddChildTail( m_oTextBox );

	TFLOAT fWidth = m_oTextBox.GetWidth() + fLeftSize;

	// Setup right arrow
	T2GUITextureSection* pRightArrow = T2GUITextureSectionManager::GetTextureSection( a_szRightArrow );
	TFLOAT               fRightSize  = pRightArrow->GetWidth();

	m_aArrows[ 1 ].SetDimensions( fRightSize, fRightSize );
	m_aArrows[ 1 ].SetTextureSection( pRightArrow );
	m_aArrows[ 1 ].SetAttachment( T2GUIATTACHMENT_TOPRIGHT, T2GUIATTACHMENT_TOPRIGHT );
	AddChildTail( m_aArrows[ 1 ] );

	fWidth += fRightSize;

	if ( fRightSize > fHeight )
		fHeight = fRightSize;

	SetDimensions( fWidth, fHeight );
	return TTRUE;
}

// $Barnyard: FUNCTION 00444310
TBOOL AToggleSlider::ProcessInputEvent( const Toshi::TInputInterface::InputEvent* a_pInputEvent )
{
	if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_MOVED )
	{
		// Arrows are hit-tested in OnUpdate
		m_bMouseDirty = TTRUE;
		return TFALSE;
	}

	if ( a_pInputEvent->GetEventType() != TInputInterface::EVENT_TYPE_GONE_DOWN ||
	     a_pInputEvent->GetDoodad() != TInputDeviceMouse::BUTTON_1 ||
	     m_iLastPressedArrowIndex == -1 )
		return TFALSE;

	// Clicking an arrow emulates a press of the Left/Right key
	TInputDeviceKeyboard* pKeyboard = AInputHandler::GetSingleton()->GetKeyboardDevice();
	TINT                  iKey      = ( m_iLastPressedArrowIndex != 0 ) ? TInputDeviceKeyboard::KEY_RIGHT : TInputDeviceKeyboard::KEY_LEFT;

	TInputInterface::GetSingleton()->SendEvent( pKeyboard, iKey, TInputInterface::EVENT_TYPE_GONE_DOWN );
	TInputInterface::GetSingleton()->SendEvent( pKeyboard, iKey, TInputInterface::EVENT_TYPE_GONE_UP );
	return TTRUE;
}

// $Barnyard: FUNCTION 00444250
void AToggleSlider::OnUpdate( TFLOAT a_fDeltaTime )
{
	if ( m_bMouseDirty == TTRUE )
	{
		// Check if any of the arrow rectangles is hovered
		m_iLastPressedArrowIndex = -1;
		T2GUITransform oElementInvTransform;

		TVector2 vecCursorPos = AGUI2::GetMouseCursor().GetCursorPos();

		for ( TINT i = 0; i < TARRAYSIZE( m_aArrows ); i++ )
		{
			// Get cursor position that is relative to the arrow
			m_aArrows[ i ].GetInvScreenTransform( oElementInvTransform );

			TVector2 vecElementRelCursorPos;
			oElementInvTransform.Transform( vecElementRelCursorPos, vecCursorPos );

			if ( m_aArrows[ i ].IsPointInside( vecElementRelCursorPos.x, vecElementRelCursorPos.y ) )
			{
				m_fnArrowFocusCallback( m_pCallbackUserData, m_iID );
				m_iLastPressedArrowIndex = i;
			}
		}

		m_bMouseDirty = TFALSE;
	}

	AGUI2MenuGridItem::OnUpdate( a_fDeltaTime );
}

// NOTE: folded with AGUI2MenuItem::SetAlpha (0063cb00)
void AToggleSlider::SetAlpha( TFLOAT a_fAlpha )
{
	for ( T2GUIElement* it = ChildRBegin(); it != TNULL; it = GetPrevChild( it ) )
		it->SetAlpha( a_fAlpha );

	T2GUIElement::SetAlpha( a_fAlpha );
}

// $Barnyard: FUNCTION 00443e80
// $Barnyard: FUNCTION 004443c0
ACameraSlider::~ACameraSlider()
{
}

// $Barnyard: FUNCTION 00443ee0
void ACameraSlider::SetAlpha( TFLOAT a_fAlpha )
{
	m_oSlider.SetAlpha( a_fAlpha );
	T2GUIElement::SetAlpha( a_fAlpha );
}
