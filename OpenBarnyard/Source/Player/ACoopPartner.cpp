#include "pch.h"
#include "ACoopPartner.h"
#include "ACoopNet.h"
#include "ACoopAnim.h"
#include "APlayerProfileManager.h"
#include "ALoadScreen.h"
#include "Helpers/AGameLoader.h"
#include "GameInterface/AGameStateController.h"
#include "GameInterface/AInventoryState2.h"
#include "GameInterface/ASSeqState.h"
#include "HUD/AHudManager.h"
#include "HUD/ASimMiniMap.h"
#include "GUI/AGUI2.h"
#include "GUI/AGUI2TextBox.h"
#include "GUI/AGUI2Style.h"
#include "Locale/ALocaleManager.h"
#include "Locale/AModStrings.h"
#include "MiniGame/AMiniGameManager.h"
#include "Cameras/ACameraManager.h"
#include "Render/ARenderer.h"
#include "Tasks/ARootTask.h"
#include "Units/AUnit.h"
#include "Units/AAnimatedUnit.h"
#include "Animation/AModel.h"
#include "Units/AUnitPlayer.h"
#include "Units/APlayerControlComponent.h"
#include "Units/Projectiles/AMilkSquirtProjectileLauncher.h"
#include "Units/Projectiles/ALaunchAmmo.h"
#include "UnitMovement/Steering/APlayerSteer.h"
#include "UnitMovement/Steering/ASteerKinematics.h"
#include "UnitMovement/PathfindingNetworks/AInteriorGoal.h"
#include "Quests/AQuestManager.h"
#include "Sound/ASoundManager.h"
#include "SoundBank/am_m.h"
#include "SoundBank/am_f.h"
#include "SoundBank/barnyard.h"
#include "FX/AParticleEffectManager.h"
#include "FX/ASplashManager.h"
#include "GUI/AFadeManager.h"
#include "Cameras/AGTAStyleCameraHelper.h"
#include "Input/AInputHandler.h"
#include "UnitMovement/Steering/ABikeSteer.h"
#include "Animation/AAnimatableObjectManager.h"
#include "Helpers/ABikeManager.h"
#include "Units/AInventoryGroupComponent.h"
#include "ACoopProgress.h"
#include "Helpers/AGameTimeManager.h"
#include "MiniGame/ABarnExploreMinigameState.h"
#include "MiniGame/ACoinPickupMicroGame.h"
#include "Units/ABarnOpenRequirement.h"

#include <GUI/T2GUIFontManager.h>

#include <cmath>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TPSTRING8_DECLARE( AAnimalConversationState );
TPSTRING8_DECLARE( AGopherShopGame );
TPSTRING8_DECLARE( ACookingMiniGame );
TPSTRING8_DECLARE( ASleepCycleState );
TPSTRING8_DECLARE( ABarnExploreMinigameState );
TPSTRING8_DECLARE( c_head );
TPSTRING8_DECLARE( idle_lp );

static TBOOL IsLogEnabled()
{
	static TINT s_iLog = -1;

	if ( s_iLog < 0 )
	{
		const TCHAR* szCommandLine = GetCommandLineA();
		s_iLog                     = ( szCommandLine && TStringManager::String8FindString( szCommandLine, "-netlog" ) ) ? 1 : 0;
	}

	return s_iLog == 1;
}

//-----------------------------------------------------------------------------
// Who the partner is and what they're doing (CHANNEL_PARTNER)
//-----------------------------------------------------------------------------

static constexpr TUINT32 INFO_MAGIC = 0x31495942; // "BYI1"

#pragma pack( push, 1 )
struct InfoMessage
{
	TUINT32 uiMagic;
	TUINT32 uiNonce;    // of the sender's run
	TUINT16 uiSequence; // one more for every message
	TUINT8  uiArea;     // ACoopNet::AREA of the sender
	TUINT8  uiActivity; // ACoopPartner::ACTIVITY
	TINT8   iMiniGame;  // with ACTIVITY_MINIGAME: AGameLoader::GetCurrMiniGame() of the sender, else -1
	TUINT8  uiVariant;  // with ACTIVITY_MINIGAME: AMiniGameManager::GetMiniGameVariant() of the sender
	TUINT8  auiReserved[ 2 ];
	TWCHAR  wszName[ ACoopPartner::MAX_NAME ]; // the name of the sender's cow (the profile's)
};
#pragma pack( pop )

TSTATICASSERT( sizeof( InfoMessage ) == 16 + 2 * ACoopPartner::MAX_NAME );

static TUINT32     s_uiInfoNonce    = 0;
static TUINT16     s_uiInfoSequence = 0;
static InfoMessage s_oLastSentInfo  = {};
static TFLOAT      s_fInfoWait      = 0.0f;

struct PartnerState
{
	TBOOL       bKnown;
	InfoMessage oInfo;
	DWORD       uiTime; // GetTickCount when it came
};

static PartnerState s_oPartner = {};

static ACoopPartner::ACTIVITY s_eLocalActivity = ACoopPartner::ACTIVITY_UNKNOWN;

static ACoopPartner::ACTIVITY FindLocalActivity()
{
	if ( !AGameStateController::IsSingletonCreated() || !ARootTask::IsSingletonCreated() )
		return ACoopPartner::ACTIVITY_FRONTEND;

	AGameStateController* pController = AGameStateController::GetSingleton();

	if ( ARootTask::GetSingleton()->IsTransitioning() || AGameLoader::IsChangingTerrain() || AGameLoader::IsReturningToFrontEnd() || pController->GetNumStates() <= 0 )
		return ACoopPartner::ACTIVITY_LOADING;

	if ( pController->FindGameStateOnStack( TPS8( AGopherShopGame ) ) )
		return ACoopPartner::ACTIVITY_SHOP;

	if ( pController->FindGameStateOnStack( TPS8( ACookingMiniGame ) ) )
		return ACoopPartner::ACTIVITY_COOKING;

	if ( AGameLoader::IsInMiniGame() )
		return ACoopPartner::ACTIVITY_MINIGAME;

	if ( AGameLoader::GetScriptedSequence() != -1 || ASSeqState::IsPlaying() )
		return ACoopPartner::ACTIVITY_SCENE;

	if ( pController->FindGameStateOnStack( TPS8( ASleepCycleState ) ) || pController->FindGameStateOnStack( TPS8D( "ASleepCycleChooseState" ) ) )
		return ACoopPartner::ACTIVITY_SLEEPING;

	if ( ACoopNet::GetLocalArea() == ACoopNet::AREA_NONE )
	{
		if ( AGameLoader::IsMainGameLoaded() || AGameLoader::IsBarnExploreMiniGame() || AGameLoader::IsGameStateLibraryLoaded() )
			return ACoopPartner::ACTIVITY_LOADING;

		return ACoopPartner::ACTIVITY_FRONTEND;
	}

	if ( pController->FindGameStateOnStack( TPS8( AAnimalConversationState ) ) )
		return ACoopPartner::ACTIVITY_CONVERSATION;

	if ( ARootTask::GetSingleton()->IsPaused() )
		return ACoopPartner::ACTIVITY_MENU;

	AGameState* pCurrent = pController->GetCurrentState();

	// The micro games of the farm are pushed on top of it (the gold rush, the super squirt, the treasure hunt)...
	if ( pCurrent->GetClass() != ( AGameLoader::IsBarnExploreMiniGame() ? &TGetClass( ABarnExploreMinigameState ) : &TGetClass( ASimState ) ) )
		return ACoopPartner::ACTIVITY_MICROGAME;

	// ...or added to it (the milk can, the tree that's kicked)
	if ( pCurrent->GetNumChildStates() != 0 )
	{
		AInventoryState2* pInventory = TSTATICCAST( AInventoryState2, pCurrent->FindChildState( &TGetClass( AInventoryState2 ) ) );

		if ( !pInventory || pCurrent->GetNumChildStates() > 1 )
			return ACoopPartner::ACTIVITY_MICROGAME;

		if ( pInventory->IsOpen() )
			return ACoopPartner::ACTIVITY_MENU;
	}

	return ACoopPartner::ACTIVITY_FREE;
}

// A micro game that an emote of the player can't disturb: the gold rush (ACoinPickupMicroGame) only watches where
// the cow runs once its lever is pulled. The others have the cow's hands or its animations (the milk can, the tree,
// the super squirt, the detector of the treasure hunt). What the partner does to the cow (hits, a high five to
// answer) stays off in every micro game
static TBOOL s_bEmotesInMicroGame = TFALSE;

static TBOOL FindEmotesInMicroGame()
{
	if ( s_eLocalActivity != ACoopPartner::ACTIVITY_MICROGAME || !AGameStateController::IsSingletonCreated() || AGameStateController::GetSingleton()->GetNumStates() <= 0 )
		return TFALSE;

	AGameState* pCurrent = AGameStateController::GetSingleton()->GetCurrentState();
	return pCurrent->IsA( &TGetClass( ACoinPickupMicroGame ) ) && pCurrent->GetNumChildStates() == 0;
}

// The player's own emotes can be played now
static TBOOL AreEmotesAllowed()
{
	return s_eLocalActivity == ACoopPartner::ACTIVITY_FREE || s_bEmotesInMicroGame;
}

static void GetLocalName( TWCHAR* a_wszOut )
{
	a_wszOut[ 0 ] = L'\0';

	if ( !APlayerProfileManager::IsSingletonCreated() )
		return;

	const TString16& rcName = APlayerProfileManager::GetSingleton()->GetMainProfile()->GetName();
	const TWCHAR*    wszName = rcName.GetString();

	if ( !wszName )
		return;

	TINT i = 0;

	for ( ; wszName[ i ] != L'\0' && i < ACoopPartner::MAX_NAME - 1; i++ )
		a_wszOut[ i ] = wszName[ i ];

	a_wszOut[ i ] = L'\0';
}

static void SendInfo( TFLOAT a_fDeltaTime )
{
	if ( s_uiInfoNonce == 0 )
		s_uiInfoNonce = ( GetTickCount() * 2246822519u ) ^ GetCurrentProcessId() | 1;

	InfoMessage oInfo = {};
	oInfo.uiMagic     = INFO_MAGIC;
	oInfo.uiNonce     = s_uiInfoNonce;
	oInfo.uiArea      = ACoopNet::GetLocalArea();
	oInfo.uiActivity  = s_eLocalActivity;
	oInfo.iMiniGame   = -1;

	if ( s_eLocalActivity == ACoopPartner::ACTIVITY_MINIGAME && AMiniGameManager::IsSingletonCreated() )
	{
		const TINT iMiniGame = AGameLoader::GetCurrMiniGame();

		if ( iMiniGame >= 0 && iMiniGame < AMiniGameManager::GetSingleton()->GetTotalNumMiniGames() && iMiniGame < 128 )
		{
			oInfo.iMiniGame = TINT8( iMiniGame );
			oInfo.uiVariant = TUINT8( TMath::Max( AMiniGameManager::GetSingleton()->GetMiniGameVariant(), 0 ) );
		}
	}

	GetLocalName( oInfo.wszName );

	// When something changes, else every so often (a datagram can be lost)
	const TBOOL bChanged = oInfo.uiArea != s_oLastSentInfo.uiArea || oInfo.uiActivity != s_oLastSentInfo.uiActivity || oInfo.iMiniGame != s_oLastSentInfo.iMiniGame ||
	    TUtil::MemCompare( oInfo.wszName, s_oLastSentInfo.wszName, sizeof( oInfo.wszName ) ) != 0;

	s_fInfoWait -= a_fDeltaTime;

	if ( !bChanged && s_fInfoWait > 0.0f )
		return;

	s_fInfoWait       = 1.0f / ACoopPartner::INFO_RATE;
	oInfo.uiSequence  = ++s_uiInfoSequence;
	s_oLastSentInfo   = oInfo;

	ACoopNet::SendData( ACoopNet::CHANNEL_PARTNER, &oInfo, sizeof( oInfo ) );

	if ( bChanged && IsLogEnabled() )
		TINFO( "[cooppartner] sent: area %d, activity %d, minigame %d\n", oInfo.uiArea, oInfo.uiActivity, oInfo.iMiniGame );
}

// CHANNEL_PARTNER
static void OnInfoData( const void* a_pData, TINT a_iSize )
{
	if ( a_iSize != TINT( sizeof( InfoMessage ) ) )
		return;

	InfoMessage oInfo;
	TUtil::MemCopy( &oInfo, a_pData, sizeof( oInfo ) );

	if ( oInfo.uiMagic != INFO_MAGIC || oInfo.uiActivity >= ACoopPartner::ACTIVITY_NUMOF || oInfo.uiArea > ACoopNet::AREA_BARN_NIGHT )
		return;

	// Not an older one of the same run
	if ( s_oPartner.bKnown && oInfo.uiNonce == s_oPartner.oInfo.uiNonce && TINT16( oInfo.uiSequence - s_oPartner.oInfo.uiSequence ) <= 0 )
		return;

	oInfo.wszName[ ACoopPartner::MAX_NAME - 1 ] = L'\0';

	// Printable characters only
	for ( TINT i = 0; oInfo.wszName[ i ] != L'\0'; i++ )
	{
		if ( oInfo.wszName[ i ] < 0x20 )
			oInfo.wszName[ i ] = L'?';
	}

	if ( oInfo.iMiniGame >= 0 && ( !AMiniGameManager::IsSingletonCreated() || oInfo.iMiniGame >= AMiniGameManager::GetSingleton()->GetTotalNumMiniGames() ) )
		oInfo.iMiniGame = -1;

	const TBOOL bChanged = !s_oPartner.bKnown || s_oPartner.oInfo.uiActivity != oInfo.uiActivity || s_oPartner.oInfo.uiArea != oInfo.uiArea || s_oPartner.oInfo.iMiniGame != oInfo.iMiniGame;

	s_oPartner.bKnown = TTRUE;
	s_oPartner.oInfo  = oInfo;
	s_oPartner.uiTime = GetTickCount();

	if ( bChanged && IsLogEnabled() )
		TINFO( "[cooppartner] the partner: area %d, activity %d, minigame %d\n", oInfo.uiArea, oInfo.uiActivity, oInfo.iMiniGame );
}

//-----------------------------------------------------------------------------
// The line about the partner and the name tag over their cow
//-----------------------------------------------------------------------------

static constexpr TINT STATUS_SIZE = 96;

static TWCHAR s_wszStatus[ STATUS_SIZE ] = L""; // empty: no line
static TWCHAR s_wszTag[ ACoopPartner::MAX_NAME ] = L"";

// The line that says why "Go to partner" couldn't be done, shown for a few seconds
static MODSTRING s_eNote     = MODSTRING_PARTNER_GOTO_AWAY;
static TFLOAT    s_fNoteTime = 0.0f;

// Where the tag goes (GUI units from the top left of the screen), and whether it's shown
static TVector2 s_vTagPosition;
static TBOOL    s_bTagShown = TFALSE;
static TFLOAT   s_fTagAlpha = 0.0f;

static void FormatStatus( MODSTRING a_eText, const TWCHAR* a_wszArgument = L"" )
{
	// "Partner: ...", a text of the mod (AModStrings: the language of the game) with the argument for a %s. Cut to
	// the buffer if a translation or the argument is long
	AModStrings::Format( s_wszStatus, STATUS_SIZE, AModStrings::Get( a_eText ), a_wszArgument );
}

// The line at the top of the screen: what the partner does while they aren't here
static void UpdateStatus( AUnit* a_pRemoteUnit )
{
	s_wszStatus[ 0 ] = L'\0';

	if ( ACoopNet::GetMode() == ACoopNet::MODE_OFF )
		return;

	if ( s_fNoteTime > 0.0f )
	{
		FormatStatus( s_eNote );
		return;
	}

	if ( !ACoopNet::IsConnected() )
	{
		FormatStatus( MODSTRING_PARTNER_WAITING );
		return;
	}

	// Silent for a while: loading, or gone
	if ( ACoopNet::GetRemoteStateAge() > 1.5f && ( !s_oPartner.bKnown || TFLOAT( GetTickCount() - s_oPartner.uiTime ) * 0.001f > 1.5f ) )
	{
		FormatStatus( MODSTRING_PARTNER_LOADING );
		return;
	}

	if ( !s_oPartner.bKnown )
		return;

	const InfoMessage& rcInfo = s_oPartner.oInfo;

	switch ( rcInfo.uiActivity )
	{
		case ACoopPartner::ACTIVITY_FRONTEND:
			FormatStatus( MODSTRING_PARTNER_MAINMENU );
			return;
		case ACoopPartner::ACTIVITY_LOADING:
			FormatStatus( MODSTRING_PARTNER_LOADING );
			return;
		case ACoopPartner::ACTIVITY_MINIGAME:
			if ( rcInfo.iMiniGame >= 0 && AMiniGameManager::IsSingletonCreated() && g_pLocaleManager )
			{
				AMiniGame* pMiniGame = AMiniGameManager::GetSingleton()->GetMiniGame( rcInfo.iMiniGame );

				if ( pMiniGame && pMiniGame->GetLocaleTitle() >= 0 )
				{
					FormatStatus( MODSTRING_PARTNER_PLAYING, g_pLocaleManager->GetString( pMiniGame->GetLocaleTitle() ) );
					return;
				}
			}

			FormatStatus( MODSTRING_PARTNER_MINIGAME );
			return;
		case ACoopPartner::ACTIVITY_SCENE:
			FormatStatus( MODSTRING_PARTNER_SCENE );
			return;
		case ACoopPartner::ACTIVITY_SHOP:
			FormatStatus( MODSTRING_PARTNER_SHOP );
			return;
		case ACoopPartner::ACTIVITY_COOKING:
			FormatStatus( MODSTRING_PARTNER_COOKING );
			return;
		case ACoopPartner::ACTIVITY_SLEEPING:
			FormatStatus( MODSTRING_PARTNER_SLEEPING );
			return;
		case ACoopPartner::ACTIVITY_CONVERSATION:
			FormatStatus( MODSTRING_PARTNER_CONVERSATION );
			return;
		case ACoopPartner::ACTIVITY_MENU:
			FormatStatus( MODSTRING_PARTNER_MENU );
			return;
		default:
			break;
	}

	// Here: the cow and its tag say it all
	if ( a_pRemoteUnit && rcInfo.uiArea == ACoopNet::GetLocalArea() )
		return;

	switch ( rcInfo.uiArea )
	{
		case ACoopNet::AREA_FARM:
			FormatStatus( MODSTRING_PARTNER_FARM );
			return;
		case ACoopNet::AREA_BARN_DAY:
		case ACoopNet::AREA_BARN_NIGHT:
		{
			const ACoopNet::AREA eLocal = ACoopNet::GetLocalArea();

			// The other barn: each game has its own time of day
			if ( eLocal == ACoopNet::AREA_BARN_DAY || eLocal == ACoopNet::AREA_BARN_NIGHT )
				FormatStatus( ( rcInfo.uiArea == ACoopNet::AREA_BARN_NIGHT ) ? MODSTRING_PARTNER_BARN_NIGHT : MODSTRING_PARTNER_BARN_DAY );
			else
				FormatStatus( MODSTRING_PARTNER_BARN );

			return;
		}
		default:
			break;
	}
}

// Where the name tag goes: above the head of the partner's model
static void UpdateTag( TFLOAT a_fDeltaTime )
{
	s_bTagShown = TFALSE;

	AAnimatableObject* pAvatar = ACoopAnim::GetRemoteAvatar();

	if ( !pAvatar || !ARenderer::IsSingletonCreated() || !ACameraManager::IsSingletonCreated() || !AGUI2::IsSingletonCreated() )
		return;

	ACamera* pCamera = ACameraManager::GetSingleton()->GetCurrentCamera();

	if ( !pCamera )
		return;

	AModelInstance* pModelInstance = pAvatar->GetModelInstance().Get();
	TMatrix44       matModel;
	pModelInstance->GetLocalTransform( matModel );

	TVector4 vHead = matModel.GetTranslation();
	vHead.w        = 1.0f;

	// The head if the model has one, else above the model (the Y axis points down)
	const TINT iHead = pModelInstance->GetSkeletonInstance() ? pModelInstance->GetBoneID( TPS8( c_head ) ) : -1;

	TVector4 vHeadBone = vHead;

	if ( iHead >= 0 )
	{
		TMatrix44 matHead;
		pModelInstance->GetBoneTransform( iHead, matHead );
		vHead     = matHead.GetTranslation();
		vHead.w   = 1.0f;
		vHeadBone = vHead;
		vHead.y -= 0.65f;
	}
	else
	{
		vHead.y -= 2.2f;
	}

	// Near enough: it fades out between 25 and 40 metres; right at the camera it would be anywhere
	TVector4 vCamera = pCamera->GetPosition();
	TVector4 vToTag( vHead.x - vCamera.x, vHead.y - vCamera.y, vHead.z - vCamera.z, 0.0f );
	const TFLOAT fDistance = vToTag.Magnitude();

	if ( fDistance > 40.0f || fDistance < 1.5f )
		return;

	TVector2 vGUI;

	if ( !ARenderer::GetSingleton()->ProjectToGUI( pCamera, vHead, vGUI ) )
		return;

	TFLOAT fWidth, fHeight;
	AGUI2::GetRootElement()->GetDimensions( fWidth, fHeight );

	// Near the camera the 0.65 metres above the head are a big part of the screen and the tag would be over something
	// else (the local cow behind the partner's): it stays at most a tenth of the screen above the head
	TVector2 vGUIHead;

	if ( iHead >= 0 && ARenderer::GetSingleton()->ProjectToGUI( pCamera, vHeadBone, vGUIHead ) )
	{
		const TFLOAT fLift    = vGUIHead.y - vGUI.y;
		const TFLOAT fMaxLift = 0.1f * fHeight;

		if ( fLift > fMaxLift )
		{
			vGUI.x = vGUIHead.x + ( vGUI.x - vGUIHead.x ) * ( fMaxLift / fLift );
			vGUI.y = vGUIHead.y - fMaxLift;
		}
	}

	if ( vGUI.x < 0.0f || vGUI.x > fWidth || vGUI.y < 0.0f || vGUI.y > fHeight )
		return;

	s_vTagPosition = vGUI;
	s_bTagShown    = TTRUE;
	s_fTagAlpha    = ( fDistance < 25.0f ) ? 1.0f : 1.0f - ( fDistance - 25.0f ) / 15.0f;

	const TWCHAR* wszName = ACoopPartner::GetPartnerName();
	TINT          i       = 0;

	for ( ; wszName[ i ] != L'\0' && i < ACoopPartner::MAX_NAME - 1; i++ )
		s_wszTag[ i ] = wszName[ i ];

	s_wszTag[ i ] = L'\0';
}

// The line at the top and the name tag, with the HUD of the farm and of the barn
class ACoopPartnerHud : public AHudElement
{
public:
	TDECLARE_CLASS( ACoopPartnerHud, AHudElement );

	ACoopPartnerHud();
	~ACoopPartnerHud();

	virtual TBOOL OnAttach() override;
	virtual void  OnDetach() override;
	virtual void  Show() override;
	virtual void  Hide() override;
	virtual void  Update( TFLOAT a_fDeltaTime ) override;

private:
	AGUI2TextBox m_oStatus;
	AGUI2TextBox m_oTag;
	T2GUIFont*   m_pFont;
	TFLOAT       m_fStatusAlpha;
};

TDEFINE_CLASS( ACoopPartnerHud );

static ACoopPartnerHud* s_pHud = TNULL;

ACoopPartnerHud::ACoopPartnerHud()
    : AHudElement( MASK_INTERACTION )
    , m_pFont( TNULL )
    , m_fStatusAlpha( 0.0f )
{
	// Shown by AHudManager once the HUD of the current state has the interaction texts (the farm, the barn)
	m_bCurrentlyVisible = TFALSE;
}

ACoopPartnerHud::~ACoopPartnerHud()
{
	if ( s_pHud == this )
		s_pHud = TNULL;
}

TBOOL ACoopPartnerHud::OnAttach()
{
	m_pFont = T2GUIFontManager::FindFont( AGUI2STYLE_FONT_SECONDARY );

	TFLOAT fScreenWidth;
	TFLOAT fScreenHeight;
	AGUI2::GetRootElement()->GetDimensions( fScreenWidth, fScreenHeight );

	// Below the two lines of ACoopProgress at the top
	m_oStatus.Create( m_pFont, 0.0f );
	m_oStatus.SetAttachment( T2GUIATTACHMENT_TOPCENTER, T2GUIATTACHMENT_TOPCENTER );
	m_oStatus.SetColour( 0xFFFFFFFF );
	m_oStatus.SetShadowEnabled( TTRUE );
	m_oStatus.SetShadowColour( 0xA0000000 );
	m_oStatus.SetShadowAlpha( 0.6f );
	m_oStatus.SetShadowOffset( 3.5f, 3.5f );
	m_oStatus.SetSize( 0.8f * fScreenWidth, 0.0f, TFALSE );
	m_oStatus.SetTransform( 0.0f, 40.0f + 2.0f * m_pFont->GetLineHeight() * 1.25f );
	AGUI2::GetRootElement()->AddChildTail( m_oStatus );
	m_oStatus.T2GUIElement::Hide();

	// Placed over the partner's head every frame
	m_oTag.Create( m_pFont, 0.0f );
	m_oTag.SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_MIDDLECENTER );
	m_oTag.SetColour( AGUI2STYLE_COLOR_PRIMARY );
	m_oTag.SetShadowEnabled( TTRUE );
	m_oTag.SetShadowColour( 0xA0000000 );
	m_oTag.SetShadowAlpha( 0.6f );
	m_oTag.SetShadowOffset( 2.5f, 2.5f );
	m_oTag.SetSize( 0.4f * fScreenWidth, 0.0f, TFALSE );
	m_oTag.SetScale( 0.8f );
	AGUI2::GetRootElement()->AddChildTail( m_oTag );
	m_oTag.T2GUIElement::Hide();

	return TTRUE;
}

void ACoopPartnerHud::OnDetach()
{
	m_oStatus.RemoveSelf();
	m_oTag.RemoveSelf();

	T2GUIFontManager::ReleaseFont( m_pFont );
	m_pFont = TNULL;
}

void ACoopPartnerHud::Show()
{
	m_bCurrentlyVisible = TTRUE;
}

void ACoopPartnerHud::Hide()
{
	m_oStatus.T2GUIElement::Hide();
	m_oTag.T2GUIElement::Hide();
	m_fStatusAlpha      = 0.0f;
	m_bCurrentlyVisible = TFALSE;
}

void ACoopPartnerHud::Update( TFLOAT a_fDeltaTime )
{
	const TBOOL bSession = ACoopNet::GetMode() != ACoopNet::MODE_OFF;

	// The line fades in and out
	const TBOOL bStatus = bSession && s_wszStatus[ 0 ] != L'\0';
	m_fStatusAlpha      = TMath::Max( TMath::Min( m_fStatusAlpha + ( bStatus ? 4.0f : -4.0f ) * a_fDeltaTime, 1.0f ), 0.0f );

	if ( m_fStatusAlpha > 0.0f )
	{
		// The text box keeps the pointer, the buffer is static
		if ( bStatus )
			m_oStatus.SetText( s_wszStatus );

		m_oStatus.SetAlpha( m_fStatusAlpha * 0.85f );
		m_oStatus.SetShadowAlpha( m_fStatusAlpha * 0.5f );
		m_oStatus.T2GUIElement::Show();
	}
	else
	{
		m_oStatus.T2GUIElement::Hide();
	}

	if ( bSession && s_bTagShown && s_wszTag[ 0 ] != L'\0' )
	{
		m_oTag.SetText( s_wszTag );
		m_oTag.SetTransform( s_vTagPosition.x, s_vTagPosition.y );
		m_oTag.SetAlpha( s_fTagAlpha );
		m_oTag.SetShadowAlpha( s_fTagAlpha * 0.6f );
		m_oTag.T2GUIElement::Show();
	}
	else
	{
		m_oTag.T2GUIElement::Hide();
	}
}

static void EnsureHud()
{
	if ( s_pHud || !AHudManager::IsSingletonCreated() || !AGUI2::IsSingletonCreated() )
		return;

	s_pHud = new ACoopPartnerHud();

	if ( !AHudManager::GetSingleton()->AddElement( s_pHud, TTRUE, TTRUE, TFALSE ) )
	{
		delete s_pHud;
		s_pHud = TNULL;
	}
}

//-----------------------------------------------------------------------------
// The partner on the minimap: the game's "player" icon, turned with the partner's cow
//-----------------------------------------------------------------------------

static ASimMiniMap* s_pIconMap  = TNULL;
static AUnit*       s_pIconUnit = TNULL;
static TINT         s_iIcon     = -1;

// Whether the icon is still the one added here (the map is made again with the farm)
static TBOOL IsIconValid( ASimMiniMap* a_pMap )
{
	if ( s_iIcon < 0 || s_iIcon >= ASimMiniMap::MAX_ICONS || a_pMap != s_pIconMap || !a_pMap )
		return TFALSE;

	ASimMiniMap::Icon* pIcon = &a_pMap->m_aIcons[ s_iIcon ];

	if ( pIcon->m_pUnit != s_pIconUnit || pIcon->m_eType != ASimMiniMap::ICON_PLAYER )
		return TFALSE;

	for ( auto it = a_pMap->m_llUsedIcons.Begin(); it != a_pMap->m_llUsedIcons.End(); it++ )
	{
		if ( it == pIcon )
			return TTRUE;
	}

	return TFALSE;
}

static ASimMiniMap* FindMiniMap()
{
	return AHudManager::IsSingletonCreated() ? AHudManager::GetSingleton()->FindElement<ASimMiniMap>() : TNULL;
}

static void RemoveIcon()
{
	if ( s_iIcon >= 0 )
	{
		ASimMiniMap* pMap = FindMiniMap();

		if ( IsIconValid( pMap ) )
			pMap->RemoveIcon( s_iIcon );
	}

	s_iIcon     = -1;
	s_pIconMap  = TNULL;
	s_pIconUnit = TNULL;
}

static void UpdateIcon( AUnit* a_pRemoteUnit )
{
	ASimMiniMap* pMap = FindMiniMap();

	// The farm has the minimap (the barn doesn't), and the partner is shown
	const TBOOL bWanted = pMap && a_pRemoteUnit && ACoopNet::GetLocalArea() == ACoopNet::AREA_FARM && ACoopAnim::GetRemoteAvatar() != TNULL;

	if ( s_iIcon >= 0 && ( !bWanted || !IsIconValid( pMap ) || s_pIconUnit != a_pRemoteUnit ) )
		RemoveIcon();

	if ( !bWanted )
		return;

	if ( s_iIcon < 0 && !pMap->m_llFreeIcons.IsEmpty() )
	{
		s_iIcon = pMap->AddUnitIcon( a_pRemoteUnit->GetSelfProxy(), ASimMiniMap::ICON_PLAYER, 0.0f, TTRUE );

		if ( s_iIcon >= 0 )
		{
			s_pIconMap  = pMap;
			s_pIconUnit = a_pRemoteUnit;
			pMap->SetIconVisible( s_iIcon, TTRUE );

			if ( IsLogEnabled() )
				TINFO( "[cooppartner] the partner is on the minimap (icon %d)\n", s_iIcon );
		}
	}
}

//-----------------------------------------------------------------------------
// The events (CHANNEL_ACTION): what a player did once (a squirt, an emote, a hit on
// the other cow...). Every message has the events of the last second of the sender,
// so a lost datagram loses nothing; the receiver does each one once (by its id)
//-----------------------------------------------------------------------------

static constexpr TUINT32 ACTION_MAGIC   = 0x31455942; // "BYE1"
static constexpr TINT    MAX_EVENTS     = 16;
static constexpr TFLOAT  EVENT_LIFETIME = 1.0f;       // seconds an event is sent again
static constexpr TFLOAT  ACTION_RATE    = 30.0f;      // messages a second while there are events

enum EVENTTYPE : TUINT8
{
	EVENTTYPE_NONE,
	EVENTTYPE_SHOT,     // development: the receiver saves a frame now (uiParam: its number)
	EVENTTYPE_SQUIRT,   // the sender's cow squirts milk (uiParam 1: it's out of milk, it sputters)
	EVENTTYPE_SOUND,    // a sound of the sender's cow outside of its animations (afValues[0]: its id), e.g. the bike's bell
	EVENTTYPE_SPLASH,   // the sender's cow fell into deep water at afValues[0-2]
	EVENTTYPE_EMOTE,    // the sender plays an emote (uiParam: EMOTE), the receiver plays its voice
	EVENTTYPE_HIT,      // the sender hits the receiver's cow (uiParam: HITKIND, afValues[0-1]: where the sender is, XZ)
	EVENTTYPE_HIGHFIVE, // the sender offers a high five (afValues[0-1]: where it is, XZ), a free cow nearby answers it
	EVENTTYPE_NUMOF
};

#pragma pack( push, 1 )
struct ActionEvent
{
	TUINT16 uiId;
	TUINT8  uiType;  // EVENTTYPE
	TUINT8  uiParam; // depends on the type
	TFLOAT  afValues[ 4 ];
};

struct ActionMessage
{
	TUINT32     uiMagic;
	TUINT32     uiNonce; // of the sender's run: the ids start again with it
	TUINT8      uiNumEvents;
	TUINT8      auiReserved[ 3 ];
	ActionEvent aEvents[ MAX_EVENTS ];
};
#pragma pack( pop )

struct SentEvent
{
	ActionEvent oEvent;
	DWORD       uiTime; // GetTickCount when it happened
};

static SentEvent s_aSentEvents[ MAX_EVENTS ];
static TINT      s_iNextEvent      = 0; // the next slot of the ring
static TUINT16   s_uiNextEventId   = 1;
static TUINT32   s_uiActionNonce   = 0;
static TFLOAT    s_fActionWait     = 0.0f;
static TUINT32   s_uiRemoteNonce   = 0; // of the events that came last
static TUINT16   s_uiLastAppliedId = 0;
static TBOOL     s_bAppliedAny     = TFALSE;

static void QueueEvent( EVENTTYPE a_eType, TUINT8 a_uiParam, TFLOAT a_fA = 0.0f, TFLOAT a_fB = 0.0f, TFLOAT a_fC = 0.0f, TFLOAT a_fD = 0.0f )
{
	if ( !ACoopNet::IsConnected() )
		return;

	SentEvent& rSent             = s_aSentEvents[ s_iNextEvent ];
	s_iNextEvent                 = ( s_iNextEvent + 1 ) % MAX_EVENTS;
	rSent.oEvent.uiId            = s_uiNextEventId++;
	rSent.oEvent.uiType          = a_eType;
	rSent.oEvent.uiParam         = a_uiParam;
	rSent.oEvent.afValues[ 0 ]   = a_fA;
	rSent.oEvent.afValues[ 1 ]   = a_fB;
	rSent.oEvent.afValues[ 2 ]   = a_fC;
	rSent.oEvent.afValues[ 3 ]   = a_fD;
	rSent.uiTime                 = GetTickCount();

	if ( s_uiNextEventId == 0 )
		s_uiNextEventId = 1;

	// Out with the next message
	s_fActionWait = 0.0f;
}

static void SendEvents( TFLOAT a_fDeltaTime )
{
	s_fActionWait -= a_fDeltaTime;

	if ( s_fActionWait > 0.0f )
		return;

	s_fActionWait = 1.0f / ACTION_RATE;

	if ( s_uiActionNonce == 0 )
		s_uiActionNonce = ( GetTickCount() * 3266489917u ) ^ ( GetCurrentProcessId() << 4 ) | 1;

	ActionMessage oMessage = {};
	oMessage.uiMagic       = ACTION_MAGIC;
	oMessage.uiNonce       = s_uiActionNonce;

	const DWORD uiNow = GetTickCount();

	// The oldest first
	for ( TINT i = 0; i < MAX_EVENTS; i++ )
	{
		const SentEvent& rcSent = s_aSentEvents[ ( s_iNextEvent + i ) % MAX_EVENTS ];

		if ( rcSent.oEvent.uiType != EVENTTYPE_NONE && TFLOAT( uiNow - rcSent.uiTime ) * 0.001f < EVENT_LIFETIME )
			oMessage.aEvents[ oMessage.uiNumEvents++ ] = rcSent.oEvent;
	}

	if ( oMessage.uiNumEvents == 0 )
		return;

	const TINT iSize = TINT( sizeof( ActionMessage ) - sizeof( oMessage.aEvents ) + oMessage.uiNumEvents * sizeof( ActionEvent ) );
	ACoopNet::SendData( ACoopNet::CHANNEL_ACTION, &oMessage, iSize );
}

#ifndef TOSHI_FINAL
#  include <Console/PConsole.h>

// Development: "-mpshots <path prefix>" saves frames of both games at the same moment, "-mpshotat t1,t2,..." (host:
// seconds after its player is first in an area) says when: <prefix>_host_<n>.png here, <prefix>_join_<n>.png there
static TCHAR  s_szShotPrefix[ MAX_PATH ] = "";
static TFLOAT s_afShotAt[ 16 ];
static TINT   s_iNumShotAt   = 0;
static TINT   s_iNextShot    = 0;
static TFLOAT s_fInAreaTime  = -1.0f;
static TBOOL  s_bShotsParsed = TFALSE;

static void ParseShots()
{
	if ( s_bShotsParsed )
		return;

	s_bShotsParsed = TTRUE;

	const TCHAR* szCommandLine = AGameLoader::Dev_GetCommandLine();
	const TCHAR* szPrefix      = szCommandLine ? TStringManager::String8FindString( szCommandLine, "-mpshots " ) : TNULL;

	if ( !szPrefix )
		return;

	szPrefix += TStringManager::String8Length( "-mpshots " );
	TINT iLength = 0;

	while ( szPrefix[ iLength ] != '\0' && szPrefix[ iLength ] != ' ' && iLength < MAX_PATH - 16 )
	{
		s_szShotPrefix[ iLength ] = szPrefix[ iLength ];
		iLength++;
	}

	s_szShotPrefix[ iLength ] = '\0';

	const TCHAR* szAt = TStringManager::String8FindString( szCommandLine, "-mpshotat " );

	if ( szAt )
	{
		szAt += TStringManager::String8Length( "-mpshotat " );

		while ( s_iNumShotAt < TARRAYSIZE( s_afShotAt ) )
		{
			TCHAR* szEnd;
			const TFLOAT fTime = TFLOAT( strtod( szAt, &szEnd ) );

			if ( szEnd == szAt )
				break;

			s_afShotAt[ s_iNumShotAt++ ] = fTime;
			szAt                         = szEnd;

			if ( *szAt != ',' )
				break;

			szAt++;
		}
	}

	TINFO( "[cooppartner] development shots: %s, %d times\n", s_szShotPrefix, s_iNumShotAt );
}

static void TakeShot( const TCHAR* a_szWho, TINT a_iNumber )
{
	if ( s_szShotPrefix[ 0 ] == '\0' )
		return;

	TCHAR szLine[ MAX_PATH + 32 ];
	TStringManager::String8Format( szLine, sizeof( szLine ), "screenshot %s_%s_%d.png", s_szShotPrefix, a_szWho, a_iNumber );
	PConsole::Execute( szLine );
	TINFO( "[cooppartner] shot %d (%s)\n", a_iNumber, a_szWho );
}

static void UpdateShots( TFLOAT a_fDeltaTime )
{
	ParseShots();

	if ( s_iNumShotAt == 0 || ACoopNet::GetMode() != ACoopNet::MODE_HOST )
		return;

	if ( s_fInAreaTime < 0.0f )
	{
		if ( ACoopNet::GetLocalArea() == ACoopNet::AREA_NONE )
			return;

		s_fInAreaTime = 0.0f;
	}

	s_fInAreaTime += a_fDeltaTime;

	if ( s_iNextShot < s_iNumShotAt && s_fInAreaTime >= s_afShotAt[ s_iNextShot ] )
	{
		s_iNextShot++;
		TakeShot( "host", s_iNextShot );
		QueueEvent( EVENTTYPE_SHOT, TUINT8( s_iNextShot ) );
	}
}
#endif


//-----------------------------------------------------------------------------
// The local cow and what the two players do to each other. Each game only moves
// its own cow: a hit is sent to the other game, which applies the game's own
// reaction to its cow if it's free (the hop of a jump, pushed away, a voice, and
// knocked over by a tackle or a bike) and the other screen sees it in the pose
//-----------------------------------------------------------------------------

// How far a kick and a high five reach (metres)
static constexpr TFLOAT KICK_REACH     = 1.9f;
static constexpr TFLOAT HIGHFIVE_REACH = 3.0f;

// The partner running into this cow at least this fast pushes it (metres a second), from TACKLE_SPEED it knocks it
// over, and a bike does from BIKE_HIT_SPEED (sprinting is about 8, running 4)
static constexpr TFLOAT BUMP_SPEED     = 5.5f;
static constexpr TFLOAT TACKLE_SPEED   = 7.5f;
static constexpr TFLOAT BIKE_HIT_SPEED = 3.0f;

// Seconds between two hits on this cow
static constexpr TFLOAT HIT_COOLDOWN = 1.5f;

enum HITKIND : TUINT8
{
	HITKIND_BUMP,   // the partner ran into this cow
	HITKIND_KICK,   // the partner's kick
	HITKIND_TACKLE, // the partner sprinted into this cow: knocked over
	HITKIND_BIKE,   // the partner rode into this cow: knocked over
	HITKIND_NUMOF
};

static const TCHAR* s_aszHitNames[ HITKIND_NUMOF ] = { "bump", "kick", "tackle", "bike" };

enum EMOTE : TINT
{
	EMOTE_WAVE,
	EMOTE_LAUGH,
	EMOTE_HIGHFIVE,
	EMOTE_KICK,
	EMOTE_YAWN,
	EMOTE_FALL,
	EMOTE_YUK,
	EMOTE_SAD,
	EMOTE_NUMOF
};

struct EmoteDef
{
	const TCHAR* szName;
	const TCHAR* szAnim;        // the animation of the cow (the "default" set has them all)
	const TCHAR* szNextAnim;    // played when the first one is over (TNULL: none)
	TFLOAT       fMaxTime;      // it's over after this long (seconds)
	ASoundId     eMaleVoice;    // a voice of its own (-1: the animation has one, or none)
	ASoundId     eFemaleVoice;
	TBOOL        bHoldMovement; // the animation doesn't hold the cow (APlayerSteer::CanStartAction), it's held here
	TFLOAT       fStopAfter;    // a looped animation is ended after this long (0: it ends by itself)
};

static const EmoteDef s_aEmotes[ EMOTE_NUMOF ] = {
	{ "wave", "wave", TNULL, 3.0f, -1, -1, TFALSE, 0.0f },
	{ "laugh", "talk_01", TNULL, 2.5f, soundbank::AM_M_PLAYERM_LAUGH, soundbank::AM_F_PLAYERF_LAUGH, TTRUE, 1.8f },
	{ "high five", "high_five", TNULL, 2.5f, -1, -1, TTRUE, 0.0f },
	{ "kick", "kick", TNULL, 2.0f, -1, -1, TTRUE, 0.0f },
	{ "yawn", "yawn", TNULL, 4.0f, -1, -1, TFALSE, 0.0f },
	{ "fall over", "bk_crashoffbike_land", "bk_crashoffbike_getup", 6.0f, soundbank::AM_M_OUCH, soundbank::AM_F_OUCH, TTRUE, 0.0f },
	{ "yuk", "yuk", TNULL, 3.0f, -1, -1, TFALSE, 0.0f },
	{ "sad", "defeat", TNULL, 4.0f, soundbank::AM_M_FAIL_M, soundbank::AM_F_FAIL_F, TFALSE, 0.0f },
};

// "Go to partner" (see below)
enum GOTO : TINT
{
	GOTO_NONE,
	GOTO_FADE_OUT,
	GOTO_HOLD,
	GOTO_FADE_IN,
};

static GOTO              s_eGoTo         = GOTO_NONE;
static TFLOAT            s_fGoToTime     = 0.0f;

// What the local cow does for this module: an emote, or the reaction to a hit
struct LocalAction
{
	TBOOL    bActive;
	AUnit*   pUnit;     // the cow it's done with (it ends if that isn't the local player any more)
	TINT     iEmote;    // EMOTE, -1: the reaction to a hit
	HITKIND  eHit;
	TINT     iStep;     // the animation of the emote, or the part of the reaction (0 pushed, 1 down, 2 getting up)
	TFLOAT   fTime;
	TFLOAT   fStepTime;
	TBOOL    bHolding;  // the movement of the cow is held here
	TBOOL    bDone;     // the kick has hit / the high five was offered
	TBOOL    bAnswer;   // a high five that answers the partner's (not offered again)
	TVector4 vFacing;   // the reaction: the cow faces the one who hit it...
	TFLOAT   fPushSpeed; // ...and is pushed backwards
};

static LocalAction s_oAction       = {};
static TFLOAT      s_fHitCooldown  = 0.0f;
static TFLOAT      s_fRemoteSpeed  = 0.0f; // the partner's highest speed of the last moments (it stops when it hits)
static TINT        s_iSplashEffects[ 3 ] = { -2, -2, -2 };

static AAnimatableObject* GetModel( AUnit* a_pUnit )
{
	return ( a_pUnit && a_pUnit->IsA( &TGetClass( AAnimatedUnit ) ) ) ? TSTATICCAST( AAnimatedUnit, a_pUnit )->GetAnimatableObject() : TNULL;
}

static ASimplePlayerControlComponent* GetControl( AUnit* a_pPlayer )
{
	return TSTATICCAST( ASimplePlayerControlComponent, a_pPlayer->FindComponent( &TGetClass( APlayerControlComponent ) ) );
}

static TBOOL IsLocalFemale()
{
	return APlayerProfileManager::IsSingletonCreated() && APlayerProfileManager::GetSingleton()->GetMainGender() == APlayerProfile::GENDER_FEMALE;
}

static void PlayVoice( AUnit* a_pUnit, TBOOL a_bFemale, ASoundId a_eMale, ASoundId a_eFemale )
{
	const ASoundId eSound = a_bFemale ? a_eFemale : a_eMale;

	if ( eSound >= 0 && a_pUnit && ASoundManager::IsSingletonCreated() )
		ASoundManager::GetSingleton()->PlayCueAtUnit( eSound, a_pUnit );
}

// The cow whose movement this module holds (an emote, a hit, "Go to partner"): it could move when it was taken, and
// is given back as soon as nothing of this module runs. If the game sets the movement of that cow meanwhile (a quest,
// the deep water, a micro game), it's the game's from then on (ACoopPartner::OnMovementEnabledSet).
// (A proxy: the cow may be gone with its level, or be kept by the bike it got on, when it's given back)
static TRef<AUnitProxy>* s_pHeldUnit        = TNULL;
static TBOOL             s_bSettingMovement = TFALSE;

static AUnit* GetHeldUnit()
{
	return ( s_pHeldUnit && *s_pHeldUnit ) ? ( *s_pHeldUnit )->GetUnit() : TNULL;
}

static void ForgetHeldUnit()
{
	if ( s_pHeldUnit )
	{
		delete s_pHeldUnit;
		s_pHeldUnit = TNULL;
	}
}

static void ReleaseMovement()
{
	if ( !s_pHeldUnit )
		return;

	if ( AUnit* pHeld = GetHeldUnit() )
	{
		if ( ASimplePlayerControlComponent* pControl = GetControl( pHeld ) )
		{
			s_bSettingMovement = TTRUE;
			pControl->SetMovementEnabled( TTRUE );
			s_bSettingMovement = TFALSE;
		}
	}

	ForgetHeldUnit();
}

static void HoldMovement( AUnit* a_pPlayer )
{
	if ( !a_pPlayer || GetHeldUnit() == a_pPlayer )
		return;

	// Another cow is still held (it isn't the player's any more): given back first
	ReleaseMovement();

	ASimplePlayerControlComponent* pControl = GetControl( a_pPlayer );

	// Held already by the game: not ours to give back
	if ( pControl && pControl->IsMovementEnabled() )
	{
		s_bSettingMovement = TTRUE;
		pControl->SetMovementEnabled( TFALSE );
		s_bSettingMovement = TFALSE;

		s_pHeldUnit = new TRef<AUnitProxy>( a_pPlayer->GetSelfProxy() );
	}
}

void ACoopPartner::OnMovementEnabledSet( AUnit* a_pUnit )
{
	if ( !s_bSettingMovement && s_pHeldUnit && GetHeldUnit() == a_pUnit )
		ForgetHeldUnit();
}

// Why the local cow can't do an emote or take a hit now, TNULL if it can. a_bForHit lets a running emote of this
// module be cut short by a hit
static const TCHAR* GetBusyReason( AUnit* a_pPlayer, TBOOL a_bForHit, APlayerSteer** a_ppOutSteer )
{
	*a_ppOutSteer = TNULL;

	if ( !a_pPlayer )
		return "not in an area";

	// A hit only while the cow is free; an emote of the player also in a micro game it can't disturb
	if ( a_bForHit ? ( s_eLocalActivity != ACoopPartner::ACTIVITY_FREE ) : !AreEmotesAllowed() )
		return "busy (a menu, a conversation, a scene, a micro game...)";

	// (The gold rush starts with the cow pulling its lever, the game waits for that animation)
	if ( s_bEmotesInMicroGame && ACoopAnim::IsLocalPlaying( a_pPlayer, "coinspawner" ) )
		return "pulling the lever of the gold rush";

	ASteer* pSteer = a_pPlayer->GetSteer();

	if ( !pSteer || !pSteer->IsA( &TGetClass( APlayerSteer ) ) )
		return "not on foot";

	APlayerSteer* pPlayerSteer = TSTATICCAST( APlayerSteer, pSteer );

	if ( pPlayerSteer->GetState() != APlayerSteer::STATE_WALKING && pPlayerSteer->GetState() != APlayerSteer::STATE_STILL )
		return "jumping, falling or climbing";

	if ( pPlayerSteer->IsInWater() )
		return "in the water";

	const TBOOL bOwnAction = s_oAction.bActive && s_oAction.pUnit == a_pPlayer;

	if ( bOwnAction && ( !a_bForHit || s_oAction.iEmote < 0 ) )
		return "doing something already";

	if ( !bOwnAction && !pPlayerSteer->CanStartAction() )
		return "busy with an action";

	if ( a_pPlayer->HasInteraction() )
		return "in an interaction";

	ASimplePlayerControlComponent* pControl = GetControl( a_pPlayer );

	if ( !pControl || ( !pControl->IsMovementEnabled() && GetHeldUnit() != a_pPlayer ) )
		return "can't move now";

	if ( AQuestManager::IsSingletonCreated() && AQuestManager::GetSingleton()->IsTimerPaused() )
		return "a quest scene runs";

	if ( AMiniGameManager::IsSingletonCreated() && !AMiniGameManager::GetSingleton()->GetNextMiniGameName().IsEmpty() )
		return "a minigame is about to start";

	if ( TSTATICCAST( AAnimatedUnit, a_pPlayer )->GetAttachedUnit( TPS8D( "cb_rlocator" ), -1 ) )
		return "carrying something";

	*a_ppOutSteer = pPlayerSteer;
	return TNULL;
}

// The movement is given back by UpdateLocalAction (ReleaseMovement)
static void EndAction()
{
	s_oAction.bActive = TFALSE;
}

static TBOOL PlayOnLocalCow( AUnit* a_pPlayer, const TCHAR* a_szAnim )
{
	AAnimatableObject* pObject = GetModel( a_pPlayer );
	TPString8          strAnim( a_szAnim );

	if ( !pObject || !pObject->HasAnimation( strAnim ) )
		return TFALSE;

	pObject->PlayAnimation( strAnim );
	return TTRUE;
}

// Where the partner is as this game shows it (TFALSE if not shown)
static TBOOL GetPartnerPosition( TVector4& a_rOutPosition )
{
	AUnit* pRemote = ACoopNet::GetRemotePlayerUnit();

	if ( !pRemote || !ACoopAnim::GetRemoteAvatar() || !pRemote->GetSteer() )
		return TFALSE;

	pRemote->GetSteer()->CopyPosition( a_rOutPosition );
	return TTRUE;
}

static TBOOL StartEmote( AUnit* a_pPlayer, TINT a_iEmote, TBOOL a_bAnswer )
{
	APlayerSteer* pSteer = TNULL;
	const TCHAR*  szBusy = GetBusyReason( a_pPlayer, TFALSE, &pSteer );

	if ( szBusy )
	{
		if ( IsLogEnabled() )
			TINFO( "[cooppartner] no %s now: %s\n", s_aEmotes[ a_iEmote ].szName, szBusy );

		return TFALSE;
	}

	const EmoteDef& rcEmote = s_aEmotes[ a_iEmote ];

	// The animation set of the cow has it (the carried items have their own sets)
	if ( !PlayOnLocalCow( a_pPlayer, rcEmote.szAnim ) )
	{
		if ( IsLogEnabled() )
			TINFO( "[cooppartner] no %s now: the cow has no animation %s\n", rcEmote.szName, rcEmote.szAnim );

		return TFALSE;
	}

	s_oAction            = {};
	s_oAction.bActive    = TTRUE;
	s_oAction.pUnit      = a_pPlayer;
	s_oAction.iEmote     = a_iEmote;
	s_oAction.bAnswer    = a_bAnswer;

	if ( rcEmote.bHoldMovement )
	{
		HoldMovement( a_pPlayer );
		pSteer->SetSpeed( 0.0f );
	}

	PlayVoice( a_pPlayer, IsLocalFemale(), rcEmote.eMaleVoice, rcEmote.eFemaleVoice );

	// The other game plays the voice at this cow (the animation comes with the pose)
	QueueEvent( EVENTTYPE_EMOTE, TUINT8( a_iEmote ) );

	// A high five near the partner is offered to them
	if ( a_iEmote == EMOTE_HIGHFIVE && !a_bAnswer )
	{
		TVector4 vPartner;

		if ( GetPartnerPosition( vPartner ) )
		{
			const TVector4& rcPosition = *a_pPlayer->GetSteer()->GetPosition();

			if ( TVector4::DistanceSq( vPartner, rcPosition ) < HIGHFIVE_REACH * HIGHFIVE_REACH )
			{
				QueueEvent( EVENTTYPE_HIGHFIVE, 0, rcPosition.x, rcPosition.z );
				PlayVoice( a_pPlayer, IsLocalFemale(), soundbank::AM_M_CHEER, soundbank::AM_F_CHEER );
				TINFO( "[cooppartner] high five offered to the partner\n" );
			}
		}
	}

	TINFO( "[cooppartner] emote: %s%s\n", rcEmote.szName, a_bAnswer ? " (answering the partner)" : "" );
	return TTRUE;
}

// The partner hit the local cow from a_rcFrom
static TBOOL StartHit( AUnit* a_pPlayer, HITKIND a_eKind, const TVector4& a_rcFrom )
{
	APlayerSteer* pSteer = TNULL;
	const TCHAR*  szBusy = ( s_fHitCooldown > 0.0f ) ? "just hit" : GetBusyReason( a_pPlayer, TTRUE, &pSteer );

	if ( szBusy )
	{
		TINFO( "[cooppartner] the partner's %s isn't taken: %s\n", s_aszHitNames[ a_eKind ], szBusy );
		return TFALSE;
	}

	// An emote of this cow ends here
	EndAction();

	const TVector4& rcPosition = *a_pPlayer->GetSteer()->GetPosition();

	// It faces the one who hit it, and is pushed away from it
	TVector4 vFacing( a_rcFrom.x - rcPosition.x, 0.0f, a_rcFrom.z - rcPosition.z, 0.0f );

	if ( vFacing.MagnitudeSqXZ() < 0.0001f )
		pSteer->CopyDirection( vFacing );

	vFacing.y = 0.0f;
	vFacing.w = 0.0f;
	vFacing.Normalise();

	static const TFLOAT s_afPush[ HITKIND_NUMOF ] = { 4.5f, 6.0f, 8.0f, 9.0f };

	s_oAction            = {};
	s_oAction.bActive    = TTRUE;
	s_oAction.pUnit      = a_pPlayer;
	s_oAction.iEmote     = -1;
	s_oAction.eHit       = a_eKind;
	s_oAction.vFacing    = vFacing;
	s_oAction.fPushSpeed = s_afPush[ a_eKind ];

	HoldMovement( a_pPlayer );

	ASteerKinematics* pKinematics = pSteer->GetKinematics();
	pKinematics->SetFacing( vFacing );
	pKinematics->SetDesiredFacing( vFacing );

	// A hop: the game's own jump (its animations, its landing)
	pSteer->SetState( APlayerSteer::STATE_JUMPING );
	pKinematics->SetSpeed( -s_oAction.fPushSpeed );

	const TBOOL bHeavy = a_eKind == HITKIND_TACKLE || a_eKind == HITKIND_BIKE;
	PlayVoice( a_pPlayer, IsLocalFemale(), bHeavy ? soundbank::AM_M_CRASH : soundbank::AM_M_OUCH, bHeavy ? soundbank::AM_F_CRASH : soundbank::AM_F_OUCH );

	s_fHitCooldown = HIT_COOLDOWN;

	TINFO( "[cooppartner] hit by the partner: %s\n", s_aszHitNames[ a_eKind ] );
	return TTRUE;
}

static void UpdateEmote( TFLOAT a_fDeltaTime, AUnit* a_pPlayer )
{
	const EmoteDef&    rcEmote = s_aEmotes[ s_oAction.iEmote ];
	AAnimatableObject* pObject = GetModel( a_pPlayer );

	if ( !pObject || s_oAction.fTime > rcEmote.fMaxTime )
	{
		EndAction();
		return;
	}

	const TCHAR*    szAnim = ( s_oAction.iStep == 0 ) ? rcEmote.szAnim : rcEmote.szNextAnim;
	const TPString8 strAnim( szAnim );
	const TBOOL     bPlaying = pObject->IsAnimPlaying( strAnim, 0.0f ) || pObject->IsAnimationPlanned( strAnim );

	// The kick hits the partner in front of the cow
	if ( s_oAction.iEmote == EMOTE_KICK && !s_oAction.bDone && s_oAction.fTime >= 0.3f )
	{
		s_oAction.bDone = TTRUE;

		TVector4 vPartner;

		if ( GetPartnerPosition( vPartner ) )
		{
			const TVector4& rcPosition = *a_pPlayer->GetSteer()->GetPosition();
			TVector4        vToPartner( vPartner.x - rcPosition.x, 0.0f, vPartner.z - rcPosition.z, 0.0f );
			const TFLOAT    fDistance = vToPartner.Magnitude();

			TVector4 vFacing;
			a_pPlayer->GetSteer()->CopyDirection( vFacing );

			if ( fDistance < KICK_REACH && TMath::Abs( vPartner.y - rcPosition.y ) < 1.0f && ( fDistance < 0.2f || TVector4::DotProduct3( vFacing, vToPartner ) / fDistance > 0.5f ) )
			{
				QueueEvent( EVENTTYPE_HIT, HITKIND_KICK, rcPosition.x, rcPosition.z );
				TINFO( "[cooppartner] the kick hits the partner (%.2f m away)\n", fDistance );
			}
		}
	}

	// A looped one is ended here
	if ( rcEmote.fStopAfter > 0.0f && s_oAction.fTime >= rcEmote.fStopAfter )
	{
		PlayOnLocalCow( a_pPlayer, "idle_lp" );
		EndAction();
		return;
	}

	TFLOAT fProgress = pObject->GetAnimationProgress( strAnim );

	// Over: by its own transition, or at its end without one (it'd stay in its last pose)
	if ( ( !bPlaying && s_oAction.fStepTime > 0.2f ) || ( bPlaying && fProgress >= 0.97f && s_oAction.fStepTime > 0.2f ) )
	{
		if ( s_oAction.iStep == 0 && rcEmote.szNextAnim )
		{
			s_oAction.iStep     = 1;
			s_oAction.fStepTime = 0.0f;

			if ( !PlayOnLocalCow( a_pPlayer, rcEmote.szNextAnim ) )
				EndAction();

			return;
		}

		if ( bPlaying )
			PlayOnLocalCow( a_pPlayer, "idle_lp" );

		EndAction();
	}
}

static void UpdateHit( TFLOAT a_fDeltaTime, AUnit* a_pPlayer )
{
	ASteer* pSteer = a_pPlayer->GetSteer();

	if ( !pSteer || !pSteer->IsA( &TGetClass( APlayerSteer ) ) || s_oAction.fTime > 7.0f )
	{
		EndAction();
		return;
	}

	APlayerSteer* pPlayerSteer = TSTATICCAST( APlayerSteer, pSteer );
	const TBOOL   bHeavy       = s_oAction.eHit == HITKIND_TACKLE || s_oAction.eHit == HITKIND_BIKE;
	const TBOOL   bOnGround    = pPlayerSteer->GetState() == APlayerSteer::STATE_WALKING || pPlayerSteer->GetState() == APlayerSteer::STATE_STILL;

	switch ( s_oAction.iStep )
	{
		case 0:
		{
			// Pushed backwards, slower and slower (the steer's collision keeps it out of the walls)
			const TFLOAT fPush = 0.4f;

			if ( s_oAction.fTime < fPush && !pPlayerSteer->IsInWater() )
			{
				ASteerKinematics* pKinematics = pPlayerSteer->GetKinematics();
				pKinematics->SetFacing( s_oAction.vFacing );
				pKinematics->SetDesiredFacing( s_oAction.vFacing );
				pKinematics->SetSpeed( -s_oAction.fPushSpeed * ( 1.0f - s_oAction.fTime / fPush ) );
			}

			if ( s_oAction.fTime >= fPush && bOnGround )
			{
				if ( !bHeavy || pPlayerSteer->IsInWater() || !PlayOnLocalCow( a_pPlayer, "bk_crashoffbike_land" ) )
				{
					EndAction();
					return;
				}

				// Knocked over
				pPlayerSteer->SetSpeed( 0.0f );
				s_oAction.iStep     = 1;
				s_oAction.fStepTime = 0.0f;
			}

			// The landing doesn't come (it fell from somewhere)
			if ( s_oAction.fTime > 3.0f )
				EndAction();

			break;
		}
		case 1:
		case 2:
		{
			AAnimatableObject* pObject  = GetModel( a_pPlayer );
			const TCHAR*       szAnim   = ( s_oAction.iStep == 1 ) ? "bk_crashoffbike_land" : "bk_crashoffbike_getup";
			const TPString8    strAnim( szAnim );
			const TBOOL        bPlaying = pObject && ( pObject->IsAnimPlaying( strAnim, 0.0f ) || pObject->IsAnimationPlanned( strAnim ) );
			const TFLOAT       fProgress = pObject ? pObject->GetAnimationProgress( strAnim ) : 1.0f;

			if ( s_oAction.fStepTime > 0.2f && ( !bPlaying || fProgress >= 0.97f ) )
			{
				if ( s_oAction.iStep == 1 && PlayOnLocalCow( a_pPlayer, "bk_crashoffbike_getup" ) )
				{
					s_oAction.iStep     = 2;
					s_oAction.fStepTime = 0.0f;
					break;
				}

				PlayOnLocalCow( a_pPlayer, "idle_lp" );
				EndAction();
			}

			break;
		}
	}
}

static void UpdateLocalAction( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer )
{
	s_fHitCooldown = TMath::Max( s_fHitCooldown - a_fDeltaTime, 0.0f );

	// The cow isn't the local player's any more (another level, a scene, the bike...)
	if ( s_oAction.bActive && a_pLocalPlayer != s_oAction.pUnit )
		s_oAction.bActive = TFALSE;

	if ( !s_oAction.bActive )
	{
		// At once, whatever the game does now: a micro game that starts right after an emote needs the cow to walk
		if ( s_eGoTo == GOTO_NONE )
			ReleaseMovement();

		return;
	}

	// The game is paused in a menu: the emote or the fall goes on after it
	if ( s_eLocalActivity == ACoopPartner::ACTIVITY_MENU )
		return;

	// A conversation, a scene, a micro game began: they have the cow now (its animations too). An emote goes on in a
	// micro game it can't disturb
	if ( ( s_oAction.iEmote >= 0 ) ? !AreEmotesAllowed() : ( s_eLocalActivity != ACoopPartner::ACTIVITY_FREE ) )
	{
		EndAction();
		return;
	}

	s_oAction.fTime += a_fDeltaTime;
	s_oAction.fStepTime += a_fDeltaTime;

	if ( s_oAction.iEmote >= 0 )
		UpdateEmote( a_fDeltaTime, a_pLocalPlayer );
	else
		UpdateHit( a_fDeltaTime, a_pLocalPlayer );
}

// The partner runs (or rides) into the local cow: this game sees it and pushes its own cow
static void UpdateBumps( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer, AUnit* a_pRemoteUnit )
{
	TVector4 vRemote, vRemoteDirection;
	TFLOAT   fRemoteSpeed = 0.0f;

	if ( !a_pRemoteUnit || !ACoopAnim::GetRemoteAvatar() || !ACoopNet::GetRemoteState( vRemote, vRemoteDirection, fRemoteSpeed ) )
	{
		s_fRemoteSpeed = 0.0f;
		return;
	}

	// Its highest speed of the last moments: it slows down when it hits
	s_fRemoteSpeed = TMath::Max( fRemoteSpeed, s_fRemoteSpeed - a_fDeltaTime * 12.0f );

	if ( !a_pLocalPlayer || s_fHitCooldown > 0.0f || s_oAction.bActive )
		return;

	const TBOOL  bRiding  = ACoopAnim::IsRemoteRiding();
	const TFLOAT fMinimum = bRiding ? BIKE_HIT_SPEED : BUMP_SPEED;

	if ( s_fRemoteSpeed < fMinimum )
		return;

	const TVector4& rcLocal = *a_pLocalPlayer->GetSteer()->GetPosition();

	if ( TMath::Abs( rcLocal.y - vRemote.y ) > 1.2f )
		return;

	TVector4     vToLocal( rcLocal.x - vRemote.x, 0.0f, rcLocal.z - vRemote.z, 0.0f );
	const TFLOAT fDistance = vToLocal.Magnitude();

	// Touching (the collision spheres are about 0.55 big), and coming this way
	if ( fDistance > 1.45f || ( fDistance > 0.05f && TVector4::DotProduct3( vRemoteDirection, vToLocal ) / fDistance < 0.5f ) )
		return;

	const HITKIND eKind = bRiding ? HITKIND_BIKE : ( s_fRemoteSpeed >= TACKLE_SPEED ? HITKIND_TACKLE : HITKIND_BUMP );

	if ( StartHit( a_pLocalPlayer, eKind, vRemote ) )
		TINFO( "[cooppartner] the partner ran into this cow at %.1f m/s\n", s_fRemoteSpeed );
}

//-----------------------------------------------------------------------------
// The emote keys: 1-8 (also on the numpad), a pad's right stick button held with the
// D-pad (up: wave, right: high five, down: kick, left: laugh)
//-----------------------------------------------------------------------------

static TBOOL s_abEmoteKeyDown[ EMOTE_NUMOF ];
static TBOOL s_abEmotePadDown[ 4 ];
static TBOOL s_bEmoteChordHeld = TFALSE;

static void UpdateEmoteKeys( AUnit* a_pLocalPlayer )
{
	if ( !AInputHandler::IsSingletonCreated() )
		return;

	AInputHandler* pInput = AInputHandler::GetSingleton();

	static const TInputDevice::Doodad s_aKeys[ EMOTE_NUMOF ] = {
		TInputDeviceKeyboard::KEY_1, TInputDeviceKeyboard::KEY_2, TInputDeviceKeyboard::KEY_3, TInputDeviceKeyboard::KEY_4,
		TInputDeviceKeyboard::KEY_5, TInputDeviceKeyboard::KEY_6, TInputDeviceKeyboard::KEY_7, TInputDeviceKeyboard::KEY_8
	};

	static const TInputDevice::Doodad s_aNumpad[ EMOTE_NUMOF ] = {
		TInputDeviceKeyboard::KEY_NUMPAD1, TInputDeviceKeyboard::KEY_NUMPAD2, TInputDeviceKeyboard::KEY_NUMPAD3, TInputDeviceKeyboard::KEY_NUMPAD4,
		TInputDeviceKeyboard::KEY_NUMPAD5, TInputDeviceKeyboard::KEY_NUMPAD6, TInputDeviceKeyboard::KEY_NUMPAD7, TInputDeviceKeyboard::KEY_NUMPAD8
	};

	// The game doesn't take keys now (a menu, a fade): pressed ones aren't started later
	const TBOOL bTakesInput = a_pLocalPlayer && AreEmotesAllowed() && ACoopNet::IsConnected();

	TInputDeviceKeyboard* pKeyboard = pInput->GetKeyboardDevice();

	for ( TINT i = 0; i < EMOTE_NUMOF; i++ )
	{
		const TBOOL bDown = pKeyboard && ( pKeyboard->IsDown( s_aKeys[ i ] ) || pKeyboard->IsDown( s_aNumpad[ i ] ) );

		if ( bDown && !s_abEmoteKeyDown[ i ] && bTakesInput )
			StartEmote( a_pLocalPlayer, i, TFALSE );

		s_abEmoteKeyDown[ i ] = bDown;
	}

	static const TInputDevice::Doodad s_aPad[ 4 ] = {
		TInputDeviceController::BUTTON_DPAD_UP, TInputDeviceController::BUTTON_DPAD_RIGHT, TInputDeviceController::BUTTON_DPAD_DOWN, TInputDeviceController::BUTTON_DPAD_LEFT
	};

	static const TINT s_aPadEmotes[ 4 ] = { EMOTE_WAVE, EMOTE_HIGHFIVE, EMOTE_KICK, EMOTE_LAUGH };

	TBOOL abPadDown[ 4 ] = {};
	s_bEmoteChordHeld    = TFALSE;

	for ( TINT iController = 0; iController < pInput->GetNumControllers(); iController++ )
	{
		TInputDeviceController* pController = pInput->GetController( iController );

		if ( !pController || !pController->IsDown( TInputDeviceController::BUTTON_STICK_RIGHT ) )
			continue;

		s_bEmoteChordHeld = TTRUE;

		for ( TINT i = 0; i < 4; i++ )
			abPadDown[ i ] |= pController->IsDown( s_aPad[ i ] );
	}

	for ( TINT i = 0; i < 4; i++ )
	{
		if ( abPadDown[ i ] && !s_abEmotePadDown[ i ] && bTakesInput )
			StartEmote( a_pLocalPlayer, s_aPadEmotes[ i ], TFALSE );

		s_abEmotePadDown[ i ] = abPadDown[ i ];
	}
}

//-----------------------------------------------------------------------------
// The partner's squirts, sounds and splashes here
//-----------------------------------------------------------------------------

// The sounds the other game may ask for (the bells of the bikes)
static TBOOL IsSoundAllowed( TINT a_iSound )
{
	static const TINT s_aiSounds[] = { soundbank::BARNYARD_BIKEBELL, 268, 277, 278, 279 };

	for ( TINT i = 0; i < TARRAYSIZE( s_aiSounds ); i++ )
	{
		if ( s_aiSounds[ i ] == a_iSound )
			return TTRUE;
	}

	return TFALSE;
}

// The voice bank of the partner's gender: the farm loads only the local player's (ASoundManager::
// LoadSoundBanksFromProperty skips the other one, its library unloads both). Loaded once a stay on the farm, while the
// partner is shown there
static TBOOL s_bVoiceBankTried = TFALSE;

static void UpdateVoiceBank( AUnit* a_pRemoteUnit )
{
	if ( ACoopNet::GetLocalArea() != ACoopNet::AREA_FARM )
	{
		s_bVoiceBankTried = TFALSE;
		return;
	}

	if ( s_bVoiceBankTried || !a_pRemoteUnit || !ACoopAnim::GetRemoteAvatar() || !ASoundManager::IsSingletonCreated() || s_eLocalActivity != ACoopPartner::ACTIVITY_FREE )
		return;

	const TBOOL bRemoteFemale = ACoopAnim::IsRemoteFemale();

	if ( bRemoteFemale == IsLocalFemale() )
		return;

	s_bVoiceBankTried = TTRUE;

	const TPString8 strBank( bRemoteFemale ? "am_f" : "am_m" );

	if ( ASoundManager::GetSingleton()->FindSoundBank( strBank ) )
		return;

	const TBOOL bLoaded = ASoundManager::GetSingleton()->LoadSoundBank( strBank, TFALSE, TTRUE );
	TINFO( "[cooppartner] the voices of the partner's cow (%s)%s\n", strBank.GetString(), bLoaded ? " loaded" : " couldn't be loaded" );
}

static void FireRemoteLauncher( TBOOL a_bEmpty )
{
	AUnit* pRemote = ACoopNet::GetRemotePlayerUnit();

	if ( !pRemote || !ACoopAnim::GetRemoteAvatar() || ACoopAnim::IsRemoteRiding() || !pRemote->IsA( &TGetClass( AUnitPlayer ) ) )
		return;

	// The remote cow's own launcher (the "player" type has it, except in the barn): the milk, the sound, the splashes
	// and what the milk does to this game's characters are the game's own
	AProjectileLauncher* pLauncher = TSTATICCAST( AUnitPlayer, pRemote )->GetPrimaryLauncher();

	if ( !pLauncher || !pLauncher->GetAmmo() )
		return;

	pLauncher->GetAmmo()->SetAmmo( a_bEmpty ? 0 : pLauncher->GetAmmo()->GetMaxAmmo() );
	pLauncher->Activate();
	pLauncher->Fire();

	if ( IsLogEnabled() )
		TINFO( "[cooppartner] the partner squirts%s\n", a_bEmpty ? " (out of milk)" : "" );
}

static void PlaySplash( const TVector4& a_rcPosition )
{
	if ( s_iSplashEffects[ 0 ] == -2 )
	{
		s_iSplashEffects[ 0 ] = AParticleEffectManager::FindEffect( "watersplash2a" );
		s_iSplashEffects[ 1 ] = AParticleEffectManager::FindEffect( "watersplash2b" );
		s_iSplashEffects[ 2 ] = AParticleEffectManager::FindEffect( "waterripple" );
	}

	ASoundManager::GetSingleton()->PlayCueAtPosition( IsLocalFemale() ? soundbank::AM_F_SPLASH : soundbank::AM_M_SPLASH, a_rcPosition );

	for ( TINT i = 0; i < 3; i++ )
	{
		if ( s_iSplashEffects[ i ] >= 0 )
			AParticleEffectManager::GetSingleton()->PlayEffect( s_iSplashEffects[ i ], a_rcPosition, TVector4::VEC_NEGY );
	}

	if ( ASplashManager::IsSingletonCreated() )
		ASplashManager::GetSingleton()->Spawn( a_rcPosition, TVector4::VEC_NEGY, 1.0f, ASplashManager::SPLASHTYPE_WATER );
}

//-----------------------------------------------------------------------------
// "Go to partner" (the start menu): the screen fades out, the local cow is put next
// to the partner's on the farm, the terrain comes in behind the black, it fades in
//-----------------------------------------------------------------------------

// (GOTO and s_eGoTo are declared with the local cow above)
static TGenericListener* s_pFadeListener = TNULL; // never freed: it may be linked to a fade when the program exits

static TBOOL __stdcall OnGoToFadeOver( void* a_pCaller, void* a_pOwner, void* a_pData );

static void StartGoToFade( TFLOAT a_fFrom, TFLOAT a_fTo, TFLOAT a_fTime )
{
	if ( !s_pFadeListener )
		s_pFadeListener = new TGenericListener();

	AFade* pFade = AFadeManager::GetSingleton()->StartFade( AFade::Color( TUINT8( a_fFrom * 255.0f ), 0, 0, 0 ), AFade::Color( TUINT8( a_fTo * 255.0f ), 0, 0, 0 ), a_fTime );

	if ( pFade )
		s_pFadeListener->Connect( pFade->GetFadeOverEmitter(), TNULL, OnGoToFadeOver, 0 );
}

// The movement is given back by UpdateLocalAction (ReleaseMovement)
static void EndGoTo()
{
	s_eGoTo = GOTO_NONE;
}

// Puts the local cow on the ground next to a_rcTarget (to the right of a_rcDirection, else to the left, behind or in
// front), facing a_rcDirection
static void PlaceLocalPlayerNextTo( const TVector4& a_rcTarget, const TVector4& a_rcDirection, const TCHAR* a_szWhat )
{
	AUnit* pPlayer = ACoopNet::GetLocalPlayer();

	if ( !pPlayer || !pPlayer->GetSteer() || !pPlayer->GetSteer()->IsA( &TGetClass( APlayerSteer ) ) )
		return;

	APlayerSteer*  pSteer  = TSTATICCAST( APlayerSteer, pPlayer->GetSteer() );
	const TVector4 vTarget = a_rcTarget;

	TVector4 vForward = a_rcDirection;
	vForward.y = 0.0f;
	vForward.w = 1.0f;

	if ( vForward.MagnitudeSqXZ() < 0.01f )
		vForward = TVector4::VEC_POSX;
	else
		vForward.Normalise();

	// To the right, to the left, behind or in front: the first place with ground at about the same height (the Y axis
	// points down, GetGroundHeight gives 0 for "nothing below")
	static const TFLOAT s_aAngles[] = { TMath::HALF_PI, -TMath::HALF_PI, TMath::PI, 0.0f };

	TVector4 vPosition = vTarget;
	vPosition.y -= 1.6f;
	TBOOL bGround = TFALSE;

	for ( TINT i = 0; i < TARRAYSIZE( s_aAngles ) && !bGround; i++ )
	{
		TVector4 vOffset = vForward;
		vOffset.AsVector3().RotateY( s_aAngles[ i ] );
		vOffset.Multiply( 1.8f );

		TVector4     vCandidate( vTarget.x + vOffset.x, vTarget.y, vTarget.z + vOffset.z, 1.0f );
		const TFLOAT fGround = pSteer->GetGroundHeight( vCandidate );

		if ( fGround != 0.0f && TMath::Abs( fGround - vTarget.y ) < 1.5f )
		{
			vPosition   = vCandidate;
			vPosition.y = fGround - 0.2f;
			bGround     = TTRUE;
		}
	}

	if ( pSteer->GetState() == APlayerSteer::STATE_CLIMBING )
		pSteer->EndState( APlayerSteer::STATE_CLIMBING );

	// As AGameLoader places the player at a point that isn't a goal of the network (also its last position: the
	// check for deep water casts its ray from there)
	AInteriorGoal oGoal;
	oGoal.m_vForward   = vForward;
	oGoal.m_fPositionX = vPosition.x;
	oGoal.m_fPositionY = vPosition.y;
	oGoal.m_fPositionZ = vPosition.z;

	pSteer->PlaceAtGoal( &oGoal, TTRUE, TFALSE );
	pSteer->SetSpeed( 0.0f );

	if ( pSteer->GetState() == APlayerSteer::STATE_JUMPING )
		pSteer->SetState( APlayerSteer::STATE_FALLING );

	pSteer->SetState( APlayerSteer::STATE_WALKING );

	// The camera behind the cow again
	ACameraManager* pCameraManager = ACameraManager::GetSingleton();
	pCameraManager->SetSplitscreenCameraHelper( 1 );

	if ( ACameraHelper* pHelper = pCameraManager->GetCurrentCameraHelper() )
	{
		pHelper->SetTarget( pPlayer->GetSelfProxy() );

		if ( pHelper == pCameraManager->GetSplitscreenCameraHelper( 1 ) )
			TSTATICCAST( AGTAStyleCameraHelper, pHelper )->ResetPosition( TTRUE );
	}

	TINFO( "[cooppartner] went to %s: %.2f %.2f %.2f (it is at %.2f %.2f %.2f%s)\n", a_szWhat, vPosition.x, vPosition.y, vPosition.z, vTarget.x, vTarget.y, vTarget.z, bGround ? "" : ", no ground found next to it" );
}

// Puts the local cow on the ground next to the partner's, facing the same way
static void PlaceNextToPartner()
{
	AUnit* pRemote = ACoopNet::GetRemotePlayerUnit();

	if ( !pRemote || !pRemote->GetSteer() )
		return;

	TVector4 vForward;
	pRemote->GetSteer()->CopyDirection( vForward );
	PlaceLocalPlayerNextTo( *pRemote->GetSteer()->GetPosition(), vForward, "the partner" );
}

static TBOOL __stdcall OnGoToFadeOver( void* a_pCaller, void* a_pOwner, void* a_pData )
{
	// Before the fade goes: its emitter goes with it
	s_pFadeListener->Disconnect();

	switch ( s_eGoTo )
	{
		case GOTO_FADE_OUT:
			PlaceNextToPartner();

			// Black while the terrain around comes in (it streams in around the camera)
			s_eGoTo = GOTO_HOLD;
			StartGoToFade( 1.0f, 1.0f, 0.8f );
			break;
		case GOTO_HOLD:
			s_eGoTo = GOTO_FADE_IN;
			StartGoToFade( 1.0f, 0.0f, 0.5f );
			break;
		default:
			EndGoTo();
			break;
	}

	return TTRUE;
}

static void UpdateGoTo( TFLOAT a_fDeltaTime )
{
	if ( s_eGoTo == GOTO_NONE )
		return;

	s_fGoToTime += a_fDeltaTime;

	// The fades are gone (a level change stops them) or something went wrong: the cow isn't held forever
	if ( s_fGoToTime > 6.0f || !AFadeManager::IsSingletonCreated() || !AFadeManager::GetSingleton()->HasAnyFadesInProgress( TFALSE ) )
	{
		if ( s_pFadeListener && s_pFadeListener->IsLinked() )
			s_pFadeListener->Disconnect();

		EndGoTo();
	}
}

// How the local cow gets to the partner's
enum GOTOPLAN : TINT
{
	GOTOPLAN_NONE,
	GOTOPLAN_PLACE,      // the same area: the screen fades, the cow is put next to the partner's
	GOTOPLAN_ENTER_BARN, // the partner is in the barn: in through the barn door of the farm
	GOTOPLAN_LEAVE_BARN, // the partner is on the farm: out through the door of the barn, then next to the partner
};

static TBOOL  s_bGoToAsked     = TFALSE; // "Go to partner" was chosen (the pause menu is closing)
static TFLOAT s_fGoToAskedTime = 0.0f;
static TFLOAT s_fGoToFollowUp  = 0.0f;   // seconds left to go next to the partner once the cow is out of the barn

// The cow of this game can leave what it's doing: on foot, out of the water, nothing in its hands, no interaction,
// no quest scene
static TBOOL IsLocalCowFreeToGo( AUnit* a_pPlayer )
{
	if ( !a_pPlayer || s_eGoTo != GOTO_NONE )
		return TFALSE;

	ASteer* pSteer = a_pPlayer->GetSteer();

	if ( !pSteer || !pSteer->IsA( &TGetClass( APlayerSteer ) ) || TSTATICCAST( APlayerSteer, pSteer )->IsInWater() )
		return TFALSE;

	if ( a_pPlayer->HasInteraction() || ( AQuestManager::IsSingletonCreated() && AQuestManager::GetSingleton()->IsTimerPaused() ) )
		return TFALSE;

	if ( TSTATICCAST( AAnimatedUnit, a_pPlayer )->GetAttachedUnit( TPS8D( "cb_rlocator" ), -1 ) )
		return TFALSE;

	return TTRUE;
}

// ...and this game can go to another area now: nothing runs on top of the farm or the barn (a micro game, a
// conversation, a menu), no minigame is about to start and, in a story game, the story has a safe moment (no scene
// of a quest, no fade: ACoopProgress)
static TBOOL CanLocalGameChangeArea()
{
	if ( s_eLocalActivity != ACoopPartner::ACTIVITY_FREE )
		return TFALSE;

	if ( AMiniGameManager::IsSingletonCreated() && !AMiniGameManager::GetSingleton()->GetNextMiniGameName().IsEmpty() )
		return TFALSE;

	if ( AGameLoader::IsStoryMode() && !ACoopProgress::IsSafeMoment() )
		return TFALSE;

	return TTRUE;
}

// How the local cow gets to the partner's now. TFALSE if it can't: a_rOutWhy is the line that says why
static TBOOL FindGoToPlan( GOTOPLAN& a_rOutPlan, MODSTRING& a_rOutWhy )
{
	a_rOutPlan = GOTOPLAN_NONE;
	a_rOutWhy  = MODSTRING_PARTNER_GOTO_AWAY;

	if ( ACoopNet::GetMode() == ACoopNet::MODE_OFF || !ACoopNet::IsConnected() )
		return TFALSE;

	const ACoopNet::AREA eLocal  = ACoopNet::GetLocalArea();
	const ACoopNet::AREA eRemote = ACoopNet::GetRemoteArea();

	if ( eLocal == ACoopNet::AREA_NONE || !IsLocalCowFreeToGo( ACoopNet::GetLocalPlayer() ) )
	{
		a_rOutWhy = MODSTRING_PARTNER_GOTO_BUSY;
		return TFALSE;
	}

	// The partner walks around somewhere and their game is heard (else the line at the top says what they do)
	if ( eRemote == ACoopNet::AREA_NONE || ACoopNet::GetRemoteStateAge() > 1.0f )
		return TFALSE;

	const TBOOL bLocalBarn  = eLocal != ACoopNet::AREA_FARM;
	const TBOOL bRemoteBarn = eRemote != ACoopNet::AREA_FARM;

	if ( eLocal == eRemote )
	{
		TVector4 vPosition, vDirection;
		TFLOAT   fSpeed;

		if ( !ACoopNet::GetRemotePlayerUnit() || !ACoopNet::GetRemoteState( vPosition, vDirection, fSpeed ) )
			return TFALSE;

		// In the barn only on the floor this cow is on: another floor may still be closed for it (the loft's ladder)
		if ( bLocalBarn && ACoopNet::GetBarnFloorAt( vPosition.y ) != ABarnExploreMinigameState::ms_iFloor )
		{
			a_rOutWhy = MODSTRING_PARTNER_GOTO_BARN_FLOOR;
			return TFALSE;
		}

		a_rOutPlan = GOTOPLAN_PLACE;
		return TTRUE;
	}

	// The barn by day and the barn at night are two places
	if ( bLocalBarn && bRemoteBarn )
	{
		a_rOutWhy = MODSTRING_PARTNER_GOTO_BARN_TIME;
		return TFALSE;
	}

	if ( !CanLocalGameChangeArea() )
	{
		a_rOutWhy = MODSTRING_PARTNER_GOTO_BUSY;
		return TFALSE;
	}

	if ( bLocalBarn )
	{
		a_rOutPlan = GOTOPLAN_LEAVE_BARN;
		return TTRUE;
	}

	// Into the barn when the barn door of this game would let the cow in (ABarnOpenRequirement is the requirement of
	// the door's interaction: by day, and at night once this game's story has the party)...
	if ( !ABarnOpenRequirement::IsBarnOpen( -1 ) )
	{
		a_rOutWhy = MODSTRING_PARTNER_GOTO_BARN_CLOSED;
		return TFALSE;
	}

	// ...and the barn it leads to now is the partner's (the game loads the barn of the night at night)
	if ( AGameTimeManager::GetSingleton()->IsItNight() != ( eRemote == ACoopNet::AREA_BARN_NIGHT ) )
	{
		a_rOutWhy = MODSTRING_PARTNER_GOTO_BARN_TIME;
		return TFALSE;
	}

	a_rOutPlan = GOTOPLAN_ENTER_BARN;
	return TTRUE;
}

// The game's own ways between the farm and the barn. In: what the interaction of the barn door does (its spawn
// activity sets the goal the cow comes back at, the FSM calls StartGameState). Out: the FSM's FinishGameState of the
// door inside (the time and the profile are saved, the farm comes back)
static void EnterBarn()
{
	AGameLoader::SetPlayerStartGoal( TPS8D( "barn_entrance" ) );
	AGameStateController::GetSingleton()->StartGameState( TPS8( ABarnExploreMinigameState ) );
}

static void LeaveBarn()
{
	AGameStateController::GetSingleton()->FinishGameState();
}

// The same area: the screen fades out, the cow is put next to the partner's, it fades in
static void StartGoToPlace()
{
	HoldMovement( ACoopNet::GetLocalPlayer() );

	s_eGoTo     = GOTO_FADE_OUT;
	s_fGoToTime = 0.0f;
	StartGoToFade( 0.0f, 1.0f, 0.5f );

	TINFO( "[cooppartner] going to the partner\n" );
}

static void UpdateGoToRequest( TFLOAT a_fDeltaTime )
{
	s_fNoteTime = TMath::Max( s_fNoteTime - a_fDeltaTime, 0.0f );

	// Out of the barn for the partner: next to them once the farm is there and they're still on it
	if ( s_fGoToFollowUp > 0.0f && !s_bGoToAsked )
	{
		s_fGoToFollowUp -= a_fDeltaTime;

		if ( ACoopNet::GetLocalArea() == ACoopNet::AREA_FARM && s_eLocalActivity == ACoopPartner::ACTIVITY_FREE && AFadeManager::IsSingletonCreated() )
		{
			GOTOPLAN  ePlan;
			MODSTRING eWhy;

			if ( FindGoToPlan( ePlan, eWhy ) && ePlan == GOTOPLAN_PLACE )
			{
				s_fGoToFollowUp = 0.0f;
				StartGoToPlace();
			}
			else if ( ePlan != GOTOPLAN_NONE )
			{
				// The partner went somewhere else meanwhile: the cow stays at the door
				s_fGoToFollowUp = 0.0f;
			}
		}
	}

	if ( !s_bGoToAsked )
		return;

	s_fGoToAskedTime += a_fDeltaTime;

	GOTOPLAN    ePlan;
	MODSTRING   eWhy;
	const TBOOL bCan = FindGoToPlan( ePlan, eWhy ) && ( ePlan != GOTOPLAN_PLACE || AFadeManager::IsSingletonCreated() );

	// Busy here: the pause menu it was chosen in is still closing, or the screen still fades. Looked at again for a
	// moment
	if ( !bCan && eWhy == MODSTRING_PARTNER_GOTO_BUSY && s_fGoToAskedTime < 1.0f )
		return;

	s_bGoToAsked    = TFALSE;
	s_fGoToFollowUp = 0.0f;

	if ( !bCan )
	{
		// The line at the top says why not for a few seconds
		s_eNote     = eWhy;
		s_fNoteTime = 4.0f;
		TINFO( "[cooppartner] can't go to the partner now (%s)\n", AModStrings::GetId( eWhy ) );
		return;
	}

	// An emote ends here, and nothing of this module holds the cow through a loading
	EndAction();

	switch ( ePlan )
	{
		case GOTOPLAN_PLACE:
			StartGoToPlace();
			break;
		case GOTOPLAN_ENTER_BARN:
			ReleaseMovement();
			TINFO( "[cooppartner] going to the partner in the barn%s: through the barn door\n", AGameTimeManager::GetSingleton()->IsItNight() ? " (at night)" : "" );
			EnterBarn();
			break;
		case GOTOPLAN_LEAVE_BARN:
			ReleaseMovement();
			s_fGoToFollowUp = 30.0f;
			TINFO( "[cooppartner] going to the partner on the farm: out of the barn first\n" );
			LeaveBarn();
			break;
		default:
			break;
	}
}

//-----------------------------------------------------------------------------
// The events of the other game
//-----------------------------------------------------------------------------

static void ApplyEvent( const ActionEvent& a_rcEvent )
{
	AUnit* pLocal = ACoopNet::GetLocalPlayer();

	switch ( a_rcEvent.uiType )
	{
		case EVENTTYPE_SQUIRT:
			FireRemoteLauncher( a_rcEvent.uiParam != 0 );
			break;
		case EVENTTYPE_SOUND:
		{
			const TINT iSound = TINT( a_rcEvent.afValues[ 0 ] );
			TVector4   vPosition;

			if ( IsSoundAllowed( iSound ) && GetPartnerPosition( vPosition ) )
				ASoundManager::GetSingleton()->PlayCueAtPosition( iSound, vPosition );

			break;
		}
		case EVENTTYPE_SPLASH:
			if ( ACoopNet::GetRemotePlayerUnit() )
				PlaySplash( TVector4( a_rcEvent.afValues[ 0 ], a_rcEvent.afValues[ 1 ], a_rcEvent.afValues[ 2 ], 1.0f ) );

			break;
		case EVENTTYPE_EMOTE:
			if ( a_rcEvent.uiParam < EMOTE_NUMOF && ACoopNet::GetRemotePlayerUnit() && ACoopAnim::GetRemoteAvatar() )
			{
				const EmoteDef& rcEmote = s_aEmotes[ a_rcEvent.uiParam ];
				PlayVoice( ACoopNet::GetRemotePlayerUnit(), ACoopAnim::IsRemoteFemale(), rcEmote.eMaleVoice, rcEmote.eFemaleVoice );
			}

			break;
		case EVENTTYPE_HIT:
		{
			if ( a_rcEvent.uiParam >= HITKIND_NUMOF || !pLocal )
				break;

			// Near here too (the two games see the two cows a little apart)
			const TVector4& rcLocal = *pLocal->GetSteer()->GetPosition();
			TVector4        vFrom( a_rcEvent.afValues[ 0 ], rcLocal.y, a_rcEvent.afValues[ 1 ], 1.0f );

			if ( TVector4::DistanceSq( vFrom, rcLocal ) < 3.5f * 3.5f && ACoopNet::GetRemotePlayerUnit() )
				StartHit( pLocal, HITKIND( a_rcEvent.uiParam ), vFrom );
			else
				TINFO( "[cooppartner] the partner's %s doesn't reach this cow here\n", s_aszHitNames[ a_rcEvent.uiParam ] );

			break;
		}
		case EVENTTYPE_HIGHFIVE:
		{
			// (Not in a micro game: the answer stops the cow for a moment, and the partner started it)
			if ( !pLocal || !ACoopNet::GetRemotePlayerUnit() || s_eLocalActivity != ACoopPartner::ACTIVITY_FREE )
				break;

			const TVector4& rcLocal = *pLocal->GetSteer()->GetPosition();
			TVector4        vFrom( a_rcEvent.afValues[ 0 ], rcLocal.y, a_rcEvent.afValues[ 1 ], 1.0f );

			if ( TVector4::DistanceSq( vFrom, rcLocal ) > HIGHFIVE_REACH * HIGHFIVE_REACH * 1.5f )
				break;

			// Only a cow that stands answers: one that runs past isn't stopped
			if ( pLocal->GetSteer()->GetKinematics() && TMath::Abs( pLocal->GetSteer()->GetKinematics()->GetSpeed() ) > 1.5f )
			{
				TINFO( "[cooppartner] the partner's high five isn't answered: this cow is on the move\n" );
				break;
			}

			// The cow turns to the partner and answers
			if ( pLocal->GetSteer()->IsA( &TGetClass( APlayerSteer ) ) )
			{
				TVector4 vFacing( vFrom.x - rcLocal.x, 0.0f, vFrom.z - rcLocal.z, 0.0f );

				if ( vFacing.MagnitudeSqXZ() > 0.0001f && !s_oAction.bActive )
				{
					APlayerSteer* pBusySteer;

					if ( !GetBusyReason( pLocal, TFALSE, &pBusySteer ) )
					{
						vFacing.Normalise();
						pBusySteer->GetKinematics()->SetFacing( vFacing );
						pBusySteer->GetKinematics()->SetDesiredFacing( vFacing );
					}
				}
			}

			if ( StartEmote( pLocal, EMOTE_HIGHFIVE, TTRUE ) )
				PlayVoice( pLocal, IsLocalFemale(), soundbank::AM_M_CHEER, soundbank::AM_F_CHEER );

			break;
		}
#ifndef TOSHI_FINAL
		case EVENTTYPE_SHOT:
			ParseShots();
			TakeShot( "join", a_rcEvent.uiParam );
			break;
#endif
		default:
			break;
	}
}

// CHANNEL_ACTION
static void OnActionData( const void* a_pData, TINT a_iSize )
{
	const TINT iHeader = TINT( sizeof( ActionMessage ) - sizeof( ActionMessage::aEvents ) );

	if ( a_iSize < iHeader || a_iSize > TINT( sizeof( ActionMessage ) ) )
		return;

	ActionMessage oMessage;
	TUtil::MemCopy( &oMessage, a_pData, a_iSize );

	if ( oMessage.uiMagic != ACTION_MAGIC || oMessage.uiNumEvents > MAX_EVENTS || a_iSize != iHeader + oMessage.uiNumEvents * TINT( sizeof( ActionEvent ) ) )
		return;

	// Another run of the other game: its ids start again
	if ( oMessage.uiNonce != s_uiRemoteNonce )
	{
		s_uiRemoteNonce = oMessage.uiNonce;
		s_bAppliedAny   = TFALSE;
	}

	// The oldest first, each one once
	for ( TINT i = 0; i < oMessage.uiNumEvents; i++ )
	{
		const ActionEvent& rcEvent = oMessage.aEvents[ i ];

		if ( s_bAppliedAny && TINT16( rcEvent.uiId - s_uiLastAppliedId ) <= 0 )
			continue;

		TBOOL bValid = rcEvent.uiType != EVENTTYPE_NONE && rcEvent.uiType < EVENTTYPE_NUMOF;

		for ( TINT k = 0; k < 4 && bValid; k++ )
			bValid = std::isfinite( rcEvent.afValues[ k ] ) && TMath::Abs( rcEvent.afValues[ k ] ) < 100000.0f;

		s_uiLastAppliedId = rcEvent.uiId;
		s_bAppliedAny     = TTRUE;

		if ( bValid )
			ApplyEvent( rcEvent );
	}
}

//-----------------------------------------------------------------------------
// ACoopPartner
//-----------------------------------------------------------------------------

#ifndef TOSHI_FINAL
// Development: "-mpdo <what>@<seconds>,..." does things the seconds after the player is first in an area: emote<1-8>,
// goto (Go to partner), squirt (the local launcher), sprint<seconds> is done by the keys
static void UpdateDevActions( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer )
{
	struct DevAction
	{
		TCHAR  szWhat[ 16 ];
		TFLOAT fAt;
		TBOOL  bDone;
	};

	static DevAction s_aActions[ 16 ];
	static TINT      s_iNumActions = -1;
	static TFLOAT    s_fTime       = -1.0f;

	if ( s_iNumActions < 0 )
	{
		s_iNumActions = 0;

		const TCHAR* szArg = TStringManager::String8FindString( AGameLoader::Dev_GetCommandLine(), "-mpdo " );

		if ( szArg )
		{
			szArg += TStringManager::String8Length( "-mpdo " );

			while ( *szArg != '\0' && *szArg != ' ' && s_iNumActions < TARRAYSIZE( s_aActions ) )
			{
				DevAction& rAction = s_aActions[ s_iNumActions ];
				TINT       iLength = 0;

				while ( *szArg != '\0' && *szArg != '@' && *szArg != ' ' && iLength < TARRAYSIZE( rAction.szWhat ) - 1 )
					rAction.szWhat[ iLength++ ] = *szArg++;

				rAction.szWhat[ iLength ] = '\0';

				if ( *szArg != '@' )
					break;

				TCHAR* szEnd;
				rAction.fAt   = TFLOAT( strtod( szArg + 1, &szEnd ) );
				rAction.bDone = TFALSE;
				szArg         = szEnd;
				s_iNumActions++;

				if ( *szArg == ',' )
					szArg++;
			}

			TINFO( "[cooppartner] development actions: %d\n", s_iNumActions );
		}
	}

	if ( s_iNumActions == 0 )
		return;

	if ( s_fTime < 0.0f )
	{
		if ( !a_pLocalPlayer )
			return;

		s_fTime = 0.0f;
	}

	s_fTime += a_fDeltaTime;

	for ( TINT i = 0; i < s_iNumActions; i++ )
	{
		DevAction& rAction = s_aActions[ i ];

		if ( rAction.bDone || s_fTime < rAction.fAt )
			continue;

		rAction.bDone = TTRUE;
		TINFO( "[cooppartner] development action %s at %.1f\n", rAction.szWhat, s_fTime );

		if ( TStringManager::String8Compare( rAction.szWhat, "goto" ) == 0 )
		{
			ACoopPartner::GoToPartner();
		}
		else if ( TStringManager::String8Compare( rAction.szWhat, "emote", 5 ) == 0 )
		{
			const TINT iEmote = atoi( rAction.szWhat + 5 ) - 1;

			if ( iEmote >= 0 && iEmote < EMOTE_NUMOF )
				StartEmote( a_pLocalPlayer, iEmote, TFALSE );
		}
		else if ( TStringManager::String8Compare( rAction.szWhat, "squirt" ) == 0 && a_pLocalPlayer && a_pLocalPlayer->IsA( &TGetClass( AUnitPlayer ) ) )
		{
			if ( AProjectileLauncher* pLauncher = TSTATICCAST( AUnitPlayer, a_pLocalPlayer )->GetPrimaryLauncher() )
				pLauncher->Fire();
		}
		else if ( TStringManager::String8Compare( rAction.szWhat, "bike" ) == 0 && a_pLocalPlayer && ABikeManager::IsSingletonCreated() )
		{
			// Gets on the closest bike (the debug menu's "Get on the closest bike") from next to it: from afar the game
			// refuses it when something is between them
			AUnit* pBike = TNULL;
			TINT   iType = 0;
			ABikeManager::GetSingleton()->FindClosestBike( *a_pLocalPlayer->GetSteer()->GetPosition(), pBike, iType );

			if ( pBike && pBike->GetSteer() )
			{
				TVector4 vForward;
				pBike->GetSteer()->CopyDirection( vForward );
				PlaceLocalPlayerNextTo( *pBike->GetSteer()->GetPosition(), vForward, "the closest bike" );
				ABikeManager::GetSingleton()->StartupPlayerBike();
				TINFO( "[cooppartner] getting on a bike of type %d: state %d\n", iType, TINT( ABikeManager::GetSingleton()->GetState() ) );
			}
			else
			{
				TINFO( "[cooppartner] no bike here\n" );
			}
		}
		else if ( TStringManager::String8Compare( rAction.szWhat, "bell" ) == 0 && a_pLocalPlayer && a_pLocalPlayer->GetSteer() &&
		          a_pLocalPlayer->GetSteer()->IsA( &TGetClass( ABikeSteer ) ) )
		{
			TSTATICCAST( ABikeSteer, a_pLocalPlayer->GetSteer() )->PlayHorn();
		}
		else if ( TStringManager::String8Compare( rAction.szWhat, "barn" ) == 0 && ACoopNet::GetLocalArea() == ACoopNet::AREA_FARM )
		{
			// Into the barn, as "Go to partner" goes in (the barn of the day by day)
			EnterBarn();
		}
		else if ( TStringManager::String8Compare( rAction.szWhat, "leave" ) == 0 && ACoopNet::GetLocalArea() != ACoopNet::AREA_FARM && ACoopNet::GetLocalArea() != ACoopNet::AREA_NONE )
		{
			// Out of the barn, as "Go to partner" goes out
			LeaveBarn();
		}
		else if ( TStringManager::String8Compare( rAction.szWhat, "carry", 5 ) == 0 && a_pLocalPlayer )
		{
			// Apples in the hand of the cow, put there by its inventory: the "Hand" makes the carried unit and
			// ACarryableUnitHelper::Carry attaches it to cb_rlocator (APlayerInventoryCallbacks::OnAdd). "carry3": three,
			// a bunch
			AInventoryGroupComponent* pInventory = TSTATICCAST( AInventoryGroupComponent, a_pLocalPlayer->FindComponent( &TGetClass( AInventoryGroupComponent ) ) );
			const TPString8           strHand    = TPS8D( "Hand" );
			const TINT                iCount     = TMath::Max( atoi( rAction.szWhat + 5 ), 1 );

			if ( pInventory && pInventory->GetCategory( strHand ) )
			{
				const TBOOL bAdded = pInventory->AddItems( TPS8D( "apple" ), iCount, strHand );
				TINFO( "[cooppartner] %d apple(s) for the hand of the cow: %s\n", iCount, bAdded ? "added" : "not added" );
			}
			else
			{
				TINFO( "[cooppartner] the cow has no hand inventory here\n" );
			}
		}
		else if ( TStringManager::String8Compare( rAction.szWhat, "glasses" ) == 0 && a_pLocalPlayer )
		{
			// A model on the head of the cow, as the sunglasses item puts its own
			AAnimatableObject*     pCow  = GetModel( a_pLocalPlayer );
			AAnimatableObjectType* pType = AAnimatableObjectManager::GetSingleton()->FindType( TPS8D( "sunglasses1" ) );

			if ( pCow && pType )
			{
				AAnimatableObject* pGlasses = AAnimatableObjectManager::GetSingleton()->CreateAnimatableObject( pType, a_pLocalPlayer, AAnimatableObject::CREATE_FLAGS_NO_ANIM_CONTROLLER );
				pGlasses->SetSkeletonUpdating( TTRUE, TTRUE );
				AAnimatableObjectManager::GetSingleton()->AttachObject( pGlasses, pCow, TPString8(), TPS8( c_head ), TVector4::VEC_ZERO, TQuaternion::IDENTITY, 1.0f );
			}
			else
			{
				TINFO( "[cooppartner] no sunglasses model here\n" );
			}
		}
	}
}
#endif

static struct ACoopPartnerRegistration
{
	ACoopPartnerRegistration()
	{
		ACoopNet::SetDataHandler( ACoopNet::CHANNEL_PARTNER, OnInfoData );
		ACoopNet::SetDataHandler( ACoopNet::CHANNEL_ACTION, OnActionData );
	}
} s_oRegistration;

void ACoopPartner::Update( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer, AUnit* a_pRemoteUnit )
{
	EnsureHud();

	s_eLocalActivity     = FindLocalActivity();
	s_bEmotesInMicroGame = FindEmotesInMicroGame();

	if ( ACoopNet::IsConnected() )
	{
		SendInfo( a_fDeltaTime );
		SendEvents( a_fDeltaTime );
	}
	else
	{
		s_oPartner.bKnown = TFALSE;
	}

#ifndef TOSHI_FINAL
	UpdateShots( a_fDeltaTime );
	UpdateDevActions( a_fDeltaTime, a_pLocalPlayer );
#endif

	UpdateLocalAction( a_fDeltaTime, a_pLocalPlayer );
	UpdateEmoteKeys( a_pLocalPlayer );
	UpdateVoiceBank( a_pRemoteUnit );
	UpdateBumps( a_fDeltaTime, a_pLocalPlayer, a_pRemoteUnit );
	UpdateGoToRequest( a_fDeltaTime );
	UpdateGoTo( a_fDeltaTime );

	// The remote cow's launcher (ASimState only updates the local player's): its milk streams
	if ( a_pRemoteUnit && a_pRemoteUnit->IsA( &TGetClass( AUnitPlayer ) ) )
	{
		if ( AProjectileLauncher* pLauncher = TSTATICCAST( AUnitPlayer, a_pRemoteUnit )->GetPrimaryLauncher() )
			pLauncher->Update( a_fDeltaTime );
	}

	UpdateStatus( a_pRemoteUnit );
	UpdateTag( a_fDeltaTime );
	UpdateIcon( a_pRemoteUnit );
}

void ACoopPartner::OnRemoteUnitGone()
{
	RemoveIcon();
	s_bTagShown = TFALSE;
}

void ACoopPartner::OnSessionEnded()
{
	OnRemoteUnitGone();
	EndAction();
	EndGoTo();
	ReleaseMovement();

	s_bGoToAsked    = TFALSE;
	s_fGoToFollowUp = 0.0f;
	s_fNoteTime     = 0.0f;

	s_oPartner.bKnown = TFALSE;
	s_wszStatus[ 0 ]  = L'\0';
}

ACoopPartner::ACTIVITY ACoopPartner::GetPartnerActivity()
{
	return ( s_oPartner.bKnown && ACoopNet::IsConnected() ) ? ACTIVITY( s_oPartner.oInfo.uiActivity ) : ACTIVITY_UNKNOWN;
}

TINT ACoopPartner::GetPartnerMiniGame( TINT* a_pOutVariant )
{
	const TBOOL bPlaying = GetPartnerActivity() == ACTIVITY_MINIGAME;

	if ( a_pOutVariant )
		*a_pOutVariant = bPlaying ? s_oPartner.oInfo.uiVariant : -1;

	return bPlaying ? s_oPartner.oInfo.iMiniGame : -1;
}

const TWCHAR* ACoopPartner::GetPartnerName()
{
	if ( s_oPartner.bKnown && s_oPartner.oInfo.wszName[ 0 ] != L'\0' )
		return s_oPartner.oInfo.wszName;

	// A text of the mod: "Partner" in the language of the game
	return AModStrings::Get( MODSTRING_PARTNER_NAME );
}

ACoopPartner::ACTIVITY ACoopPartner::GetLocalActivity()
{
	return s_eLocalActivity;
}

void ACoopPartner::OnSquirt( AMilkSquirtProjectileLauncher* a_pLauncher, TBOOL a_bCanFire )
{
	if ( !a_pLauncher || ACoopNet::GetMode() == ACoopNet::MODE_OFF || !ACoopNet::IsConnected() )
		return;

	AUnit* pOwner = a_pLauncher->GetOwnerUnit();

	if ( !pOwner || pOwner != ACoopNet::GetLocalPlayer() )
		return;

	QueueEvent( EVENTTYPE_SQUIRT, a_bCanFire ? 0 : 1 );

	if ( IsLogEnabled() )
		TINFO( "[cooppartner] squirt sent to the partner%s\n", a_bCanFire ? "" : " (out of milk)" );
}

void ACoopPartner::OnLocalSound( AUnit* a_pUnit, TINT a_iSound )
{
	if ( ACoopNet::GetMode() == ACoopNet::MODE_OFF || !ACoopNet::IsConnected() || !IsSoundAllowed( a_iSound ) )
		return;

	if ( a_pUnit && a_pUnit == ACoopNet::GetLocalPlayer() )
		QueueEvent( EVENTTYPE_SOUND, 0, TFLOAT( a_iSound ) );
}

void ACoopPartner::OnLocalSplash( AUnit* a_pUnit, const TVector4& a_rcPosition )
{
	if ( ACoopNet::GetMode() == ACoopNet::MODE_OFF || !ACoopNet::IsConnected() )
		return;

	if ( a_pUnit && a_pUnit == ACoopNet::GetLocalPlayer() )
		QueueEvent( EVENTTYPE_SPLASH, 0, a_rcPosition.x, a_rcPosition.y, a_rcPosition.z );
}

TBOOL ACoopPartner::IsGoToPartnerOffered()
{
	// Where the cow walks around (the pause menu is there in the minigames too)
	return ACoopNet::GetMode() != ACoopNet::MODE_OFF && ACoopNet::IsConnected() && ACoopNet::GetLocalArea() != ACoopNet::AREA_NONE && s_eGoTo == GOTO_NONE;
}

void ACoopPartner::GoToPartner()
{
	if ( ACoopNet::GetMode() == ACoopNet::MODE_OFF )
		return;

	// Looked at by Update once the pause menu is gone
	s_bGoToAsked     = TTRUE;
	s_fGoToAskedTime = 0.0f;
}

TBOOL ACoopPartner::IsEmoteChordHeld()
{
	return s_bEmoteChordHeld && ACoopNet::GetMode() != ACoopNet::MODE_OFF;
}
