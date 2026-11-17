#include "pch.h"
#include "ACoopProgress.h"
// NOTE: before APlayerManager.h, whose APlayerSet.h defines MAX_NUM_PLAYERS (the element has a constant of that name)
#include "Quests/AGameEventQuestElement.h"
#include "ACoopNet.h"
#include "APlayerManager.h"
#include "APlayerProfileManager.h"
#include "Quests/AQuestManager.h"
#include "Quests/AQuestInstance.h"
#include "Quests/AQuest.h"
#include "Quests/AQuestElement.h"
#include "Quests/ADummyQuestElement.h"
#include "Quests/ALocateQuestElement.h"
#include "Helpers/AGameLoader.h"
#include "Helpers/AGameTimeManager.h"
#include "Helpers/APlayerProgress.h"
#include "Units/AInventoryComponentInterface.h"
#include "Helpers/AReplayManager.h"
#include "GameInterface/AGameStateController.h"
#include "GameInterface/ASimState.h"
#include "GameInterface/AMiniGameInstructionState.h"
#include "MiniGame/AMiniGameManager.h"
#include "MiniGame/AMiniGameState.h"
#include "MiniGame/AMiniGame.h"
#include "MiniGame/ABarnExploreMinigameState.h"
#include "Input/AInputMapManager.h"
#include "Tasks/ARootTask.h"
#include "Units/AUnit.h"
#include "Units/AUnitManager.h"
#include "Units/AUnitNameManager.h"
#include "Units/AUnitRef.h"
#include "Units/AAnimalPopulationManager.h"
#include "Units/AInteractionQuestEvent.h"
#include "Units/AInteractionEventManager.h"
#include "Units/AInteractionManager.h"
#include "Units/ANPCBehaviourManager.h"
#include "Units/APlayerControlComponent.h"
#include "FSM/AMiscFunctions.h"
#include "ACheatActivator.h"
#include "HUD/AHudManager.h"
#include "HUD/AHudElement.h"
#include "HUD/AItemCountHudElement.h"
#include "HUD/ASimMiniMap.h"
#include "GUI/AGUI2.h"
#include "GUI/AGUI2TextBox.h"
#include "GUI/AGUI2Style.h"
#include "Input/AInputHandler.h"
#include "Locale/AModStrings.h"
#include "Mod/AModEvents.h"

#include <GUI/T2GUIFontManager.h>
#include <Input/TInputDeviceKeyboard.h>
#include <Input/TInputDeviceController.h>
#include <PrimeDebug.h>

#ifdef PRIMEDEBUG_DEV
#  include <Console/PConsole.h>
#endif

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TPSTRING8_DECLARE( ASimState );
TPSTRING8_DECLARE( ABarnExploreMinigameState );

//-----------------------------------------------------------------------------
// Messages. Both games are the same build on the same kind of CPU, so the
// structs are sent as they are.
//-----------------------------------------------------------------------------

static constexpr TUINT32 PROGRESS_MAGIC = 0x31505942; // "BYP1"
static constexpr TUINT32 SKIP_MAGIC     = 0x31535942; // "BYS1"
static constexpr TUINT32 SAVE_MAGIC     = 0x31565342; // "BSV1"

// What ProgressMessage::uiFlags tells about the sender
enum PROGRESSFLAGS : TUINT8
{
	PROGRESSFLAGS_IN_STORY = 1 << 0, // a story game is going on, the progress is valid
	PROGRESSFLAGS_LIVE     = 1 << 1, // the farm or the barn is loaded: the progress is from this frame
	PROGRESSFLAGS_BARN     = 1 << 2, // in the barn
	PROGRESSFLAGS_MINIGAME = 1 << 3, // in a minigame
	PROGRESSFLAGS_SCENE    = 1 << 4, // a scripted sequence is loaded or coming
	PROGRESSFLAGS_COMPLETE = 1 << 5, // the story is over
	PROGRESSFLAGS_SAFE     = 1 << 6, // a skip could be applied there right now (ACoopProgress::IsSafeMoment)
};

static constexpr TINT MAX_SKIP_QUESTS = 16;

#pragma pack( push, 1 )
// CHANNEL_PROGRESS: where the sender's story is
struct ProgressMessage
{
	TUINT32 uiMagic;
	TUINT32 uiDataChecksum;  // of the quest data of the sender (PrepareQuestData)
	TUINT16 uiSequence;      // one more for every message
	TUINT8  uiFlags;         // PROGRESSFLAGS
	TUINT8  uiAct;           // 1-3, 4 once the story is over
	TUINT8  uiTier;          // tier of the act, 1-based
	TUINT8  uiNumTiers;      // tiers of the whole story
	TINT8   iAssignedQuest;  // index in AQuestManager, -1 if none
	TUINT8  uiAssignedState; // AQuestInstance::STATE
	TUINT16 uiAssignedDone;  // a bit for every element of the assigned quest that's done
	TUINT8  uiDayPhase;      // AGameTimeManager::DAYPHASE
	TUINT8  uiDay;           // day of the month
	TFLOAT  fDayTime;        // seconds of game time since midnight

	// The completed mask of every tier (AQuestManager::QuestTier)
	TUINT8 auiCompleted[ AQuestManager::MAX_NUM_TIERS ];
};

// CHANNEL_SKIP: the sender skipped these quests. Sent a few times, the copies have the same uiId
struct SkipMessage
{
	TUINT32 uiMagic;
	TUINT16 uiId;
	TUINT8  uiAct; // the sender's story once they were skipped
	TUINT8  uiTier;
	TUINT8  uiNumQuests;
	TUINT8  auiQuests[ MAX_SKIP_QUESTS ];
};

// What SaveMessage::uiKind says
enum SAVEKIND : TUINT8
{
	SAVEKIND_SAVED      = 1, // the sender saved its game: save yours too (uiId: its save)
	SAVEKIND_SAVED_ALSO = 2, // the sender saved too because of your save uiId
};

// CHANNEL_PROGRESS (told apart by its magic and size): saves. Sent a few times, the copies have the same uiId and uiKind
struct SaveMessage
{
	TUINT32 uiMagic;
	TUINT16 uiId;
	TUINT8  uiKind;   // SAVEKIND
	TUINT8  uiAct;    // the sender's story in its save
	TUINT8  uiTier;
	TUINT8  uiNumDone; // completed quests
};
#pragma pack( pop )

//-----------------------------------------------------------------------------
// The quest data, loaded once by AQuestManager::Create
//-----------------------------------------------------------------------------

static TUINT32 s_uiDataChecksum = 0; // 0 until the quests are loaded
static TINT    s_iNumQuests     = 0;
static TINT    s_iNumTiers      = 0;
static TINT    s_aiQuestTier[ ACoopProgress::MAX_QUESTS ];    // absolute tier of every quest, 0 if the story has no tier for it
static TINT    s_aiQuestSlot[ ACoopProgress::MAX_QUESTS ];    // its bit in the completed mask of the tier
static TINT    s_iFinalTier = 0x7fffffff;                     // the first tier of the end of the story

// The quests whose skip does more than ending them (Data/Interactions: what their scripts do otherwise, see
// PrepareAssignedSkip and CompleteQuest). Pip Hitch-Hiker (Pip on the player's head), Pig Apple Pie (the pie) and Ben
// Tomato Launcher (its parts) are undone by their "_uninit" scripts like any quest going on.
// Sleepy Cows: hitting the sleeping cows locks the bike (AMiscFunctions::PlayerCanGetOnBike) and the last one the
// player's movement, the quest's end scene (sleepy_cows_complete_seq) frees them
static const TCHAR* s_szSleepyCows = "Sleepy Cows";
// Root (Night) Barn Detailing: its "_uninit" script takes the bought barn items back and the 500 pick ups Root gave
// (it's for a quest given up): not run, only Joey's question mark goes
static const TCHAR* s_szBarnDetailing = "Root (Night) Barn Detailing";

// Its tier and the ones after it are the end of the story (the coyote showdown, the hero, the end scene)
static const TCHAR* s_szFirstFinalQuest = "Miles Coyote Tipping";

// FNV-1a
static void MixChecksum( TUINT32& a_ruiHash, const void* a_pData, TINT a_iSize )
{
	const TUINT8* pBytes = TSTATICCAST( const TUINT8, a_pData );

	for ( TINT i = 0; i < a_iSize; i++ )
	{
		a_ruiHash ^= pBytes[ i ];
		a_ruiHash *= 16777619u;
	}
}

static TBOOL s_bLog = TFALSE; // "-cooplog"

// TTRUE once the quest data is known
static TBOOL PrepareQuestData()
{
	if ( s_uiDataChecksum != 0 )
		return TTRUE;

	if ( !AQuestManager::IsSingletonCreated() )
		return TFALSE;

	AQuestManager* pQuestManager = AQuestManager::GetSingleton();

	if ( pQuestManager->GetNumQuests() <= 0 || pQuestManager->GetNumTiers() <= 0 )
		return TFALSE;

	if ( pQuestManager->GetNumQuests() > ACoopProgress::MAX_QUESTS || pQuestManager->GetNumTiers() > AQuestManager::MAX_NUM_TIERS )
	{
		static TBOOL s_bTold = TFALSE;

		if ( !s_bTold )
		{
			s_bTold = TTRUE;
			TERROR( "[coopstory] %d quests in %d tiers, at most %d quests can be followed: the stories aren't shared\n", pQuestManager->GetNumQuests(), pQuestManager->GetNumTiers(), ACoopProgress::MAX_QUESTS );
		}

		return TFALSE;
	}

	TUINT32 uiHash = 2166136261u;
	s_iNumQuests   = pQuestManager->GetNumQuests();
	s_iNumTiers    = pQuestManager->GetNumTiers();

	MixChecksum( uiHash, &s_iNumQuests, sizeof( s_iNumQuests ) );
	MixChecksum( uiHash, &s_iNumTiers, sizeof( s_iNumTiers ) );

	for ( TINT i = 0; i < s_iNumQuests; i++ )
	{
		const TCHAR* szName = pQuestManager->GetQuest( i )->GetName().GetString();
		MixChecksum( uiHash, szName, TStringManager::String8Length( szName ) + 1 );

		s_aiQuestTier[ i ] = 0;
		s_aiQuestSlot[ i ] = 0;
	}

	for ( TINT iAct = 0; iAct < 3; iAct++ )
	{
		const TINT iNumTiersInAct = pQuestManager->GetNumTiersInAct( iAct );
		MixChecksum( uiHash, &iNumTiersInAct, sizeof( iNumTiersInAct ) );
	}

	for ( TINT iTier = 1; iTier <= s_iNumTiers; iTier++ )
	{
		const AQuestManager::QuestTier* pTier = pQuestManager->GetQuestTier( iTier );
		MixChecksum( uiHash, &pTier->uiNumQuests, sizeof( pTier->uiNumQuests ) );
		MixChecksum( uiHash, pTier->auiQuests, pTier->uiNumQuests );

		for ( TINT k = 0; k < pTier->uiNumQuests; k++ )
		{
			if ( pTier->auiQuests[ k ] < s_iNumQuests )
			{
				s_aiQuestTier[ pTier->auiQuests[ k ] ] = iTier;
				s_aiQuestSlot[ pTier->auiQuests[ k ] ] = k;
			}
		}
	}

	TINT iAct  = 0;
	TINT iTier = 0;

	if ( pQuestManager->FindQuestTier( s_szFirstFinalQuest, &iAct, &iTier ) )
		s_iFinalTier = pQuestManager->GetAbsQuestTier( iAct, iTier );
	else
		s_iFinalTier = TMath::Max( s_iNumTiers - 2, 1 );

	s_uiDataChecksum = ( uiHash != 0 ) ? uiHash : 1;

	if ( s_bLog )
		TINFO( "[coopstory] quest data: %d quests, %d tiers (%d/%d/%d), checksum %08x; the end of the story starts at tier %d\n", s_iNumQuests, s_iNumTiers, pQuestManager->GetNumTiersInAct( 0 ), pQuestManager->GetNumTiersInAct( 1 ), pQuestManager->GetNumTiersInAct( 2 ), s_uiDataChecksum, s_iFinalTier );

	return TTRUE;
}

static const TCHAR* GetQuestName( TINT a_iQuest )
{
	if ( a_iQuest < 0 || a_iQuest >= s_iNumQuests || !AQuestManager::IsSingletonCreated() )
		return "?";

	return AQuestManager::GetSingleton()->GetQuest( a_iQuest )->GetName().GetString();
}

// A name of the game data (a quest's) for a line of the players: the %s of their texts take wide strings. One name at
// a time, the buffer is static
static const TWCHAR* GetWideName( const TCHAR* a_szName )
{
	static TWCHAR s_wszName[ 96 ];

	TINT i = 0;

	for ( ; a_szName[ i ] != '\0' && i < TINT( TARRAYSIZE( s_wszName ) ) - 1; i++ )
		s_wszName[ i ] = TWCHAR( TUINT8( a_szName[ i ] ) );

	s_wszName[ i ] = L'\0';
	return s_wszName;
}

static const TWCHAR* GetQuestNameWide( TINT a_iQuest )
{
	return GetWideName( GetQuestName( a_iQuest ) );
}

// TTRUE if a_rcProgress has the quest a_iQuest completed
static TBOOL IsQuestDoneIn( const ProgressMessage& a_rcProgress, TINT a_iQuest )
{
	const TINT iTier = s_aiQuestTier[ a_iQuest ];
	return iTier > 0 && ( a_rcProgress.auiCompleted[ iTier - 1 ] & ( 1 << s_aiQuestSlot[ a_iQuest ] ) ) != 0;
}

// The current tier of a_rcProgress counted over all the acts
static TINT GetAbsTier( const ProgressMessage& a_rcProgress )
{
	return AQuestManager::GetSingleton()->GetAbsQuestTier( a_rcProgress.uiAct, a_rcProgress.uiTier );
}

static TINT CountDone( const ProgressMessage& a_rcProgress )
{
	TINT iNumDone = 0;

	for ( TINT i = 0; i < s_iNumQuests; i++ )
	{
		if ( IsQuestDoneIn( a_rcProgress, i ) )
			iNumDone++;
	}

	return iNumDone;
}

//-----------------------------------------------------------------------------
// The two stories
//-----------------------------------------------------------------------------

// This game's progress. AQuestManager only has the story while the farm or the barn is loaded (the minigames and the
// scenes reset it, the farm brings it back from the quick save), so it's read there and kept for the rest of the time
static ProgressMessage s_oLocal;
static TBOOL           s_bLocalValid = TFALSE;

// The other game's progress
static ProgressMessage s_oRemote;
static TBOOL           s_bHasRemote   = TFALSE;
static DWORD           s_uiRemoteTime = 0; // GetTickCount of the last one

static TUINT16 s_uiSendSequence = 0;
static TFLOAT  s_fSendWait      = 0.0f;

// GetTickCount when this game learned that the other player had completed the quest and this one hadn't, 0 if not
static DWORD s_auiAheadSince[ ACoopProgress::MAX_QUESTS ];

// The player hid the prompt while it showed the quest
static TBOOL s_abHidden[ ACoopProgress::MAX_QUESTS ];

// The quests the player chose to skip. One skip catches up with all of them, a tier at a time
static TBOOL  s_abCatchUp[ ACoopProgress::MAX_QUESTS ];
static TINT   s_iNumCatchUp   = 0;
static TFLOAT s_fCatchUpWait  = 0.0f;
static TINT   s_iNumSkipped   = 0;    // by this catch up
static TINT   s_iFirstSkipped = -1;
static TINT   s_iPreparedQuest = -1;  // the quest going on here whose skip has been prepared (PrepareAssignedSkip)

// Where a skip can be applied now
enum PLACE
{
	PLACE_NONE,     // nowhere: something is going on
	PLACE_FARM,     // the farm (ASimState): the story changes here (ACoopProgress::CompleteQuest)
	PLACE_BARN,     // the barn (ABarnExploreMinigameState): the same
	PLACE_MINIGAME, // the minigame of the assigned quest: it can end as won (AMiniGameState::EndAsWon)
};

static PLACE s_ePlace = PLACE_NONE;

// How long it has been safe to change the story (0 while it isn't), and why it isn't
static TFLOAT       s_fSafeTime     = 0.0f;
static const TCHAR* s_szUnsafeWhy   = "";
static DWORD        s_uiFarmUpTime  = 0;     // GetTickCount when it was first safe (the dev switches count from it)
static TFLOAT       s_fSkipDelay    = ACoopProgress::SKIP_DELAY;
static TBOOL        s_bStoryChanged = TFALSE; // CompleteQuest changed the story this frame

// How long the minigame of the assigned quest has been running (0 while PLACE_MINIGAME isn't the place)
static TFLOAT s_fMiniGameTime = 0.0f;

// The quest whose minigame was skipped (it ended as won): the story completes it once it's back, it isn't offered
// meanwhile (IsBeingWon). -1 if none
static TINT  s_iWonQuest = -1;
static DWORD s_uiWonTime = 0;

// The quests completed in the barn: their spawns on the farm are cleared once the farm is back (the farm's animals are
// kept in its quick save while the barn is up)
static TINT s_aiFarmCleanUp[ 16 ];
static TINT s_iNumFarmCleanUp = 0;

// The skips of this game the other one is told about
static SkipMessage s_oSkipMessage;
static TINT        s_iSkipCopiesLeft = 0;
static TFLOAT      s_fSkipCopyWait   = 0.0f;
static TUINT16     s_uiSkipId        = 0;
static TUINT16     s_uiLastHeardSkip = 0;
static TBOOL       s_bHeardSkip      = TFALSE;

// What the other player had already done when the two stories met (a game joined late, a new game next to a later
// one) can be caught up with right away, only what is done later waits SKIP_DELAY. Set again when one of the games
// goes back to the frontend
static TBOOL s_bStoriesMet = TFALSE;

// Saves (see OnGameSaved): the other game saved and this one saves too at its next safe moment
static TBOOL   s_bPartnerSavePending = TFALSE;
static TUINT16 s_uiPartnerSaveId     = 0;     // the id of the other game's save this one answers
static TBOOL   s_bSavingForPartner   = TFALSE; // the save going on was started because the other game saved
static TBOOL   s_bToldSaveWaits      = TFALSE; // the player knows the save waits for the quest
static TUINT16 s_uiOwnSaveId         = 0;     // the last save of this game the other one was told about
static DWORD   s_uiOwnSaveTime       = 0;     // GetTickCount of the last save of this game (0: none yet)

//-----------------------------------------------------------------------------
// The line at the top of the screen. The texts the players see are texts of
// the mod: MODSTRING_COOPSTORY_... in Locale/AModStrings, which has the English
// text, what its %s and %d stand for and the translation into the language of
// the game.
//-----------------------------------------------------------------------------

enum PROMPT
{
	PROMPT_NONE,
	PROMPT_OFFER,    // quests can be skipped
	PROMPT_MINIGAME, // the minigame being played can be skipped (it ends as won)
	PROMPT_BLOCKED,  // the other player completed a quest that can't be skipped yet
	PROMPT_CATCHUP,  // skipping a tier at a time
};

static constexpr TINT LINE_SIZE = 160;

static PROMPT  s_ePrompt = PROMPT_NONE;
static TWCHAR  s_awszPrompt[ 2 ][ LINE_SIZE ];
static TFLOAT  s_fPromptTime = 0.0f; // how long the prompt has been shown
static TFLOAT  s_fHoldTime   = 0.0f; // how long the skip button has been held
static TBOOL   s_bHoldUsed   = TFALSE;
static TINT    s_aiPlan[ ACoopProgress::MAX_QUESTS ];
static TINT    s_iPlanSize    = 0;
static TINT    s_iBlocked     = -1;
static TWCHAR  s_awszNotice[ 2 ][ LINE_SIZE ];
static TFLOAT  s_fNoticeTime = 0.0f; // a notice replaces the prompt while > 0 (seconds it's still shown)
static DWORD   s_uiNoticeTime = 0;   // GetTickCount when it was given

// A line of the prompt or of a notice (LINE_SIZE characters), cut if a translation is longer
static void SetLine( TWCHAR* a_wszOut, const TWCHAR* a_wszText )
{
	TStringManager::String16CopySafe( a_wszOut, a_wszText, LINE_SIZE );
}

// A line for the log: the letters of a translation that aren't ASCII become '?'
static void GetLogText( TCHAR* a_szOut, const TWCHAR* a_wszLine )
{
	TINT i = 0;

	for ( ; a_wszLine[ i ] != L'\0' && i < LINE_SIZE - 1; i++ )
		a_szOut[ i ] = ( a_wszLine[ i ] < 0x80 ) ? TCHAR( a_wszLine[ i ] ) : '?';

	a_szOut[ i ] = '\0';
}

static void ShowNotice( const TWCHAR* a_wszLine1, const TWCHAR* a_wszLine2, TFLOAT a_fSeconds )
{
	SetLine( s_awszNotice[ 0 ], a_wszLine1 );
	SetLine( s_awszNotice[ 1 ], a_wszLine2 );
	s_fNoticeTime  = a_fSeconds;
	s_uiNoticeTime = GetTickCount();

	if ( s_bLog )
	{
		TCHAR szLine1[ LINE_SIZE ];
		TCHAR szLine2[ LINE_SIZE ];
		GetLogText( szLine1, s_awszNotice[ 0 ] );
		GetLogText( szLine2, s_awszNotice[ 1 ] );
		TINFO( "[coopstory] notice: %s / %s\n", szLine1, szLine2 );
	}
}

// Two lines at the top of the screen with the HUD of the farm, the barn and the minigames (added to AHudManager the first
// time it's needed)
class ACoopProgressHud : public AHudElement
{
public:
	TDECLARE_CLASS( ACoopProgressHud, AHudElement );

	ACoopProgressHud();
	~ACoopProgressHud();

	virtual TBOOL OnAttach() override;
	virtual void  OnDetach() override;
	virtual void  Show() override;
	virtual void  Hide() override;
	virtual void  Update( TFLOAT a_fDeltaTime ) override;

private:
	void SetLinesVisible( TBOOL a_bVisible );

private:
	AGUI2TextBox m_aLines[ 2 ];
	T2GUIFont*   m_pFont;
	TFLOAT       m_fAlpha;
};

TDEFINE_CLASS( ACoopProgressHud );

static ACoopProgressHud* s_pHud = TNULL;

ACoopProgressHud::ACoopProgressHud()
    : AHudElement( MASK_PLAYER_MAIN | MASK_MINIGAME | MASK_INTERACTION )
    , m_pFont( TNULL )
    , m_fAlpha( 0.0f )
{
	// Shown by AHudManager while the HUD of the current state has the player's main HUD (the farm), a minigame's HUD or
	// the animals' exclamation marks (the barn)
	m_bCurrentlyVisible = TFALSE;
}

ACoopProgressHud::~ACoopProgressHud()
{
	if ( s_pHud == this )
		s_pHud = TNULL;
}

TBOOL ACoopProgressHud::OnAttach()
{
	m_pFont = T2GUIFontManager::FindFont( AGUI2STYLE_FONT_SECONDARY );

	TFLOAT fScreenWidth;
	TFLOAT fScreenHeight;
	AGUI2::GetRootElement()->GetDimensions( fScreenWidth, fScreenHeight );

	for ( TINT i = 0; i < 2; i++ )
	{
		AGUI2TextBox& rLine = m_aLines[ i ];
		rLine.Create( m_pFont, 0.0f );
		rLine.SetAttachment( T2GUIATTACHMENT_TOPCENTER, T2GUIATTACHMENT_TOPCENTER );
		rLine.SetColour( ( i == 0 ) ? 0xFFFFFFFF : AGUI2STYLE_COLOR_PRIMARY );
		rLine.SetShadowEnabled( TTRUE );
		rLine.SetShadowColour( 0xA0000000 );
		rLine.SetShadowAlpha( 0.6f );
		rLine.SetShadowOffset( 3.5f, 3.5f );
		rLine.SetSize( 0.8f * fScreenWidth, 0.0f, TFALSE );

		// Below the top edge, between the clock and the money
		rLine.SetTransform( 0.0f, 40.0f + i * m_pFont->GetLineHeight() * 1.25f );
		AGUI2::GetRootElement()->AddChildTail( rLine );
		rLine.T2GUIElement::Hide();
	}

	return TTRUE;
}

void ACoopProgressHud::OnDetach()
{
	for ( TINT i = 0; i < 2; i++ )
		m_aLines[ i ].RemoveSelf();

	T2GUIFontManager::ReleaseFont( m_pFont );
	m_pFont = TNULL;
}

void ACoopProgressHud::SetLinesVisible( TBOOL a_bVisible )
{
	for ( TINT i = 0; i < 2; i++ )
	{
		if ( a_bVisible )
			m_aLines[ i ].T2GUIElement::Show();
		else
			m_aLines[ i ].T2GUIElement::Hide();
	}
}

void ACoopProgressHud::Show()
{
	m_bCurrentlyVisible = TTRUE;
}

void ACoopProgressHud::Hide()
{
	SetLinesVisible( TFALSE );
	m_fAlpha            = 0.0f;
	m_bCurrentlyVisible = TFALSE;
}

void ACoopProgressHud::Update( TFLOAT a_fDeltaTime )
{
	// While a network session runs: an offer (it waits for the player), else a notice, else the progress of a catch up
	TWCHAR( *pLines )[ LINE_SIZE ] = TNULL;

	if ( ACoopNet::GetMode() != ACoopNet::MODE_OFF )
	{
		if ( s_ePrompt == PROMPT_OFFER || s_ePrompt == PROMPT_MINIGAME || s_ePrompt == PROMPT_BLOCKED )
		{
			pLines = s_awszPrompt;
		}
		else if ( s_fNoticeTime > 0.0f )
		{
			// It counts down while it's shown: a save shows it after the save screen
			pLines = s_awszNotice;
			s_fNoticeTime -= a_fDeltaTime;
		}
		else if ( s_ePrompt != PROMPT_NONE )
		{
			pLines = s_awszPrompt;
		}
	}

	m_fAlpha = TMath::Max( TMath::Min( m_fAlpha + ( pLines ? 4.0f : -4.0f ) * a_fDeltaTime, 1.0f ), 0.0f );

	if ( !pLines && m_fAlpha <= 0.0f )
	{
		SetLinesVisible( TFALSE );
		return;
	}

	SetLinesVisible( TTRUE );

	for ( TINT i = 0; i < 2; i++ )
	{
		// The text box keeps the pointer, the buffers are static
		if ( pLines )
			m_aLines[ i ].SetText( pLines[ i ] );

		m_aLines[ i ].SetAlpha( m_fAlpha );
		m_aLines[ i ].SetShadowAlpha( m_fAlpha * 0.6f );
	}
}

static void EnsureHud()
{
	if ( s_pHud || !AHudManager::IsSingletonCreated() || !AGUI2::IsSingletonCreated() )
		return;

	s_pHud = new ACoopProgressHud();

	if ( !AHudManager::GetSingleton()->AddElement( s_pHud, TTRUE, TTRUE, TFALSE ) )
	{
		delete s_pHud;
		s_pHud = TNULL;
	}
}

//-----------------------------------------------------------------------------
// Development switches (not in Final builds)
//-----------------------------------------------------------------------------

// What "-coopcomplete", "-coopassign", "-coopsave", "-coopminigame", "-coopgamestate" and "-coopfinish" ask for
enum DEVACTION
{
	DEVACTION_COMPLETE,
	DEVACTION_ASSIGN,
	DEVACTION_SAVE,
	DEVACTION_MINIGAME,  // starts the minigame szName, as a quest giver does (AMiniGameManager::SetNextMinigame)
	DEVACTION_GAMESTATE, // goes to the game state szName (the barn's door: AGameStateController::StartGameState)
	DEVACTION_FINISH,    // leaves the game state (the barn's door: AGameStateController::FinishGameState)
	DEVACTION_GIVE,      // gives the player an item of the type szName (a quest item it would have picked up)
	DEVACTION_TIME,      // sets this game's clock to iQuest o'clock (as sleeping would)
	DEVACTION_STEP,      // completes the step (element) iQuest of the assigned quest, as its script would
};

struct DevAction
{
	DEVACTION eAction;
	TFLOAT    fTime;  // seconds after the farm was first up
	TINT      iQuest; // -1: the assigned quest or the first open one of the tier (DEVACTION_COMPLETE)
	TCHAR     szName[ 48 ];
	TBOOL     bDone;
};

static TBOOL s_bDevMiniGame = TFALSE; // "-coopminigame": the instructions of the minigame are dismissed after a while

static TFLOAT    s_fAutoSkip = -1.0f; // "-coopskip": seconds after the prompt is shown, -1 if not given
static DevAction s_aDevActions[ 16 ];
static TINT      s_iNumDevActions = 0;
static TCHAR     s_szShotPrefix[ 200 ]; // "-coopshots"

struct DevShot
{
	TCHAR  szEvent[ 16 ];
	TFLOAT fWait;
};

static DevShot s_aDevShots[ 8 ];
static TINT    s_iNumDevShots = 0;

#ifndef TOSHI_FINAL
// Returns TTRUE if the whole switch starts an argument of the command line, a_ppOutArgs receives what follows it
static TBOOL FindSwitch( const TCHAR* a_szSwitch, const TCHAR** a_ppOutArgs = TNULL )
{
	const TINT   iLength       = TStringManager::String8Length( a_szSwitch );
	const TCHAR* szCommandLine = AGameLoader::Dev_GetCommandLine();
	const TCHAR* szFound       = szCommandLine;

	while ( szFound && ( szFound = TStringManager::String8FindString( szFound, a_szSwitch ) ) != TNULL )
	{
		const TBOOL bStartsArgument = szFound == szCommandLine || szFound[ -1 ] == ' ' || szFound[ -1 ] == '"';

		if ( bStartsArgument && ( szFound[ iLength ] == '\0' || szFound[ iLength ] == ' ' || szFound[ iLength ] == '"' ) )
		{
			if ( a_ppOutArgs )
				*a_ppOutArgs = szFound + iLength;

			return TTRUE;
		}

		szFound += iLength;
	}

	return TFALSE;
}

// Reads "s[:q][,s[:q]...]" (or "s:name[,...]") after the switch a_szSwitch into s_aDevActions
static void ParseDevActions( const TCHAR* a_szSwitch, DEVACTION a_eAction )
{
	const TCHAR* szArgs = TNULL;

	if ( !FindSwitch( a_szSwitch, &szArgs ) )
		return;

	while ( *szArgs == ' ' ) szArgs++;

	while ( *szArgs >= '0' && *szArgs <= '9' && s_iNumDevActions < TARRAYSIZE( s_aDevActions ) )
	{
		TCHAR*     szEnd   = TNULL;
		DevAction& rAction = s_aDevActions[ s_iNumDevActions++ ];
		rAction.eAction    = a_eAction;
		rAction.fTime      = TFLOAT( strtod( szArgs, &szEnd ) );
		rAction.iQuest     = -1;
		rAction.szName[ 0 ] = '\0';
		rAction.bDone      = TFALSE;
		szArgs             = szEnd;

		if ( *szArgs == ':' && ( a_eAction == DEVACTION_MINIGAME || a_eAction == DEVACTION_GAMESTATE || a_eAction == DEVACTION_GIVE ) )
		{
			TINT iLength = 0;
			szArgs++;

			while ( *szArgs != '\0' && *szArgs != ',' && *szArgs != ' ' && *szArgs != '"' && iLength < TARRAYSIZE( rAction.szName ) - 1 )
				rAction.szName[ iLength++ ] = *szArgs++;

			rAction.szName[ iLength ] = '\0';
		}
		else if ( *szArgs == ':' )
		{
			rAction.iQuest = TINT( strtol( szArgs + 1, &szEnd, 10 ) );
			szArgs         = szEnd;
		}

		if ( *szArgs == ',' )
			szArgs++;
	}
}
#endif // !TOSHI_FINAL

static void ParseSwitches()
{
	static TBOOL s_bParsed = TFALSE;

	if ( s_bParsed )
		return;

	s_bParsed           = TTRUE;
	s_szShotPrefix[ 0 ] = '\0';

#ifndef TOSHI_FINAL
	s_bLog = FindSwitch( "-cooplog" );

	const TCHAR* szArgs = TNULL;

	if ( FindSwitch( "-coopskip", &szArgs ) )
	{
		while ( *szArgs == ' ' ) szArgs++;
		s_fAutoSkip = ( *szArgs >= '0' && *szArgs <= '9' ) ? TFLOAT( atof( szArgs ) ) : 3.0f;
	}

	if ( FindSwitch( "-coopdelay", &szArgs ) )
	{
		while ( *szArgs == ' ' ) szArgs++;

		if ( *szArgs >= '0' && *szArgs <= '9' )
			s_fSkipDelay = TFLOAT( atof( szArgs ) );
	}

	ParseDevActions( "-coopcomplete", DEVACTION_COMPLETE );
	ParseDevActions( "-coopassign", DEVACTION_ASSIGN );
	ParseDevActions( "-coopsave", DEVACTION_SAVE );
	ParseDevActions( "-coopminigame", DEVACTION_MINIGAME );
	ParseDevActions( "-coopgamestate", DEVACTION_GAMESTATE );
	ParseDevActions( "-coopfinish", DEVACTION_FINISH );
	ParseDevActions( "-coopgive", DEVACTION_GIVE );
	ParseDevActions( "-cooptime", DEVACTION_TIME );
	ParseDevActions( "-coopstep", DEVACTION_STEP );
	s_bDevMiniGame = FindSwitch( "-coopminigame" );

	if ( FindSwitch( "-coopshots", &szArgs ) )
	{
		while ( *szArgs == ' ' ) szArgs++;

		TINT i = 0;

		while ( szArgs[ i ] != '\0' && szArgs[ i ] != ' ' && szArgs[ i ] != '"' && i < TARRAYSIZE( s_szShotPrefix ) - 1 )
		{
			s_szShotPrefix[ i ] = szArgs[ i ];
			i++;
		}

		s_szShotPrefix[ i ] = '\0';
	}

	if ( s_bLog )
		TINFO( "[coopstory] -coopskip %.1f, skips offered after %.0f s, %d timed actions, shots '%s'\n", s_fAutoSkip, s_fSkipDelay, s_iNumDevActions, s_szShotPrefix );
#endif
}

// "-coopshots": a frame of a_szEvent in a_fWait seconds
static void RequestShot( const TCHAR* a_szEvent, TFLOAT a_fWait )
{
	if ( s_szShotPrefix[ 0 ] == '\0' || s_iNumDevShots >= TARRAYSIZE( s_aDevShots ) )
		return;

	TStringManager::String8CopySafe( s_aDevShots[ s_iNumDevShots ].szEvent, a_szEvent, sizeof( s_aDevShots[ 0 ].szEvent ) );
	s_aDevShots[ s_iNumDevShots ].fWait = a_fWait;
	s_iNumDevShots++;
}

static void UpdateShots( TFLOAT a_fDeltaTime )
{
	for ( TINT i = 0; i < s_iNumDevShots; )
	{
		s_aDevShots[ i ].fWait -= a_fDeltaTime;

		if ( s_aDevShots[ i ].fWait > 0.0f )
		{
			i++;
			continue;
		}

#ifdef PRIMEDEBUG_DEV
		static TINT s_iNumShots = 0;

		TCHAR szLine[ 300 ];
		TStringManager::String8Format( szLine, sizeof( szLine ), "screenshot %s_%s%d.png", s_szShotPrefix, s_aDevShots[ i ].szEvent, ++s_iNumShots );
		PConsole::Execute( szLine );

		if ( s_bLog )
			TINFO( "[coopstory] %s\n", szLine );
#endif

		s_aDevShots[ i ] = s_aDevShots[ --s_iNumDevShots ];
	}
}

//-----------------------------------------------------------------------------
// Reading this game's story
//-----------------------------------------------------------------------------

// TTRUE while AQuestManager has this game's story: the farm or the barn is on the stack.
// NOTE: AGameSystemManager::Reset resets AQuestManager whenever a level is loaded. The loads empty the stack first and the
// farm and the barn bring the story back from the quick save in the same call (LoadMainGameImpl, LoadGameStateImpl).
// A scene that has been asked for (the next tier's) only loads later in the frame (ARootTask::OnUpdate, after ACoopNet),
// so the tier that asked for it is still read
static TBOOL IsStoryLive()
{
	if ( !AQuestManager::IsSingletonCreated() || !AGameStateController::IsSingletonCreated() )
		return TFALSE;

	if ( !AGameLoader::IsStoryMode() || AGameLoader::IsReturningToFrontEnd() || AGameLoader::IsChangingTerrain() || AGameLoader::IsInMiniGame() )
		return TFALSE;

	if ( !AGameLoader::IsMainGameLoaded() && !AGameLoader::IsBarnExploreMiniGame() )
		return TFALSE;

	AGameStateController* pController = AGameStateController::GetSingleton();
	return pController->FindGameStateOnStack( TPS8( ASimState ) ) || pController->FindGameStateOnStack( TPS8( ABarnExploreMinigameState ) );
}

static void UpdateLocalProgress()
{
	// Back at the frontend: the next game can be another one
	if ( !AGameLoader::IsStoryMode() )
		s_bLocalValid = TFALSE;

	const TBOOL bLive = PrepareQuestData() && IsStoryLive();

	if ( bLive )
	{
		AQuestManager* pQuestManager = AQuestManager::GetSingleton();

		s_oLocal.uiAct           = TUINT8( pQuestManager->GetAct() );
		s_oLocal.uiTier          = TUINT8( pQuestManager->GetTier() );
		s_oLocal.uiNumTiers      = TUINT8( s_iNumTiers );
		s_oLocal.iAssignedQuest  = -1;
		s_oLocal.uiAssignedState = 0;
		s_oLocal.uiAssignedDone  = 0;

		if ( AQuestInstance* pInstance = pQuestManager->GetAssignedQuest( 0 ) )
		{
			s_oLocal.iAssignedQuest  = TINT8( pInstance->GetQuestIndex() );
			s_oLocal.uiAssignedState = TUINT8( pInstance->GetState() );

			for ( TINT i = 0; i < pInstance->GetQuest()->GetNumElements() && i < 16; i++ )
			{
				if ( pInstance->IsElementDone( i ) )
					s_oLocal.uiAssignedDone |= TUINT16( 1 << i );
			}
		}

		for ( TINT i = 0; i < AQuestManager::MAX_NUM_TIERS; i++ )
			s_oLocal.auiCompleted[ i ] = ( i < s_iNumTiers ) ? pQuestManager->GetQuestTier( i + 1 )->uiCompletedMask : 0;

		if ( AGameTimeManager::IsSingletonCreated() )
		{
			AGameTimeManager* pTime = AGameTimeManager::GetSingleton();
			s_oLocal.uiDayPhase     = TUINT8( pTime->GetCurrentDayPhase() );
			s_oLocal.uiDay          = TUINT8( pTime->GetDay() );
			s_oLocal.fDayTime       = pTime->GetDayTime();
		}

		s_bLocalValid = TTRUE;
	}

	TUINT8 uiFlags = 0;

	if ( s_bLocalValid )
		uiFlags |= PROGRESSFLAGS_IN_STORY;

	if ( bLive )
		uiFlags |= PROGRESSFLAGS_LIVE;

	if ( AGameLoader::IsBarnExploreMiniGame() )
		uiFlags |= PROGRESSFLAGS_BARN;

	if ( AGameLoader::IsInMiniGame() )
		uiFlags |= PROGRESSFLAGS_MINIGAME;

	if ( AGameLoader::GetScriptedSequence() != -1 )
		uiFlags |= PROGRESSFLAGS_SCENE;

	if ( AQuestManager::IsSingletonCreated() && AQuestManager::GetSingleton()->IsGameComplete() )
		uiFlags |= PROGRESSFLAGS_COMPLETE;

	if ( s_fSafeTime > 0.0f )
		uiFlags |= PROGRESSFLAGS_SAFE;

	s_oLocal.uiMagic        = PROGRESS_MAGIC;
	s_oLocal.uiDataChecksum = s_uiDataChecksum;
	s_oLocal.uiFlags        = uiFlags;
}

// Where a_rcProgress' player is, for the log
static const TCHAR* GetWhere( const ProgressMessage& a_rcProgress )
{
	if ( !( a_rcProgress.uiFlags & PROGRESSFLAGS_IN_STORY ) ) return "not in a story game";
	if ( a_rcProgress.uiFlags & PROGRESSFLAGS_MINIGAME ) return "in a minigame";
	if ( a_rcProgress.uiFlags & PROGRESSFLAGS_SCENE ) return "in a scene";
	if ( a_rcProgress.uiFlags & PROGRESSFLAGS_BARN ) return "in the barn";
	if ( a_rcProgress.uiFlags & PROGRESSFLAGS_LIVE ) return ( a_rcProgress.uiFlags & PROGRESSFLAGS_SAFE ) ? "on the farm, free" : "on the farm, busy";
	return "loading";
}

static void LogProgress( const TCHAR* a_szWho, const ProgressMessage& a_rcProgress )
{
	TINFO( "[coopstory] %s: act %d tier %d (abs %d/%d), %d quests done, assigned %s (state %d, elements done 0x%x), day %d phase %d time %.0f, %s\n",
	       a_szWho, a_rcProgress.uiAct, a_rcProgress.uiTier, GetAbsTier( a_rcProgress ), a_rcProgress.uiNumTiers, CountDone( a_rcProgress ),
	       ( a_rcProgress.iAssignedQuest >= 0 ) ? GetQuestName( a_rcProgress.iAssignedQuest ) : "none", a_rcProgress.uiAssignedState,
	       a_rcProgress.uiAssignedDone, a_rcProgress.uiDay, a_rcProgress.uiDayPhase, a_rcProgress.fDayTime, GetWhere( a_rcProgress ) );
}

//-----------------------------------------------------------------------------
// The other game's story
//-----------------------------------------------------------------------------

static TBOOL IsRemoteUsable()
{
	return ACoopNet::IsConnected() && s_bHasRemote && s_uiDataChecksum != 0 &&
	    ( GetTickCount() - s_uiRemoteTime ) < DWORD( ACoopProgress::PROGRESS_TIMEOUT * 1000.0f ) &&
	    ( s_oRemote.uiFlags & PROGRESSFLAGS_IN_STORY ) &&
	    s_oRemote.uiDataChecksum == s_uiDataChecksum && s_oRemote.uiNumTiers == s_iNumTiers;
}

//-----------------------------------------------------------------------------
// Saves on both sides: when one game saves its game, the other saves its own
//-----------------------------------------------------------------------------

// SAVEKIND_SAVED of this game's last save, sent again every 2 seconds until the other game answers (or a minute)
static SaveMessage s_oSavedMessage;
static TINT        s_iSavedCopiesLeft = 0;
static TFLOAT      s_fSavedCopyWait   = 0.0f;

// SAVEKIND_SAVED_ALSO, sent 3 times
static SaveMessage s_oSavedAlsoMessage;
static TINT        s_iSavedAlsoCopiesLeft = 0;
static TFLOAT      s_fSavedAlsoCopyWait   = 0.0f;

// The copies of a message the other game sent
static TUINT16 s_uiLastHeardSaveId   = 0;
static TUINT8  s_uiLastHeardSaveKind = 0;

static DWORD s_uiSavingForPartnerTime = 0; // GetTickCount when that save was started

static void FillSaveMessage( SaveMessage& a_rMessage, SAVEKIND a_eKind, TUINT16 a_uiId )
{
	a_rMessage           = {};
	a_rMessage.uiMagic   = SAVE_MAGIC;
	a_rMessage.uiId      = a_uiId;
	a_rMessage.uiKind    = a_eKind;
	a_rMessage.uiNumDone = TUINT8( s_bLocalValid ? CountDone( s_oLocal ) : 0 );

	if ( AQuestManager::IsSingletonCreated() )
	{
		a_rMessage.uiAct  = TUINT8( AQuestManager::GetSingleton()->GetAct() );
		a_rMessage.uiTier = TUINT8( AQuestManager::GetSingleton()->GetTier() );
	}
}

static void SendSaveMessages( TFLOAT a_fDeltaTime )
{
	if ( s_iSavedCopiesLeft > 0 && ( s_fSavedCopyWait -= a_fDeltaTime ) <= 0.0f )
	{
		if ( ACoopNet::SendData( ACoopNet::CHANNEL_PROGRESS, &s_oSavedMessage, sizeof( s_oSavedMessage ) ) )
			s_iSavedCopiesLeft--;

		s_fSavedCopyWait = 2.0f;
	}

	if ( s_iSavedAlsoCopiesLeft > 0 && ( s_fSavedAlsoCopyWait -= a_fDeltaTime ) <= 0.0f )
	{
		if ( ACoopNet::SendData( ACoopNet::CHANNEL_PROGRESS, &s_oSavedAlsoMessage, sizeof( s_oSavedAlsoMessage ) ) )
			s_iSavedAlsoCopiesLeft--;

		s_fSavedAlsoCopyWait = 0.25f;
	}
}

// This game saved because the other one did (or both saved at about the same time): tell the other game
static void AnswerPartnerSave( TUINT16 a_uiId )
{
	FillSaveMessage( s_oSavedAlsoMessage, SAVEKIND_SAVED_ALSO, a_uiId );
	s_iSavedAlsoCopiesLeft = 3;
	s_fSavedAlsoCopyWait   = 0.0f;
}

static void OnSaveMessage( const SaveMessage& a_rcMessage )
{
	// The copies of a message
	if ( a_rcMessage.uiId == s_uiLastHeardSaveId && a_rcMessage.uiKind == s_uiLastHeardSaveKind )
		return;

	s_uiLastHeardSaveId   = a_rcMessage.uiId;
	s_uiLastHeardSaveKind = a_rcMessage.uiKind;

	if ( a_rcMessage.uiKind == SAVEKIND_SAVED )
	{
		if ( s_bLog )
			TINFO( "[coopstory] save: the partner saved (save %d: act %d tier %d, %d quests done)\n", a_rcMessage.uiId, a_rcMessage.uiAct, a_rcMessage.uiTier, a_rcMessage.uiNumDone );

		// This game has just been saved too: both saves are fresh
		if ( s_uiOwnSaveTime != 0 && GetTickCount() - s_uiOwnSaveTime < 10000 )
		{
			if ( s_bLog )
				TINFO( "[coopstory] save: this game saved %.1f s ago, nothing more to save\n", ( GetTickCount() - s_uiOwnSaveTime ) / 1000.0f );

			AnswerPartnerSave( a_rcMessage.uiId );
			return;
		}

		s_bPartnerSavePending = TTRUE;
		s_uiPartnerSaveId     = a_rcMessage.uiId;
		s_bToldSaveWaits      = TFALSE;
	}
	else if ( a_rcMessage.uiKind == SAVEKIND_SAVED_ALSO && a_rcMessage.uiId == s_uiOwnSaveId )
	{
		s_iSavedCopiesLeft = 0;

		if ( s_bLog )
			TINFO( "[coopstory] save: the partner saved too (act %d tier %d, %d quests done)\n", a_rcMessage.uiAct, a_rcMessage.uiTier, a_rcMessage.uiNumDone );

		ShowNotice( AModStrings::Get( MODSTRING_COOPSTORY_SAVED_BOTH ), AModStrings::Get( MODSTRING_COOPSTORY_SAVED_BOTH_2 ), 5.0f );
		RequestShot( "savedboth", 4.0f );
	}
}

void ACoopProgress::OnGameSaved()
{
	const DWORD uiNow = GetTickCount();
	s_uiOwnSaveTime   = ( uiNow != 0 ) ? uiNow : 1;

	if ( ACoopNet::GetMode() == ACoopNet::MODE_OFF )
	{
		s_bSavingForPartner   = TFALSE;
		s_bPartnerSavePending = TFALSE;
		return;
	}

	if ( s_bSavingForPartner || s_bPartnerSavePending )
	{
		// The other game saved first: this save answers it (also when the player saved before the automatic save)
		if ( s_bLog )
			TINFO( "[coopstory] save: saved %s, the partner's save %d is answered\n", s_bSavingForPartner ? "because the partner saved" : "by the player", s_uiPartnerSaveId );

		s_bSavingForPartner   = TFALSE;
		s_bPartnerSavePending = TFALSE;
		AnswerPartnerSave( s_uiPartnerSaveId );
		ShowNotice( AModStrings::Get( MODSTRING_COOPSTORY_SAVED_FOR_PARTNER ), AModStrings::Get( MODSTRING_COOPSTORY_SAVED_FOR_PARTNER_2 ), 5.0f );
		RequestShot( "savedtoo", 4.0f );
		return;
	}

	// The ids go on from a different number in every run of the game, the copies of a message are told apart by them
	if ( s_uiOwnSaveId == 0 )
		s_uiOwnSaveId = TUINT16( uiNow );

	FillSaveMessage( s_oSavedMessage, SAVEKIND_SAVED, ++s_uiOwnSaveId );
	s_iSavedCopiesLeft = 30;
	s_fSavedCopyWait   = 0.0f;

	if ( s_bLog )
		TINFO( "[coopstory] save: saved, the partner is asked to save too (save %d: act %d tier %d)\n", s_uiOwnSaveId, s_oSavedMessage.uiAct, s_oSavedMessage.uiTier );
}

// The other game saved: this one saves its own game into its own slot once it can (the same save as the bed's)
static void UpdatePartnerSave()
{
	if ( s_bSavingForPartner )
	{
		// The save didn't come to an end (it failed): it can be tried again
		if ( GetTickCount() - s_uiSavingForPartnerTime > 30000 )
		{
			if ( s_bLog )
				TINFO( "[coopstory] save: the save for the partner didn't finish, trying again\n" );

			s_bSavingForPartner = TFALSE;
		}

		return;
	}

	if ( !s_bPartnerSavePending || s_fSafeTime < ACoopProgress::SETTLE_TIME || s_iNumCatchUp > 0 )
		return;

	if ( !APlayerProgress::GetSingleton()->GetCurrentSlot() )
	{
		// Only the development switches start a game without a slot
		if ( s_bLog )
			TINFO( "[coopstory] save: this game has no save slot, it can't save with the partner\n" );

		s_bPartnerSavePending = TFALSE;
		return;
	}

	// The game can't be saved while a quest is going on (AGameStateController::SaveCurrentGameState)
	if ( AQuestManager::GetSingleton()->GetNumAssignedQuests() > 0 )
	{
		if ( !s_bToldSaveWaits )
		{
			s_bToldSaveWaits = TTRUE;
			ShowNotice( AModStrings::Get( MODSTRING_COOPSTORY_SAVE_WAITS ), AModStrings::Get( MODSTRING_COOPSTORY_SAVE_WAITS_2 ), 5.0f );

			if ( s_bLog )
				TINFO( "[coopstory] save: waits for the quest '%s' to end\n", AQuestManager::GetSingleton()->GetAssignedQuest( 0 )->GetQuest()->GetName().GetString() );
		}

		return;
	}

	if ( s_bLog )
		TINFO( "[coopstory] save: saving into slot %d because the partner saved (save %d)\n", APlayerProgress::GetSingleton()->GetCurrentSlotIndex(), s_uiPartnerSaveId );

	s_bSavingForPartner      = TTRUE;
	s_uiSavingForPartnerTime = GetTickCount();
	AGameStateController::GetSingleton()->SaveCurrentGameState( TFALSE, TFALSE, TFALSE );
}

//-----------------------------------------------------------------------------
// The time of the day: the game that joined follows the host's clock
//-----------------------------------------------------------------------------

// How far apart the clocks may be before the joiner's is set to the host's (AGameTimeManager's units, real seconds: a
// day is 1200 of them, 12.5 is a quarter of an hour of game time)
static constexpr TFLOAT CLOCK_TOLERANCE = 12.5f;

static TFLOAT s_fClockWait      = 0.0f; // seconds until the clocks are compared again
static TBOOL  s_bToldClockWaits = TFALSE;

// The day phase of the time a_fDayTime, the way AGameTimeManager::SetTime works it out (a_eKeep if it finds none)
static AGameTimeManager::DAYPHASE GetDayPhaseAt( TFLOAT a_fDayTime, AGameTimeManager::DAYPHASE a_eKeep )
{
	TFLOAT fAccum = 0.0f;

	for ( TINT i = 0; i < AGameTimeManager::DAYPHASE_NUMOF; i++ )
	{
		fAccum += AGameTimeManager::DAYPHASE_DURATION[ i ];

		if ( a_fDayTime < AGameTimeManager::REAL_TIME_TO_GAME_TIME * AGameTimeManager::DAY_START_SECONDS + fAccum )
			return i;
	}

	return a_eKeep;
}

// The joiner's clock follows the host's: the day phase decides which quests are offered (day or night quests) and what
// the farm looks like. Set on the farm at a safe moment with no quest going on (a new day phase gives the quest of the
// phase up, AQuestManager::OnDateChange). The date isn't followed
static void UpdateClock( TFLOAT a_fDeltaTime, DWORD a_uiNow )
{
	if ( ( s_fClockWait -= a_fDeltaTime ) > 0.0f )
		return;

	s_fClockWait = 2.0f;

	if ( ACoopNet::GetMode() != ACoopNet::MODE_JOIN || s_ePlace != PLACE_FARM || s_fSafeTime < ACoopProgress::SETTLE_TIME || s_iNumCatchUp > 0 ||
	     !s_bLocalValid || !IsRemoteUsable() || !( s_oRemote.uiFlags & PROGRESSFLAGS_LIVE ) || !AGameTimeManager::IsSingletonCreated() )
		return;

	AGameTimeManager* pTime = AGameTimeManager::GetSingleton();

	if ( pTime->IsUpdateStopped() )
		return;

	// The host's clock now: its last message and the time since (both clocks run at real time)
	const TFLOAT fDayLength = AGameTimeManager::DAY_LENGTH_UNITS;
	TFLOAT       fHostTime  = s_oRemote.fDayTime + ( a_uiNow - s_uiRemoteTime ) / 1000.0f;

	while ( fHostTime >= fDayLength )
		fHostTime -= fDayLength;

	// From 6:00, the start of the day: AGameTimeManager::SetTime works the day phase out from it (the hours before it,
	// the end of the night, would be dawn). Followed again once the host's day has started
	if ( fHostTime < AGameTimeManager::REAL_TIME_TO_GAME_TIME * AGameTimeManager::DAY_START_SECONDS )
		return;

	TFLOAT fDiff = TMath::Abs( pTime->GetDayTime() - fHostTime );
	fDiff        = TMath::Min( fDiff, fDayLength - fDiff ); // around midnight

	if ( fDiff <= CLOCK_TOLERANCE )
	{
		s_bToldClockWaits = TFALSE;
		return;
	}

	if ( AQuestManager::GetSingleton()->GetNumAssignedQuests() > 0 )
	{
		if ( s_bLog && !s_bToldClockWaits )
			TINFO( "[coopstory] clock: %.0f minutes apart from the host's, it follows once the quest '%s' is over\n", fDiff * AGameTimeManager::GAME_TIME_TO_REAL_TIME / 60.0f, AQuestManager::GetSingleton()->GetAssignedQuest( 0 )->GetQuest()->GetName().GetString() );

		s_bToldClockWaits = TTRUE;
		return;
	}

	const TINT                       iMinutes   = TINT( fHostTime * AGameTimeManager::GAME_TIME_TO_REAL_TIME / 60.0f );
	const TINT                       iHour      = ( iMinutes / 60 ) % 24;
	const TINT                       iMinute    = iMinutes % 60;
	const TINT                       iOldHour   = pTime->GetCurrentHour();
	const TINT                       iOldMinute = pTime->GetCurrentMinute();
	const AGameTimeManager::DAYPHASE eOldPhase  = pTime->GetCurrentDayPhase();
	const AGameTimeManager::DAYPHASE eNewPhase  = GetDayPhaseAt( ( iHour * 60 + iMinute ) * 60.0f * AGameTimeManager::REAL_TIME_TO_GAME_TIME, eOldPhase );

	// From the night into the day: the dawn comes first (the animals and the quests of the day, AQuestManager::OnDateChange)
	if ( eOldPhase == AGameTimeManager::DAYPHASE_NIGHT && eNewPhase != AGameTimeManager::DAYPHASE_NIGHT )
		pTime->SetTime( 6, 0, 0, TFALSE, TTRUE );

	// Another day phase is told (the sky, the animals and the quests of the phase), the same one isn't: its event would
	// repopulate the farm
	pTime->SetTime( TUINT8( iHour ), TUINT8( iMinute ), 0, TFALSE, eNewPhase != pTime->GetCurrentDayPhase() );
	s_bToldClockWaits = TFALSE;

	if ( s_bLog )
		TINFO( "[coopstory] clock: follows the host's, %d:%02d -> %d:%02d (day phase %d -> %d), %.0f minutes apart; date %d, the host's %d\n", iOldHour, iOldMinute, iHour, iMinute, eOldPhase, pTime->GetCurrentDayPhase(), fDiff * AGameTimeManager::GAME_TIME_TO_REAL_TIME / 60.0f, pTime->GetDay(), s_oRemote.uiDay );

	TWCHAR wszLine[ LINE_SIZE ];
	AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_CLOCK_FOLLOWS_2 ), iHour, iMinute );
	ShowNotice( AModStrings::Get( MODSTRING_COOPSTORY_CLOCK_FOLLOWS ), wszLine, 5.0f );
	RequestShot( "clock", 2.0f );
}

//-----------------------------------------------------------------------------
// Shared steps: the same quest going on in both games
//-----------------------------------------------------------------------------

static TFLOAT s_fStepsWait      = 0.0f;
static TINT   s_iStepsInstance  = -1; // the quest instance (AQuestInstance::GetId) of s_uiStepsTold
static TUINT  s_uiStepsTold     = 0;  // the steps of it the player has been told about

// The steps (quest elements) the other player has done in the same quest are done here too: the scripted steps
// ("dummy": a cow woken up, a friend found) and the places to find ("locate"), not the items to give ("gift": the items
// are in the other game) nor the minigames ("gameevent"). Never the last open step: the quest ends by playing it or by a
// skip. A step done here plays what its end does (AQuestInstance::SetElementState, as the "questelement" scripts)
static void UpdateSharedSteps( TFLOAT a_fDeltaTime )
{
	if ( ( s_fStepsWait -= a_fDeltaTime ) > 0.0f )
		return;

	s_fStepsWait = 0.5f;

	if ( ( s_ePlace != PLACE_FARM && s_ePlace != PLACE_BARN ) || s_fSafeTime < ACoopProgress::SETTLE_TIME || s_iNumCatchUp > 0 || !s_bLocalValid || !IsRemoteUsable() )
		return;

	AQuestInstance* pInstance = AQuestManager::GetSingleton()->GetAssignedQuest( 0 );

	if ( !pInstance || pInstance->GetState() != AQuestInstance::STATE_RUNNING || pInstance->GetQuestIndex() != s_oRemote.iAssignedQuest ||
	     s_oRemote.uiAssignedState != AQuestInstance::STATE_RUNNING || s_oRemote.uiAssignedDone == 0 )
		return;

	if ( pInstance->GetId() != s_iStepsInstance )
	{
		s_iStepsInstance = pInstance->GetId();
		s_uiStepsTold    = 0;
	}

	AQuest*    pQuest       = pInstance->GetQuest();
	const TINT iNumElements = TMath::Min( pQuest->GetNumElements(), 16 );
	TINT       iNumOpen     = 0;

	for ( TINT k = 0; k < iNumElements; k++ )
	{
		if ( !pInstance->IsElementDone( k ) )
			iNumOpen++;
	}

	for ( TINT k = 0; k < iNumElements && iNumOpen > 1; k++ )
	{
		if ( pInstance->IsElementDone( k ) || !( s_oRemote.uiAssignedDone & ( 1 << k ) ) )
			continue;

		AQuestElement* pElement = pQuest->GetElement( k );

		if ( !pElement->IsA( &TGetClass( ADummyQuestElement ) ) && !pElement->IsA( &TGetClass( ALocateQuestElement ) ) )
			continue;

		// Done once it has run its end (a frame or a scene later), asked again until then
		pInstance->SetElementState( k, AQuestInstance::ELEMENTSTATE_COMPLETE, TTRUE );
		iNumOpen--;

		if ( !( s_uiStepsTold & ( 1 << k ) ) )
		{
			s_uiStepsTold |= 1 << k;

			if ( s_bLog )
				TINFO( "[coopstory] shared step: '%s' step %d (%s) was done by the partner, it's done here too (%d of %d open)\n", pQuest->GetName().GetString(), k + 1, pElement->GetClass()->GetName(), iNumOpen, iNumElements );

			TWCHAR wszLine1[ LINE_SIZE ];
			TWCHAR wszLine2[ LINE_SIZE ];
			AModStrings::Format( wszLine1, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_STEP ), GetWideName( pQuest->GetName().GetString() ) );
			AModStrings::Format( wszLine2, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_STEP_2 ), iNumElements - iNumOpen, iNumElements );
			ShowNotice( wszLine1, wszLine2, 4.0f );
			RequestShot( "step", 1.0f );
		}
	}
}

static void OnProgressData( const void* a_pData, TINT a_iSize )
{
	// A save message
	if ( a_iSize == TINT( sizeof( SaveMessage ) ) )
	{
		SaveMessage oMessage;
		TUtil::MemCopy( &oMessage, a_pData, sizeof( oMessage ) );

		if ( oMessage.uiMagic == SAVE_MAGIC )
			OnSaveMessage( oMessage );

		return;
	}

	if ( a_iSize != TINT( sizeof( ProgressMessage ) ) )
	{
		static TBOOL s_bTold = TFALSE;

		if ( !s_bTold && s_bLog )
			TWARN( "[coopstory] the other game's progress has %d bytes instead of %d (another version of the game): ignored\n", a_iSize, TINT( sizeof( ProgressMessage ) ) );

		s_bTold = TTRUE;
		return;
	}

	ProgressMessage oMessage;
	TUtil::MemCopy( &oMessage, a_pData, sizeof( oMessage ) );

	if ( oMessage.uiMagic != PROGRESS_MAGIC )
		return;

	// Not an older one than the last (the counter wraps around, and restarts with the other game)
	const DWORD uiNow = GetTickCount();

	if ( s_bHasRemote && TINT16( oMessage.uiSequence - s_oRemote.uiSequence ) <= 0 && ( uiNow - s_uiRemoteTime ) < 2000 )
		return;

	const TBOOL bFirst   = !s_bHasRemote;
	const TBOOL bChanged = bFirst || oMessage.uiAct != s_oRemote.uiAct || oMessage.uiTier != s_oRemote.uiTier ||
	    oMessage.iAssignedQuest != s_oRemote.iAssignedQuest || oMessage.uiAssignedState != s_oRemote.uiAssignedState ||
	    ( oMessage.uiFlags & ~PROGRESSFLAGS_SAFE ) != ( s_oRemote.uiFlags & ~PROGRESSFLAGS_SAFE ) ||
	    TUtil::MemCompare( oMessage.auiCompleted, s_oRemote.auiCompleted, sizeof( oMessage.auiCompleted ) ) != 0;

	if ( s_bLog && PrepareQuestData() )
	{
		if ( oMessage.uiDataChecksum != s_uiDataChecksum && ( bFirst || s_oRemote.uiDataChecksum != oMessage.uiDataChecksum ) )
			TWARN( "[coopstory] the other game's quest data differs (checksum %08x, %d tiers; here %08x, %d tiers): nothing can be skipped\n", oMessage.uiDataChecksum, oMessage.uiNumTiers, s_uiDataChecksum, s_iNumTiers );

		if ( bChanged && oMessage.uiDataChecksum == s_uiDataChecksum )
		{
			TINFO( "[coopstory] received #%d\n", oMessage.uiSequence );
			LogProgress( "partner", oMessage );

			// What the other player completed since the last message
			for ( TINT i = 0; !bFirst && i < s_iNumQuests; i++ )
			{
				if ( IsQuestDoneIn( oMessage, i ) && !IsQuestDoneIn( s_oRemote, i ) )
					TINFO( "[coopstory] partner completed '%s' (quest %d, tier %d)\n", GetQuestName( i ), i, s_aiQuestTier[ i ] );
			}
		}
	}

	if ( !bFirst && oMessage.uiDataChecksum != s_uiDataChecksum && s_oRemote.uiDataChecksum == s_uiDataChecksum && PrepareQuestData() )
		ShowNotice( AModStrings::Get( MODSTRING_COOPSTORY_DATA_DIFFERS ), AModStrings::Get( MODSTRING_COOPSTORY_DATA_DIFFERS_2 ), 8.0f );

	s_oRemote      = oMessage;
	s_bHasRemote   = TTRUE;
	s_uiRemoteTime = uiNow;
}

static void OnSkipData( const void* a_pData, TINT a_iSize )
{
	if ( a_iSize != TINT( sizeof( SkipMessage ) ) )
		return;

	SkipMessage oMessage;
	TUtil::MemCopy( &oMessage, a_pData, sizeof( oMessage ) );

	if ( oMessage.uiMagic != SKIP_MAGIC || oMessage.uiNumQuests == 0 || oMessage.uiNumQuests > MAX_SKIP_QUESTS )
		return;

	if ( s_bHeardSkip && oMessage.uiId == s_uiLastHeardSkip )
		return;

	s_bHeardSkip      = TTRUE;
	s_uiLastHeardSkip = oMessage.uiId;

	if ( !PrepareQuestData() || oMessage.auiQuests[ 0 ] >= s_iNumQuests )
		return;

	TWCHAR wszLine1[ LINE_SIZE ];
	TWCHAR wszLine2[ LINE_SIZE ];

	if ( oMessage.uiNumQuests > 1 )
		AModStrings::Format( wszLine1, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_SKIPPED_MORE ), GetQuestNameWide( oMessage.auiQuests[ 0 ] ), oMessage.uiNumQuests - 1 );
	else
		AModStrings::Format( wszLine1, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_SKIPPED ), GetQuestNameWide( oMessage.auiQuests[ 0 ] ) );

	AModStrings::Format( wszLine2, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_STORY ), oMessage.uiAct, oMessage.uiTier );
	ShowNotice( wszLine1, wszLine2, 6.0f );

	if ( s_bLog )
	{
		for ( TINT i = 0; i < oMessage.uiNumQuests; i++ )
			TINFO( "[coopstory] partner skipped '%s' (quest %d), their story is at act %d tier %d\n", GetQuestName( oMessage.auiQuests[ i ] ), oMessage.auiQuests[ i ], oMessage.uiAct, oMessage.uiTier );
	}
}

//-----------------------------------------------------------------------------
// Sending
//-----------------------------------------------------------------------------

static void SendProgress( TFLOAT a_fDeltaTime )
{
	s_fSendWait -= a_fDeltaTime;

	if ( s_fSendWait > 0.0f || !ACoopNet::IsConnected() || s_uiDataChecksum == 0 )
		return;

	s_fSendWait          = 1.0f / ACoopProgress::SEND_RATE;
	s_oLocal.uiSequence  = ++s_uiSendSequence;

	static ProgressMessage s_oLastLogged;
	static TBOOL           s_bLogged = TFALSE;
	static DWORD           s_uiLastLogTime = 0;

	const TBOOL bSent = ACoopNet::SendData( ACoopNet::CHANNEL_PROGRESS, &s_oLocal, sizeof( s_oLocal ) );

	if ( !s_bLog )
		return;

	// Every change, and every 10 seconds
	const TBOOL bChanged = !s_bLogged || s_oLastLogged.uiAct != s_oLocal.uiAct || s_oLastLogged.uiTier != s_oLocal.uiTier ||
	    s_oLastLogged.iAssignedQuest != s_oLocal.iAssignedQuest || s_oLastLogged.uiAssignedState != s_oLocal.uiAssignedState ||
	    ( s_oLastLogged.uiFlags & ~PROGRESSFLAGS_SAFE ) != ( s_oLocal.uiFlags & ~PROGRESSFLAGS_SAFE ) ||
	    TUtil::MemCompare( s_oLastLogged.auiCompleted, s_oLocal.auiCompleted, sizeof( s_oLocal.auiCompleted ) ) != 0;

	if ( bChanged || GetTickCount() - s_uiLastLogTime >= 10000 )
	{
		TINFO( "[coopstory] sent #%d (%d bytes)%s\n", s_oLocal.uiSequence, TINT( sizeof( s_oLocal ) ), bSent ? "" : ", not delivered" );

		if ( s_oLocal.uiFlags & PROGRESSFLAGS_IN_STORY )
			LogProgress( "me", s_oLocal );
		else
			TINFO( "[coopstory] me: %s\n", GetWhere( s_oLocal ) );

		s_oLastLogged   = s_oLocal;
		s_bLogged       = TTRUE;
		s_uiLastLogTime = GetTickCount();
	}
}

static void SendSkipCopies( TFLOAT a_fDeltaTime )
{
	if ( s_iSkipCopiesLeft <= 0 )
		return;

	s_fSkipCopyWait -= a_fDeltaTime;

	if ( s_fSkipCopyWait > 0.0f )
		return;

	s_fSkipCopyWait = 0.25f;
	s_iSkipCopiesLeft--;
	ACoopNet::SendData( ACoopNet::CHANNEL_SKIP, &s_oSkipMessage, sizeof( s_oSkipMessage ) );
}

//-----------------------------------------------------------------------------
// ACoopProgress
//-----------------------------------------------------------------------------

// Where a skip can be applied now (PLACE_NONE while something is going on, a_ppOutReason receives what)
static PLACE GetSafePlace( const TCHAR** a_ppOutReason )
{
	const TCHAR* szReason = TNULL;
	PLACE        ePlace   = PLACE_FARM;

	if ( !AQuestManager::IsSingletonCreated() || !ARootTask::IsSingletonCreated() || !AGameStateController::IsSingletonCreated() ||
	     !APlayerManager::IsSingletonCreated() || !AMiniGameManager::IsSingletonCreated() )
		szReason = "the game isn't up";
	else if ( !AGameLoader::IsStoryMode() )
		szReason = "not in a story game";
	else if ( ARootTask::GetSingleton()->IsTransitioning() || AGameLoader::IsChangingTerrain() || AGameLoader::IsReturningToFrontEnd() || AGameLoader::GetScriptedSequence() != -1 )
		szReason = "loading";
	else if ( AGameLoader::IsInMiniGame() )
		ePlace = PLACE_MINIGAME;
	else if ( AGameLoader::IsBarnExploreMiniGame() )
		ePlace = PLACE_BARN;
	else if ( !AGameLoader::IsMainGameLoaded() || AGameLoader::IsGameStateLibraryLoaded() )
		szReason = "not on the farm";

	if ( !szReason && ARootTask::GetSingleton()->IsPaused() )
		szReason = "paused";

	AGameStateController* pController = szReason ? TNULL : AGameStateController::GetSingleton();

	if ( pController )
	{
		const AGameStateController::FLAGS uiBusyFlags =
		    AGameStateController::FLAGS_IGNORE_INPUT | AGameStateController::FLAGS_AWAITING_TRANSITION | AGameStateController::FLAGS_FADING_IN |
		    AGameStateController::FLAGS_RETURN_TO_FRONTEND | AGameStateController::FLAGS_START_NEW_GAME | AGameStateController::FLAGS_TRANSITION_TO_MINIGAME |
		    AGameStateController::FLAGS_LEAVE_SCRIPTED_SEQUENCE | AGameStateController::FLAGS_LOAD_SAVED_GAME | AGameStateController::FLAGS_REMOVE_STATE |
		    AGameStateController::FLAGS_RESTART_MINIGAME;

		AGameState* pState = ( pController->GetNumStates() > 0 ) ? pController->GetCurrentState() : TNULL;

		if ( pController->GetFlags() & uiBusyFlags )
			szReason = "the screen fades";
		else if ( !pState )
			szReason = "no game state";
		else if ( ePlace == PLACE_MINIGAME )
		{
			// The game itself, not its instructions, menu or results
			if ( !pState->IsA( &TGetClass( AMiniGameState ) ) )
				szReason = "the minigame shows its instructions, a menu or its results";
			else if ( !TSTATICCAST( AMiniGameState, pState )->CanEndAsWon() )
				szReason = "the minigame isn't running";
		}
		else if ( pState->GetClass() != ( ( ePlace == PLACE_BARN ) ? &TGetClass( ABarnExploreMinigameState ) : &TGetClass( ASimState ) ) )
			szReason = "a menu, a conversation or a scene is open";
		else if ( pState->GetNumChildStates() != 0 )
		{
			// Named for the log
			static TCHAR s_szMicroGame[ 96 ];
			AGameState*  pChild = pState->FindChildState( &TGetClass( AGameState ) );
			TStringManager::String8Format( s_szMicroGame, sizeof( s_szMicroGame ), "a micro game runs (%s)", pChild ? pChild->GetClass()->GetName() : "?" );
			szReason = s_szMicroGame;
		}
	}

	// In a minigame AQuestManager doesn't have the story (it's reset by the loading, the farm brings it back)
	if ( !szReason && ePlace != PLACE_MINIGAME )
	{
		AQuestManager* pQuestManager = AQuestManager::GetSingleton();
		AUnit*         pPlayer       = APlayerManager::GetSingleton()->GetPlayerUnit( 0 );

		if ( pQuestManager->IsGameComplete() )
			szReason = "the story is over";
		else if ( pQuestManager->IsTimerPaused() )
			szReason = "a quest scene runs";
		else if ( !AMiniGameManager::GetSingleton()->GetNextMiniGameName().IsEmpty() )
			szReason = "a minigame is about to start";
		else if ( !pPlayer )
			szReason = "there's no player";
		else if ( pPlayer->HasInteraction() )
			szReason = "the player is busy with an interaction";

		// The quest being done here mustn't be in one of its own scenes
		for ( TINT i = 0; !szReason && i < pQuestManager->GetNumAssignedQuests(); i++ )
		{
			AQuestInstance* pInstance = pQuestManager->GetAssignedQuest( i );
			AQuest*         pQuest    = pInstance->GetQuest();

			if ( pInstance->GetState() != AQuestInstance::STATE_RUNNING )
			{
				szReason = "the quest being done is starting or ending";
				break;
			}

			for ( TINT k = 0; k < pQuest->GetNumElements(); k++ )
			{
				const AQuestElement::STATE eState = pQuest->GetElement( k )->GetProgress( pInstance->GetData() )->eState;

				if ( eState == AQuestElement::STATE_STARTSEQ || eState == AQuestElement::STATE_ENDSEQ || eState == AQuestElement::STATE_FAILSEQ )
				{
					szReason = "a scene of the quest being done runs";
					break;
				}
			}
		}
	}

	if ( a_ppOutReason )
		*a_ppOutReason = szReason;

	return szReason ? PLACE_NONE : ePlace;
}

TBOOL ACoopProgress::IsSafeMoment( const TCHAR** a_ppOutReason )
{
	const PLACE ePlace = GetSafePlace( a_ppOutReason );

	if ( ePlace == PLACE_MINIGAME && a_ppOutReason )
		*a_ppOutReason = "in a minigame";

	return ePlace == PLACE_FARM || ePlace == PLACE_BARN;
}

const TWCHAR* ACoopProgress::GetSkipBlockReason( TINT a_iQuest )
{
	if ( !PrepareQuestData() || a_iQuest < 0 || a_iQuest >= s_iNumQuests || s_aiQuestTier[ a_iQuest ] <= 0 )
		return AModStrings::Get( MODSTRING_COOPSTORY_CANT_SKIP_NOT_STORY );

	if ( s_aiQuestTier[ a_iQuest ] >= s_iFinalTier )
		return AModStrings::Get( MODSTRING_COOPSTORY_CANT_SKIP_END );

	return TNULL;
}

// The items in the player's inventory (the logs show what a skip took and gave)
static TINT CountPlayerItems()
{
	AUnit*                        pPlayer    = APlayerManager::IsSingletonCreated() ? APlayerManager::GetSingleton()->GetPlayerUnit( 0 ) : TNULL;
	AInventoryComponentInterface* pInventory = pPlayer ? AInventoryComponentInterface::GetInventory( pPlayer ) : TNULL;

	return pInventory ? pInventory->GetNumItems() : -1;
}

// What the end scene of Sleepy Cows (sleepy_cows_complete_seq) and AQuestInstance do once its last cow is up: the player
// can get on the bike and move again
static void FreePlayerFromSleepyCows()
{
	AMiscFunctions::GetSingleton()->PlayerCanGetOnBike( TTRUE );

	AUnit*                   pPlayer        = APlayerManager::GetSingleton()->GetPlayerUnit( 0 );
	APlayerControlComponent* pPlayerControl = pPlayer ? TSTATICCAST( APlayerControlComponent, pPlayer->FindComponent( &TGetClass( APlayerControlComponent ) ) ) : TNULL;

	if ( pPlayerControl )
	{
		pPlayerControl->SetMovementEnabled( TTRUE );
		pPlayerControl->SetInventoryDisabled( TFALSE );
		ACheatActivator::ResetRequirement( ACheatActivator::GetSingleton()->GetRequirement( ACheatActivator::REQUIREMENT_ROCK_CLOSE_TO_SECONDARY ) );
	}

	if ( s_bLog )
		TINFO( "[coopstory] '%s': the player can get on the bike and move again (what its end scene does)\n", s_szSleepyCows );
}

// The quest a_iQuest is going on here and is about to be skipped: what the game does when a quest is given up
// (AQuestManager::AbandonQuest), while it's still going on: its "<quest>_uninit" script runs (it takes the quest's items,
// detaches Pip from the player...) and the count of its items is hidden. The quest is completed a moment later
// (UpdateCatchUp), once the script has run
static void PrepareAssignedSkip( TINT a_iQuest )
{
	AQuest*      pQuest = AQuestManager::GetSingleton()->GetQuest( a_iQuest );
	const TCHAR* szName = pQuest->GetName().GetString();

	if ( pQuest->GetName() == TPString8( s_szBarnDetailing ) )
	{
		// Its script is for a quest given up: it takes the bought barn items back. Only Joey's question mark goes
		AUnit* pJoey = AUnitNameManager::GetSingleton()->RetrieveByName( TPString8( "joey" ) );

		if ( pJoey )
		{
			AAnimalPopulationManager::GetSingleton()->RemoveMarker( pJoey );

			ASimMiniMap* pMiniMap = AHudManager::GetSingleton()->FindElement<ASimMiniMap>();

			if ( pMiniMap )
				pMiniMap->RemoveUnitIcon( pJoey->GetSelfProxy() );
		}

		if ( s_bLog )
			TINFO( "[coopstory] '%s' is going on here: its _uninit script isn't run (it takes the bought barn items back), Joey's question mark %s\n", szName, pJoey ? "is removed" : "isn't there" );
	}
	else
	{
		TCHAR szUninit[ 128 ];
		TStringManager::String8Format( szUninit, sizeof( szUninit ), "%s_uninit", szName );

		const TINT            iItems    = CountPlayerItems();
		AInteractionSequence* pSequence = AInteractionManager::GetSingleton()->AssignInteractionToAllUnits( TPString8( szUninit ) );

		if ( s_bLog )
			TINFO( "[coopstory] '%s' is going on here: its script %s %s (%d items)\n", szName, szUninit, pSequence ? "runs" : "isn't there or has nothing to do", iItems );
	}

	AItemCountHudElement* pItemCount = AHudManager::GetSingleton()->FindElement<AItemCountHudElement>();

	if ( pItemCount )
		pItemCount->SetVisibleForce( TFALSE );
}

TBOOL ACoopProgress::CompleteQuest( TINT a_iQuest, const TCHAR* a_szReason )
{
	if ( !PrepareQuestData() || a_iQuest < 0 || a_iQuest >= s_iNumQuests || s_aiQuestTier[ a_iQuest ] <= 0 )
		return TFALSE;

	AQuestManager*            pQuestManager = AQuestManager::GetSingleton();
	AQuestManager::QuestTier* pTier         = pQuestManager->GetQuestTier( s_aiQuestTier[ a_iQuest ] );
	const TINT                iSlot         = s_aiQuestSlot[ a_iQuest ];

	if ( pTier->uiCompletedMask & ( 1 << iSlot ) )
		return TFALSE;

	AQuest*         pQuest    = pQuestManager->GetQuest( a_iQuest );
	AQuestInstance* pInstance = pQuestManager->FindAssignedQuest( pQuest->GetName(), 1 );
	const TINT      iOldAct   = pQuestManager->GetAct();
	const TINT      iOldTier  = pQuestManager->GetTier();

	if ( pInstance )
	{
		// The quest event of RemoveQuest is the giver's: without the giver (not spawned now) it takes the type of its
		// reference, which a quest assigned without its giver doesn't have.
		// NOTE: the event saves the name of that type, a missing one crashed the next save (strlen of TNULL)
		if ( !pInstance->GetQuestGiver() && !pInstance->GetQuestGiverRef()->pType )
			pInstance->GetQuestGiverRef()->pType = AUnitManager::GetSingleton()->FindUnit( pQuest->GetQuestGiver().GetString() );

		// Assigned here: it ends like a quest that is complete (AQuestManager::OnUpdate), its end scenes don't play
		pQuestManager->RemoveQuest( pInstance, TTRUE, TTRUE );
	}
	else
	{
		// What AQuestManager::RemoveQuest leaves of a completed quest: completed in its tier...
		pTier->uiCompletedMask |= TUINT8( 1 << iSlot );

		// ...its event, the "X Incomplete" requirements of the scripts check for it. The giver may not be spawned,
		// its type is enough (as AGameLoader's story.goto does).
		// NOTE: only with the type: the event saves the name of the giver's type (a missing name would crash the save)
		AUnitType*              pGiverType = AUnitManager::GetSingleton()->FindUnit( pQuest->GetQuestGiver().GetString() );
		AInteractionQuestEvent* pEvent     = pGiverType ? TSTATICCAST( AInteractionQuestEvent, AInteractionEventManager::GetSingleton()->NewActiveEvent( &TGetClass( AInteractionQuestEvent ) ) ) : TNULL;

		if ( pEvent )
		{
			pEvent->SetQuest( pQuest );
			pEvent->SetCompleted( TTRUE );

			AUnitRef oGiver;
			oGiver.pType = pGiverType;
			pEvent->SetUnitRef( &oGiver );
		}
		else
		{
			TWARN( "[coopstory] no quest event for '%s': %s\n", pQuest->GetName().GetString(), pGiverType ? "no free event" : "its giver isn't a unit type here" );
		}

		// ...the giver doesn't offer it any more (AQuestInstance::Update does it when a quest starts)...
		APlayerProfileManager::GetSingleton()->GetMainProfile()->ReleaseItem( pQuest->GetQuestGiver() );

		// ...and its spawns are gone
		AAnimalPopulationManager::GetSingleton()->ResetExclamationView( TTRUE );
		AAnimalPopulationManager::GetSingleton()->ClearQuestTier( s_aiQuestTier[ a_iQuest ], pQuest->GetName().GetString(), 0 );

		// The next tier once the current one is complete (AQuestManager::RemoveQuest does it for the last quest of a tier)
		const TUINT8 uiAllQuests = TUINT8( ( 1 << pTier->uiNumQuests ) - 1 );

		if ( pTier == pQuestManager->GetCurrentQuestTier() && ( pTier->uiCompletedMask & uiAllQuests ) == uiAllQuests )
		{
			if ( pQuestManager->GetNumAssignedQuests() == 0 )
			{
				pQuestManager->AdvanceQuestTier( 1 );

				if ( iOldAct < pQuestManager->GetAct() || iOldTier < pQuestManager->GetTier() )
					FIRE_EVENT( AQuestManager::ProgressAdvanced, iOldAct, iOldTier, pQuestManager->GetAct(), pQuestManager->GetTier() );
			}
			else
			{
				TWARN( "[coopstory] the tier of '%s' is complete but '%s' is assigned: the story stays at act %d tier %d\n", pQuest->GetName().GetString(), pQuestManager->GetAssignedQuest( 0 )->GetQuest()->GetName().GetString(), pQuestManager->GetAct(), pQuestManager->GetTier() );
			}
		}
	}

	s_bStoryChanged = TTRUE;

	// In the barn the farm (its animals, the player's squirt, the gates) is in its quick save: the farm clears the quest's
	// spawns and gives what it keeps of the rewards once it's back (UpdateFarmCleanUp)
	const TBOOL bInBarn = AGameLoader::IsBarnExploreMiniGame();

	if ( bInBarn && s_iNumFarmCleanUp < TARRAYSIZE( s_aiFarmCleanUp ) )
		s_aiFarmCleanUp[ s_iNumFarmCleanUp++ ] = a_iQuest;

	// What the quest gives for good
	const TINT iNumRewards = AGameLoader::GiveQuestRewards( pQuest->GetName().GetString(), bInBarn ? AGameLoader::REWARDS_PROFILE : AGameLoader::REWARDS_ALL );

	// What the end scene of Sleepy Cows does (the sleeping cows lock the bike and, the last one, the player)
	if ( pQuest->GetName() == TPString8( s_szSleepyCows ) )
		FreePlayerFromSleepyCows();

	// What AQuestManager::OnUpdate does once a quest is complete
	pQuestManager->SetBannerState( AQuestManager::BANNERSTATE_COMPLETE );
	pQuestManager->PlayQuestCompleteMusic();
	ANPCBehaviourManager::GetSingleton()->RequestScheduleUpdate();
	AReplayManager::GetSingleton()->UpdateReplays();

	if ( s_bLog )
		TINFO( "[coopstory] completed '%s' (quest %d, %s, %s here, %s, %d rewards, %d items): act %d tier %d -> act %d tier %d (abs %d)%s\n", pQuest->GetName().GetString(), a_iQuest, a_szReason, pInstance ? "assigned" : "not assigned", bInBarn ? "in the barn" : "on the farm", iNumRewards, CountPlayerItems(), iOldAct, iOldTier, pQuestManager->GetAct(), pQuestManager->GetTier(), pQuestManager->GetCurAbsQuestTier(), ( AGameLoader::GetScriptedSequence() != -1 ) ? ", the scene of the tier starts" : "" );

	return TTRUE;
}

void ACoopProgress::Dev_LogStory( const TCHAR* a_szTag )
{
#ifndef TOSHI_FINAL
	if ( !PrepareQuestData() )
		return;

	AQuestManager* pQuestManager = AQuestManager::GetSingleton();

	// The completed quests by their index (the same in both games when the checksum is)
	TCHAR szDone[ 600 ];
	TINT  iLength  = 0;
	TINT  iNumDone = 0;
	szDone[ 0 ]    = '\0';

	for ( TINT iTier = 1; iTier <= s_iNumTiers; iTier++ )
	{
		const AQuestManager::QuestTier* pTier = pQuestManager->GetQuestTier( iTier );

		for ( TINT k = 0; k < pTier->uiNumQuests; k++ )
		{
			if ( !( pTier->uiCompletedMask & ( 1 << k ) ) )
				continue;

			iNumDone++;

			if ( iLength < TINT( sizeof( szDone ) ) - 8 )
			{
				TStringManager::String8Format( szDone + iLength, sizeof( szDone ) - iLength, "%s%d", ( iLength > 0 ) ? "," : "", pTier->auiQuests[ k ] );
				iLength += TStringManager::String8Length( szDone + iLength );
			}
		}
	}

	AQuestInstance* pInstance = pQuestManager->GetAssignedQuest( 0 );

	TINFO( "[%s] story: act %d tier %d (abs %d), %d quests done [%s], assigned %s, quest data %08x\n", a_szTag, pQuestManager->GetAct(), pQuestManager->GetTier(), pQuestManager->GetCurAbsQuestTier(), iNumDone, szDone, pInstance ? pInstance->GetQuest()->GetName().GetString() : "none", s_uiDataChecksum );

	// The items the quests give for good, and how many items there are
	static const TCHAR* s_aszItems[] = {
		"cookbook", "cocktails", "sunglasses", "scorebook", "bikekey", "goldrush_token", "milkweed_seed",
		"honey_gate_key", "woods_gate_key", "metaldetector", "golf_gate_key", "junkyard_gate_key"
	};

	AUnit*                        pPlayer    = APlayerManager::IsSingletonCreated() ? APlayerManager::GetSingleton()->GetPlayerUnit( 0 ) : TNULL;
	AInventoryComponentInterface* pInventory = pPlayer ? AInventoryComponentInterface::GetInventory( pPlayer ) : TNULL;

	if ( !pInventory )
		return;

	TCHAR szItems[ 400 ];
	iLength       = 0;
	szItems[ 0 ]  = '\0';

	for ( TINT i = 0; i < TARRAYSIZE( s_aszItems ); i++ )
	{
		const TINT iCount = pInventory->GetNumItemsOfType( TPString8( s_aszItems[ i ] ) );

		if ( iCount > 0 && iLength < TINT( sizeof( szItems ) ) - 40 )
		{
			TStringManager::String8Format( szItems + iLength, sizeof( szItems ) - iLength, "%s%s x%d", ( iLength > 0 ) ? ", " : "", s_aszItems[ i ], iCount );
			iLength += TStringManager::String8Length( szItems + iLength );
		}
	}

	TINFO( "[%s] items: %d in all; quest rewards: %s\n", a_szTag, pInventory->GetNumItems(), ( iLength > 0 ) ? szItems : "none" );
#endif
}

//-----------------------------------------------------------------------------
// Offers and skips
//-----------------------------------------------------------------------------

// The minigame of a_iQuest has been skipped: the story completes the quest once it's back (a won minigame), it isn't
// offered meanwhile. After two minutes it is again (the story didn't complete it)
static TBOOL IsBeingWon( TINT a_iQuest, DWORD a_uiNow )
{
	return a_iQuest >= 0 && a_iQuest == s_iWonQuest && ( a_uiNow - s_uiWonTime ) < 120000;
}

static TBOOL IsOffered( TINT a_iQuest, DWORD a_uiNow )
{
	return s_auiAheadSince[ a_iQuest ] != 0 && ( a_uiNow - s_auiAheadSince[ a_iQuest ] ) >= DWORD( s_fSkipDelay * 1000.0f ) && !IsBeingWon( a_iQuest, a_uiNow );
}

// The quest of the minigame being played: the assigned quest of the story (read before the minigame started, see
// IsStoryLive) if one of its elements is this minigame, else -1
static TINT GetMiniGameQuest()
{
	if ( !s_bLocalValid || s_oLocal.iAssignedQuest < 0 || s_oLocal.iAssignedQuest >= s_iNumQuests || !AGameStateController::IsSingletonCreated() )
		return -1;

	AGameStateController* pController = AGameStateController::GetSingleton();
	AGameState*           pState      = ( pController->GetNumStates() > 0 ) ? pController->GetCurrentState() : TNULL;

	if ( !pState || !pState->IsA( &TGetClass( AMiniGameState ) ) )
		return -1;

	AMiniGameManager* pMiniGameManager = AMiniGameManager::GetSingleton();
	const TINT        iMiniGame        = TSTATICCAST( AMiniGameState, pState )->GetMiniGameIndex();

	if ( iMiniGame < 0 || iMiniGame >= pMiniGameManager->GetTotalNumMiniGames() )
		return -1;

	const TCHAR* szMiniGame = pMiniGameManager->GetMiniGame( iMiniGame )->GetName().GetString();
	AQuest*      pQuest     = AQuestManager::GetSingleton()->GetQuest( s_oLocal.iAssignedQuest );

	for ( TINT i = 0; i < pQuest->GetNumElements(); i++ )
	{
		AQuestElement* pElement = pQuest->GetElement( i );

		// As AGameEventQuestElement::OnMiniGameResult compares them
		if ( pElement->IsA( &TGetClass( AGameEventQuestElement ) ) &&
		     TStringManager::String8CompareNoCase( TSTATICCAST( AGameEventQuestElement, pElement )->GetMiniGame().GetString(), szMiniGame ) == 0 )
			return s_oLocal.iAssignedQuest;
	}

	return -1;
}

// The minigame of a_iQuest ends as won (PLACE_MINIGAME), the story completes the quest once it's back
static void SkipMiniGame( TINT a_iQuest, const TCHAR* a_szHow )
{
	AMiniGameState* pMiniGame = TSTATICCAST( AMiniGameState, AGameStateController::GetSingleton()->GetCurrentState() );
	pMiniGame->EndAsWon();

	s_iWonQuest = a_iQuest;
	s_uiWonTime = GetTickCount();

	// The other game hears about it as about any skip
	s_oSkipMessage                 = {};
	s_oSkipMessage.uiMagic         = SKIP_MAGIC;
	s_oSkipMessage.uiId            = ++s_uiSkipId;
	s_oSkipMessage.uiNumQuests     = 1;
	s_oSkipMessage.auiQuests[ 0 ]  = TUINT8( a_iQuest );
	s_oSkipMessage.uiAct           = s_oLocal.uiAct;
	s_oSkipMessage.uiTier          = s_oLocal.uiTier;
	s_iSkipCopiesLeft              = 3;
	s_fSkipCopyWait                = 0.0f;

	if ( s_bLog )
		TINFO( "[coopstory] skip accepted (%s): the minigame of '%s' (quest %d, partner finished it %.1f s ago) ends as won, the quest completes once the story is back\n", a_szHow, GetQuestName( a_iQuest ), a_iQuest, ( GetTickCount() - s_auiAheadSince[ a_iQuest ] ) / 1000.0f );

	TWCHAR wszLine[ LINE_SIZE ];
	AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_SKIPPED ), GetQuestNameWide( a_iQuest ) );
	ShowNotice( wszLine, AModStrings::Get( MODSTRING_COOPSTORY_COUNTS_AS_WON ), 6.0f );
	RequestShot( "skipped", 0.2f );
}

// The quest of the skipped minigame: completed once the story is back
static void UpdateWonQuest( DWORD a_uiNow )
{
	if ( s_iWonQuest < 0 )
		return;

	if ( s_bLocalValid && ( s_oLocal.uiFlags & PROGRESSFLAGS_LIVE ) && IsQuestDoneIn( s_oLocal, s_iWonQuest ) )
	{
		if ( s_bLog )
			TINFO( "[coopstory] '%s' is complete: the story is back and its minigame was won (%.1f s after the skip)\n", GetQuestName( s_iWonQuest ), ( a_uiNow - s_uiWonTime ) / 1000.0f );

		RequestShot( "after", 4.0f );
		s_iWonQuest = -1;
	}
	else if ( !IsBeingWon( s_iWonQuest, a_uiNow ) )
	{
		if ( s_bLog )
			TINFO( "[coopstory] '%s' isn't complete 2 minutes after its minigame was skipped: it can be skipped again\n", GetQuestName( s_iWonQuest ) );

		s_iWonQuest = -1;
	}
}

// The quests skipped in the barn: their spawns on the farm are cleared and the farm's part of their rewards is given once
// the farm is back and has settled
static void UpdateFarmCleanUp()
{
	if ( s_iNumFarmCleanUp == 0 )
		return;

	if ( !AGameLoader::IsStoryMode() )
	{
		s_iNumFarmCleanUp = 0;
		return;
	}

	if ( s_ePlace != PLACE_FARM || s_fSafeTime < ACoopProgress::SETTLE_TIME )
		return;

	AAnimalPopulationManager* pPopulation = AAnimalPopulationManager::GetSingleton();

	for ( TINT i = 0; i < s_iNumFarmCleanUp; i++ )
	{
		const TINT iQuest = s_aiFarmCleanUp[ i ];
		pPopulation->ClearQuestTier( s_aiQuestTier[ iQuest ], GetQuestName( iQuest ), 0 );
		const TINT iNumRewards = AGameLoader::GiveQuestRewards( GetQuestName( iQuest ), AGameLoader::REWARDS_FARM );

		if ( s_bLog )
			TINFO( "[coopstory] the farm is back: the spawns of '%s' (skipped in the barn) are cleared, %d rewards of the farm given\n", GetQuestName( iQuest ), iNumRewards );
	}

	pPopulation->ResetExclamationView( TTRUE );
	s_iNumFarmCleanUp = 0;
}

// Follows which quests the other player has completed and this one hasn't
static void UpdateAheadTimes( DWORD a_uiNow )
{
	// One of the games went back to the frontend: the next time the stories meet is a new start. (Not when the other
	// game is only out of reach for a while, its loadings stop it from sending)
	const TBOOL bRemoteFresh = ACoopNet::IsConnected() && s_bHasRemote && ( a_uiNow - s_uiRemoteTime ) < DWORD( ACoopProgress::PROGRESS_TIMEOUT * 1000.0f );

	if ( s_bStoriesMet && ( !s_bLocalValid || ( bRemoteFresh && !( s_oRemote.uiFlags & PROGRESSFLAGS_IN_STORY ) ) ) )
	{
		s_bStoriesMet = TFALSE;

		for ( TINT i = 0; i < s_iNumQuests; i++ )
		{
			s_auiAheadSince[ i ] = 0;
			s_abHidden[ i ]      = TFALSE;
		}

		if ( s_bLog )
			TINFO( "[coopstory] %s left the story game\n", s_bLocalValid ? "the partner" : "this game" );
	}

	// While one of the stories isn't known the times wait
	if ( !s_bLocalValid || !IsRemoteUsable() )
		return;

	// What the other player had done before the stories met can be caught up with right away
	const TBOOL bMeeting = !s_bStoriesMet;
	s_bStoriesMet        = TTRUE;

	for ( TINT i = 0; i < s_iNumQuests; i++ )
	{
		const TBOOL bAhead = s_aiQuestTier[ i ] > 0 && IsQuestDoneIn( s_oRemote, i ) && !IsQuestDoneIn( s_oLocal, i );

		if ( bAhead && s_auiAheadSince[ i ] == 0 )
		{
			// NOTE: the times are DWORD differences, a time from before GetTickCount wrapped around works the same
			s_auiAheadSince[ i ] = bMeeting ? a_uiNow - DWORD( s_fSkipDelay * 1000.0f ) : a_uiNow;

			if ( s_auiAheadSince[ i ] == 0 )
				s_auiAheadSince[ i ] = DWORD( -1 );

			if ( s_bLog )
				TINFO( "[coopstory] partner is ahead with '%s' (quest %d, tier %d): %s %.0f s\n", GetQuestName( i ), i, s_aiQuestTier[ i ], bMeeting ? "done before the stories met, it can be caught up with now, not in" : "it can be skipped here in", bMeeting ? 0.0f : s_fSkipDelay );
		}
		else if ( !bAhead && s_auiAheadSince[ i ] != 0 )
		{
			if ( s_bLog )
				TINFO( "[coopstory] '%s' is no longer ahead (%s)\n", GetQuestName( i ), IsQuestDoneIn( s_oLocal, i ) ? "done here too" : "the partner's story doesn't have it any more" );

			s_auiAheadSince[ i ] = 0;
			s_abHidden[ i ]      = TFALSE;
		}
	}
}

// The quests one skip completes now, in the order they're applied: the open quests of the tiers up to the current one,
// then the ones of the next tiers as long as every open quest of a tier is offered and can be skipped (the story then
// moves to the next tier). a_piOutBlocked receives an offered quest of the current tier that can't be skipped, or -1
static TINT BuildSkipPlan( TINT* a_piOut, DWORD a_uiNow, TINT* a_piOutBlocked )
{
	*a_piOutBlocked = -1;

	if ( !s_bLocalValid || !IsRemoteUsable() )
		return 0;

	AQuestManager* pQuestManager = AQuestManager::GetSingleton();
	const TINT     iCurrentTier  = GetAbsTier( s_oLocal );
	TINT           iNumQuests    = 0;

	for ( TINT iTier = 1; iTier <= s_iNumTiers; iTier++ )
	{
		const AQuestManager::QuestTier* pTier     = pQuestManager->GetQuestTier( iTier );
		TBOOL                           bComplete = TTRUE;

		for ( TINT k = 0; k < pTier->uiNumQuests; k++ )
		{
			const TINT iQuest = pTier->auiQuests[ k ];

			if ( iQuest >= s_iNumQuests || IsQuestDoneIn( s_oLocal, iQuest ) )
				continue;

			if ( IsOffered( iQuest, a_uiNow ) && !ACoopProgress::GetSkipBlockReason( iQuest ) )
			{
				a_piOut[ iNumQuests++ ] = iQuest;
			}
			else
			{
				bComplete = TFALSE;

				if ( *a_piOutBlocked < 0 && iTier <= iCurrentTier && IsOffered( iQuest, a_uiNow ) )
					*a_piOutBlocked = iQuest;
			}
		}

		if ( iTier >= iCurrentTier && !bComplete )
			break;
	}

	return iNumQuests;
}

static void StartCatchUp( const TCHAR* a_szHow )
{
	s_iNumCatchUp    = 0;
	s_iNumSkipped    = 0;
	s_iFirstSkipped  = -1;
	s_fCatchUpWait   = 0.0f;
	s_iPreparedQuest = -1;

	for ( TINT i = 0; i < s_iPlanSize; i++ )
	{
		s_abCatchUp[ s_aiPlan[ i ] ] = TTRUE;
		s_iNumCatchUp++;
	}

	if ( s_bLog )
	{
		TINFO( "[coopstory] skip accepted (%s): %d quests\n", a_szHow, s_iNumCatchUp );

		for ( TINT i = 0; i < s_iPlanSize; i++ )
			TINFO( "[coopstory]   '%s' (quest %d, tier %d, partner finished it %.1f s ago)\n", GetQuestName( s_aiPlan[ i ] ), s_aiPlan[ i ], s_aiQuestTier[ s_aiPlan[ i ] ], ( GetTickCount() - s_auiAheadSince[ s_aiPlan[ i ] ] ) / 1000.0f );
	}
}

static void StopCatchUp( const TCHAR* a_szWhy )
{
	if ( s_iNumCatchUp > 0 && s_bLog )
		TINFO( "[coopstory] catching up stopped with %d quests left: %s\n", s_iNumCatchUp, a_szWhy );

	for ( TINT i = 0; i < s_iNumQuests; i++ )
		s_abCatchUp[ i ] = TFALSE;

	s_iNumCatchUp = 0;
}

// Applies the skip the player chose, a tier at a time, at safe moments
static void UpdateCatchUp( TFLOAT a_fDeltaTime )
{
	if ( s_iNumCatchUp <= 0 )
		return;

	// The ones done here meanwhile
	s_iNumCatchUp = 0;

	for ( TINT i = 0; i < s_iNumQuests; i++ )
	{
		if ( s_abCatchUp[ i ] && s_bLocalValid && IsQuestDoneIn( s_oLocal, i ) )
			s_abCatchUp[ i ] = TFALSE;

		if ( s_abCatchUp[ i ] )
			s_iNumCatchUp++;
	}

	if ( s_iNumCatchUp == 0 )
	{
		if ( s_bLog )
			TINFO( "[coopstory] caught up: %d quests skipped\n", s_iNumSkipped );

		return;
	}

	if ( s_fCatchUpWait > 0.0f )
	{
		s_fCatchUpWait -= a_fDeltaTime;
		return;
	}

	// Only on the farm, once it has settled, and while the other game still has them
	if ( s_fSafeTime < ACoopProgress::SETTLE_TIME || !IsRemoteUsable() )
		return;

	AQuestManager* pQuestManager = AQuestManager::GetSingleton();
	const TINT     iCurrentTier  = pQuestManager->GetCurAbsQuestTier();
	TINT           aiQuests[ ACoopProgress::MAX_QUESTS ];
	TINT           iNumQuests    = 0;
	TINT           iAssigned     = -1;

	for ( TINT i = 0; i < s_iNumQuests; i++ )
	{
		if ( !s_abCatchUp[ i ] || s_aiQuestTier[ i ] > iCurrentTier )
			continue;

		if ( !IsQuestDoneIn( s_oRemote, i ) )
		{
			if ( s_bLog )
				TINFO( "[coopstory] '%s' isn't skipped: the partner's story doesn't have it any more\n", GetQuestName( i ) );

			s_abCatchUp[ i ] = TFALSE;
			s_iNumCatchUp--;
			continue;
		}

		AQuestInstance* pInstance = pQuestManager->GetAssignedQuest( 0 );

		if ( pInstance && pInstance->GetQuestIndex() == i )
			iAssigned = i;
		else
			aiQuests[ iNumQuests++ ] = i;
	}

	if ( iNumQuests == 0 && iAssigned < 0 )
	{
		// The rest is in later tiers, but the current one still has quests the skip doesn't complete
		if ( s_iNumCatchUp > 0 )
		{
			TWCHAR wszLine[ LINE_SIZE ];
			AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_STORY_STAYS ), pQuestManager->GetAct(), pQuestManager->GetTier() );
			ShowNotice( AModStrings::Get( MODSTRING_COOPSTORY_CATCHING_UP_STOPPED ), wszLine, 6.0f );
			StopCatchUp( "the current tier has quests the skip doesn't complete" );
		}

		return;
	}

	// The one going on here is undone first (its "_uninit" script needs it to be still going on), it's completed once
	// the script has run
	if ( iAssigned >= 0 && s_iPreparedQuest != iAssigned )
	{
		PrepareAssignedSkip( iAssigned );
		s_iPreparedQuest = iAssigned;
		s_fCatchUpWait   = 1.5f;
		return;
	}

	// The ones that aren't assigned here first: the assigned one completes the tier through AQuestManager::RemoveQuest
	if ( iAssigned >= 0 )
		aiQuests[ iNumQuests++ ] = iAssigned;

	s_iPreparedQuest = -1;

	s_oSkipMessage             = {};
	s_oSkipMessage.uiMagic     = SKIP_MAGIC;
	s_oSkipMessage.uiId        = ++s_uiSkipId;
	s_oSkipMessage.uiNumQuests = 0;

	for ( TINT i = 0; i < iNumQuests; i++ )
	{
		const TINT iQuest = aiQuests[ i ];

		if ( ACoopProgress::CompleteQuest( iQuest, "skipped, the partner had done it" ) )
		{
			if ( s_iFirstSkipped < 0 )
				s_iFirstSkipped = iQuest;

			s_iNumSkipped++;

			if ( s_oSkipMessage.uiNumQuests < MAX_SKIP_QUESTS )
				s_oSkipMessage.auiQuests[ s_oSkipMessage.uiNumQuests++ ] = TUINT8( iQuest );
		}

		s_abCatchUp[ iQuest ] = TFALSE;
		s_iNumCatchUp--;
	}

	s_oSkipMessage.uiAct  = TUINT8( pQuestManager->GetAct() );
	s_oSkipMessage.uiTier = TUINT8( pQuestManager->GetTier() );

	if ( s_oSkipMessage.uiNumQuests > 0 )
	{
		s_iSkipCopiesLeft = 3;
		s_fSkipCopyWait   = 0.0f;
	}

	TWCHAR wszLine1[ LINE_SIZE ];
	TWCHAR wszLine2[ LINE_SIZE ];

	if ( iNumQuests > 1 )
		AModStrings::Format( wszLine1, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_SKIPPED_MORE ), GetQuestNameWide( aiQuests[ 0 ] ), iNumQuests - 1 );
	else
		AModStrings::Format( wszLine1, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_SKIPPED ), GetQuestNameWide( aiQuests[ 0 ] ) );

	if ( s_iNumCatchUp > 0 )
		AModStrings::Format( wszLine2, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_YOUR_STORY_MORE ), pQuestManager->GetAct(), pQuestManager->GetTier(), s_iNumCatchUp );
	else
		AModStrings::Format( wszLine2, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_YOUR_STORY ), pQuestManager->GetAct(), pQuestManager->GetTier() );

	ShowNotice( wszLine1, wszLine2, 6.0f );
	RequestShot( "skipped", 2.0f );
	RequestShot( "after", 9.0f );

	if ( s_iNumCatchUp == 0 && s_bLog )
		TINFO( "[coopstory] caught up: %d quests skipped\n", s_iNumSkipped );

	// The next tier once the story has settled (its scene plays first, if it has one)
	s_fCatchUpWait = 1.0f;
}

// TTRUE if the left stick button does something here (Data/Options/Controls_Win.trb: the bell of the bike, the horn of
// the car of the joy rides), the skip then takes both stick buttons. K is used by no input map
static TBOOL IsLeftStickBound()
{
	if ( !AInputMapManager::IsSingletonCreated() )
		return TFALSE;

	AInputMap* pMap = AInputMapManager::GetSingleton()->GetActiveInputMap();

	if ( !pMap )
		return TFALSE;

	auto& rCommands = pMap->GetCommandMap();
	return rCommands.Find( TInputDeviceController::BUTTON_STICK_LEFT ) != rCommands.End();
}

static TBOOL IsSkipButtonDown( TBOOL a_bBothSticks )
{
	if ( !AInputHandler::IsSingletonCreated() )
		return TFALSE;

	AInputHandler* pInputHandler = AInputHandler::GetSingleton();

	if ( pInputHandler->GetKeyboardDevice() && pInputHandler->GetKeyboardDevice()->IsDown( TInputDeviceKeyboard::KEY_K ) )
		return TTRUE;

	for ( TINT i = 0; i < pInputHandler->GetNumControllers(); i++ )
	{
		TInputDeviceController* pController = pInputHandler->GetController( i );

		if ( pController && pController->IsDown( TInputDeviceController::BUTTON_STICK_LEFT ) &&
		     ( !a_bBothSticks || pController->IsDown( TInputDeviceController::BUTTON_STICK_RIGHT ) ) )
			return TTRUE;
	}

	return TFALSE;
}

static void UpdatePrompt( TFLOAT a_fDeltaTime, DWORD a_uiNow )
{
	const PROMPT ePrevious = s_ePrompt;
	s_ePrompt              = PROMPT_NONE;
	s_iPlanSize            = 0;
	s_iBlocked             = -1;

	const TBOOL   bSafe        = s_fSafeTime > 0.0f;
	const TBOOL   bBothSticks  = IsLeftStickBound();
	const TWCHAR* wszControl   = AModStrings::Get( bBothSticks ? MODSTRING_COOPSTORY_CONTROL_BOTH_STICKS : MODSTRING_COOPSTORY_CONTROL_LEFT_STICK );
	TINT          iMiniGameQuest = -1;

	if ( s_iNumCatchUp > 0 )
	{
		if ( bSafe )
		{
			s_ePrompt = PROMPT_CATCHUP;
			SetLine( s_awszPrompt[ 0 ], AModStrings::Get( MODSTRING_COOPSTORY_CATCHING_UP ) );

			TWCHAR wszLine[ LINE_SIZE ];
			AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( ( s_iNumCatchUp > 1 ) ? MODSTRING_COOPSTORY_MISSIONS_TO_GO : MODSTRING_COOPSTORY_MISSION_TO_GO ), s_iNumCatchUp );
			SetLine( s_awszPrompt[ 1 ], wszLine );
		}
	}
	else if ( s_ePlace == PLACE_MINIGAME )
	{
		// Only the minigame of the assigned quest, once it has been running for a moment; the rest waits for the story
		iMiniGameQuest = ( s_fMiniGameTime >= ACoopProgress::SETTLE_TIME && IsRemoteUsable() ) ? GetMiniGameQuest() : -1;

		if ( iMiniGameQuest >= 0 && IsOffered( iMiniGameQuest, a_uiNow ) && !s_abHidden[ iMiniGameQuest ] )
		{
			TWCHAR wszLine[ LINE_SIZE ];
			AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_FINISHED ), GetQuestNameWide( iMiniGameQuest ) );
			SetLine( s_awszPrompt[ 0 ], wszLine );

			if ( !ACoopProgress::GetSkipBlockReason( iMiniGameQuest ) )
			{
				s_ePrompt = PROMPT_MINIGAME;

				if ( s_fHoldTime > 0.0f && !s_bHoldUsed )
				{
					const TINT iPercent = TMath::Min( TINT( 100.0f * s_fHoldTime / ACoopProgress::HOLD_TIME ), 100 );
					AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_KEEP_HOLDING ), iPercent );
				}
				else
				{
					AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_HOLD_TO_SKIP_MINIGAME ), wszControl );
				}
			}
			else
			{
				s_ePrompt  = PROMPT_BLOCKED;
				s_iBlocked = iMiniGameQuest;
				AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_TAP_TO_HIDE ), ACoopProgress::GetSkipBlockReason( iMiniGameQuest ) );
			}

			SetLine( s_awszPrompt[ 1 ], wszLine );
		}
	}
	else if ( bSafe )
	{
		s_iPlanSize = BuildSkipPlan( s_aiPlan, a_uiNow, &s_iBlocked );

		TBOOL bAllHidden = TTRUE;

		for ( TINT i = 0; i < s_iPlanSize; i++ )
			bAllHidden = bAllHidden && s_abHidden[ s_aiPlan[ i ] ];

		if ( s_iPlanSize > 0 && !bAllHidden )
		{
			s_ePrompt = PROMPT_OFFER;

			// The ones of the current tier
			AQuestManager* pQuestManager = AQuestManager::GetSingleton();
			TINT           iNumNow       = 0;

			for ( TINT i = 0; i < s_iPlanSize; i++ )
			{
				if ( s_aiQuestTier[ s_aiPlan[ i ] ] <= pQuestManager->GetCurAbsQuestTier() )
					iNumNow++;
			}

			TWCHAR wszLine[ LINE_SIZE ];

			// The plan goes on into later tiers: the other player is that far ahead
			if ( s_iPlanSize > iNumNow )
				AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_AHEAD ), s_iPlanSize );
			else if ( iNumNow > 1 )
				AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_FINISHED_MORE ), GetQuestNameWide( s_aiPlan[ 0 ] ), iNumNow - 1 );
			else
				AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_FINISHED ), GetQuestNameWide( s_aiPlan[ 0 ] ) );

			SetLine( s_awszPrompt[ 0 ], wszLine );

			if ( s_fHoldTime > 0.0f && !s_bHoldUsed )
			{
				// The progress of the hold
				const TINT iPercent = TMath::Min( TINT( 100.0f * s_fHoldTime / ACoopProgress::HOLD_TIME ), 100 );
				AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_KEEP_HOLDING ), iPercent );
			}
			else if ( s_iPlanSize > iNumNow )
			{
				AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_HOLD_TO_CATCH_UP ), wszControl, s_iPlanSize );
			}
			else
			{
				AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( ( iNumNow > 1 ) ? MODSTRING_COOPSTORY_HOLD_TO_SKIP_THEM : MODSTRING_COOPSTORY_HOLD_TO_SKIP_IT ), wszControl );
			}

			SetLine( s_awszPrompt[ 1 ], wszLine );
		}
		else if ( s_iPlanSize == 0 && s_iBlocked >= 0 && !s_abHidden[ s_iBlocked ] )
		{
			s_ePrompt = PROMPT_BLOCKED;

			TWCHAR wszLine[ LINE_SIZE ];
			AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_PARTNER_FINISHED ), GetQuestNameWide( s_iBlocked ) );
			SetLine( s_awszPrompt[ 0 ], wszLine );

			AModStrings::Format( wszLine, LINE_SIZE, AModStrings::Get( MODSTRING_COOPSTORY_TAP_TO_HIDE ), ACoopProgress::GetSkipBlockReason( s_iBlocked ) );
			SetLine( s_awszPrompt[ 1 ], wszLine );
		}
	}

	// The skip button: held to skip, tapped to hide the prompt
	const TBOOL bDown = IsSkipButtonDown( bBothSticks );

	if ( s_ePrompt == PROMPT_OFFER || s_ePrompt == PROMPT_MINIGAME || s_ePrompt == PROMPT_BLOCKED )
	{
		if ( bDown && !s_bHoldUsed )
		{
			s_fHoldTime += a_fDeltaTime;

			if ( s_ePrompt != PROMPT_BLOCKED && s_fHoldTime >= ACoopProgress::HOLD_TIME )
			{
				s_bHoldUsed = TTRUE;

				if ( s_ePrompt == PROMPT_MINIGAME )
					SkipMiniGame( iMiniGameQuest, "the skip button" );
				else
					StartCatchUp( "the skip button" );
			}
		}
		else if ( !bDown )
		{
			if ( s_fHoldTime > 0.0f && s_fHoldTime < 0.5f && !s_bHoldUsed )
			{
				// A tap: hidden until the other player completes something else
				if ( s_ePrompt == PROMPT_OFFER )
				{
					for ( TINT i = 0; i < s_iPlanSize; i++ )
						s_abHidden[ s_aiPlan[ i ] ] = TTRUE;
				}
				else if ( s_ePrompt == PROMPT_MINIGAME )
				{
					s_abHidden[ iMiniGameQuest ] = TTRUE;
				}
				else
				{
					s_abHidden[ s_iBlocked ] = TTRUE;
				}

				if ( s_bLog )
					TINFO( "[coopstory] prompt hidden by the player\n" );
			}

			s_fHoldTime = 0.0f;
			s_bHoldUsed = TFALSE;
		}
	}
	else
	{
		// A press that began before the prompt doesn't count
		s_fHoldTime = 0.0f;
		s_bHoldUsed = bDown;
	}

	s_fPromptTime = ( s_ePrompt == ePrevious ) ? s_fPromptTime + a_fDeltaTime : 0.0f;

	// "-coopskip": accepted after a while
	if ( s_ePrompt == PROMPT_OFFER && s_iNumCatchUp == 0 && s_fAutoSkip >= 0.0f && s_fPromptTime >= s_fAutoSkip )
		StartCatchUp( "-coopskip" );
	else if ( s_ePrompt == PROMPT_MINIGAME && s_fAutoSkip >= 0.0f && s_fPromptTime >= s_fAutoSkip && !IsBeingWon( iMiniGameQuest, a_uiNow ) )
		SkipMiniGame( iMiniGameQuest, "-coopskip" );

	if ( s_ePrompt != ePrevious )
	{
		if ( s_bLog )
		{
			TCHAR szLine1[ LINE_SIZE ];
			TCHAR szLine2[ LINE_SIZE ];
			GetLogText( szLine1, s_awszPrompt[ 0 ] );
			GetLogText( szLine2, s_awszPrompt[ 1 ] );

			if ( s_ePrompt == PROMPT_NONE )
				TINFO( "[coopstory] prompt hidden (%s)\n", ( bSafe || s_ePlace == PLACE_MINIGAME ) ? "nothing to skip" : s_szUnsafeWhy );
			else
				TINFO( "[coopstory] prompt shown%s: %s / %s\n", ( s_ePlace == PLACE_MINIGAME ) ? " in the minigame" : ( ( s_ePlace == PLACE_BARN ) ? " in the barn" : "" ), szLine1, szLine2 );
		}

		if ( s_ePrompt == PROMPT_OFFER )
			RequestShot( "prompt", 1.5f );
		else if ( s_ePrompt == PROMPT_MINIGAME )
			RequestShot( "minigame", 1.5f );
		else if ( s_ePrompt == PROMPT_BLOCKED )
			RequestShot( "blocked", 1.5f );
	}
}

// "-coopminigame": the instructions of a minigame are dismissed after a while, as the player would (the minigame starts)
static void UpdateDevInstructions( TFLOAT a_fDeltaTime )
{
	static TFLOAT s_fShown = 0.0f;

	if ( !s_bDevMiniGame || !AGameStateController::IsSingletonCreated() || AGameStateController::GetSingleton()->GetNumStates() <= 0 )
		return;

	AGameState* pState = AGameStateController::GetSingleton()->GetCurrentState();

	if ( !pState->IsA( &TGetClass( AMiniGameInstructionState ) ) || ( AGameStateController::GetSingleton()->GetFlags() & AGameStateController::FLAGS_IGNORE_INPUT ) )
	{
		s_fShown = 0.0f;
		return;
	}

	AMiniGameInstructionState* pInstructions = TSTATICCAST( AMiniGameInstructionState, pState );
	s_fShown += a_fDeltaTime;

	if ( s_fShown >= 3.0f && pInstructions->GetMiniGame() && pInstructions->GetMiniGame()->CanDismissInstructions() )
	{
		if ( s_bLog )
			TINFO( "[coopstory] -coopminigame: the instructions of %s are dismissed\n", pInstructions->GetMiniGame()->GetClass()->GetName() );

		s_fShown = 0.0f;
		pInstructions->Dismiss();
	}
}

// "-coopcomplete", "-coopassign", "-coopsave", "-coopminigame", "-coopgamestate", "-coopfinish": what the player would
// do, at safe moments of the farm (and of the barn)
static void UpdateDevActions( DWORD a_uiNow )
{
	if ( s_iNumDevActions == 0 || s_uiFarmUpTime == 0 || s_fSafeTime < ACoopProgress::SETTLE_TIME )
		return;

	const TFLOAT   fTime         = ( a_uiNow - s_uiFarmUpTime ) / 1000.0f;
	AQuestManager* pQuestManager = AQuestManager::GetSingleton();

	for ( TINT i = 0; i < s_iNumDevActions; i++ )
	{
		DevAction& rAction = s_aDevActions[ i ];

		if ( rAction.bDone || fTime < rAction.fTime )
			continue;

		if ( rAction.eAction == DEVACTION_MINIGAME )
		{
			// As the quest giver's script does once the player has said yes (the quest is assigned first)
			rAction.bDone = TTRUE;

			if ( s_bLog )
				TINFO( "[coopstory] -coopminigame at %.1f s: %s (assigned quest: %s)\n", fTime, rAction.szName, pQuestManager->GetAssignedQuest( 0 ) ? pQuestManager->GetAssignedQuest( 0 )->GetQuest()->GetName().GetString() : "none" );

			AMiniGameManager::GetSingleton()->SetNextMinigame( TPString8( rAction.szName ) );
			break;
		}

		if ( rAction.eAction == DEVACTION_GIVE )
		{
			// A quest item the player would have picked up (the test of a skip that takes it back)
			rAction.bDone = TTRUE;

			AUnit*                        pPlayer    = APlayerManager::GetSingleton()->GetPlayerUnit( 0 );
			AInventoryComponentInterface* pInventory = pPlayer ? AInventoryComponentInterface::GetInventory( pPlayer ) : TNULL;

			if ( pInventory )
				pInventory->AddItems( TPString8( rAction.szName ), 1, AInventoryComponentInterface::ms_strDefaultInventory );

			if ( s_bLog )
				TINFO( "[coopstory] -coopgive at %.1f s: %s (%d of them, %d items)\n", fTime, rAction.szName, pInventory ? pInventory->GetNumItemsOfType( TPString8( rAction.szName ) ) : 0, CountPlayerItems() );

			break;
		}

		if ( rAction.eAction == DEVACTION_TIME )
		{
			// This game's clock somewhere else than the other's (the test of the clock that follows the host)
			rAction.bDone = TTRUE;

			AGameTimeManager*                pTime     = AGameTimeManager::GetSingleton();
			const AGameTimeManager::DAYPHASE eOldPhase = pTime->GetCurrentDayPhase();
			const TINT                       iHour     = TMath::Min( TMath::Max( rAction.iQuest, 0 ), 23 );
			const AGameTimeManager::DAYPHASE eNewPhase = GetDayPhaseAt( iHour * 60 * 60.0f * AGameTimeManager::REAL_TIME_TO_GAME_TIME, eOldPhase );

			pTime->SetTime( TUINT8( iHour ), 0, 0, TFALSE, eNewPhase != eOldPhase );

			if ( s_bLog )
				TINFO( "[coopstory] -cooptime at %.1f s: %d:00 (day phase %d -> %d)\n", fTime, iHour, eOldPhase, pTime->GetCurrentDayPhase() );

			break;
		}

		if ( rAction.eAction == DEVACTION_STEP )
		{
			// As the "questelement" scripts do (AQuestElementActivity)
			rAction.bDone             = TTRUE;
			AQuestInstance* pInstance = pQuestManager->GetAssignedQuest( 0 );

			if ( pInstance && rAction.iQuest >= 1 && rAction.iQuest <= pInstance->GetQuest()->GetNumElements() )
				pInstance->SetElementState( rAction.iQuest - 1, AQuestInstance::ELEMENTSTATE_COMPLETE, TTRUE );

			if ( s_bLog )
				TINFO( "[coopstory] -coopstep at %.1f s: step %d of %s\n", fTime, rAction.iQuest, pInstance ? pInstance->GetQuest()->GetName().GetString() : "(no quest assigned)" );

			break;
		}

		if ( rAction.eAction == DEVACTION_GAMESTATE || rAction.eAction == DEVACTION_FINISH )
		{
			// The barn's doors: in from the farm, out of the barn
			if ( ( rAction.eAction == DEVACTION_GAMESTATE ) != ( s_ePlace == PLACE_FARM ) )
				continue;

			rAction.bDone = TTRUE;

			if ( s_bLog )
				TINFO( "[coopstory] %s at %.1f s%s%s\n", ( rAction.eAction == DEVACTION_GAMESTATE ) ? "-coopgamestate" : "-coopfinish", fTime, ( rAction.eAction == DEVACTION_GAMESTATE ) ? ": " : "", rAction.szName );

			if ( rAction.eAction == DEVACTION_GAMESTATE )
				AGameStateController::GetSingleton()->StartGameState( TPString8( rAction.szName ) );
			else
				AGameStateController::GetSingleton()->FinishGameState();

			break;
		}

		if ( rAction.eAction == DEVACTION_SAVE )
		{
			// The game can't be saved while a quest is going on, the save waits like the player would
			if ( pQuestManager->GetNumAssignedQuests() > 0 || s_bSavingForPartner )
				continue;

			rAction.bDone = TTRUE;

			if ( s_bLog )
				TINFO( "[coopstory] -coopsave at %.1f s: saving into slot %d\n", fTime, APlayerProgress::GetSingleton()->GetCurrentSlotIndex() );

			AGameStateController::GetSingleton()->SaveCurrentGameState( TFALSE, TFALSE, TFALSE );
			RequestShot( "save", 0.5f );
			break;
		}

		// One quest at a time (AQuestManager::MAX_NUM_ASSIGNED_QUESTS): the assignment waits like a quest giver does
		if ( rAction.eAction == DEVACTION_ASSIGN && pQuestManager->GetNumAssignedQuests() >= AQuestManager::MAX_NUM_ASSIGNED_QUESTS )
			continue;

		rAction.bDone = TTRUE;
		TINT iQuest   = rAction.iQuest;

		if ( rAction.eAction == DEVACTION_ASSIGN )
		{
			if ( iQuest < 0 || iQuest >= s_iNumQuests )
				continue;

			// As a quest giver does (AQuestActivity), by the giver if it's spawned (its script name is its type's)
			AQuest*         pQuest    = pQuestManager->GetQuest( iQuest );
			AUnit*          pGiver    = AUnitNameManager::GetSingleton()->RetrieveByName( pQuest->GetQuestGiver() );
			AQuestInstance* pInstance = pQuestManager->AssignQuest( pGiver, pQuest->GetName() );

			if ( s_bLog )
				TINFO( "[coopstory] -coopassign at %.1f s: quest %d '%s' %s (giver %s %s)\n", fTime, iQuest, GetQuestName( iQuest ), pInstance ? "assigned" : "couldn't be assigned", pQuest->GetQuestGiver().GetString(), pGiver ? "spawned" : "not spawned" );

			break;
		}

		if ( iQuest < 0 && pQuestManager->GetAssignedQuest( 0 ) )
			iQuest = pQuestManager->GetAssignedQuest( 0 )->GetQuestIndex();

		if ( iQuest < 0 )
		{
			AQuestManager::QuestTier* pTier = pQuestManager->GetCurrentQuestTier();

			for ( TINT k = 0; k < pTier->uiNumQuests && iQuest < 0; k++ )
			{
				if ( !( pTier->uiCompletedMask & ( 1 << k ) ) && !ACoopProgress::GetSkipBlockReason( pTier->auiQuests[ k ] ) )
					iQuest = pTier->auiQuests[ k ];
			}
		}

		if ( s_bLog )
			TINFO( "[coopstory] -coopcomplete at %.1f s: quest %d '%s'\n", fTime, iQuest, GetQuestName( iQuest ) );

		if ( iQuest >= 0 && ACoopProgress::CompleteQuest( iQuest, "-coopcomplete" ) )
			RequestShot( "complete", 2.0f );

		// One a frame
		break;
	}
}

static void UpdateLog( TFLOAT a_fDeltaTime )
{
	if ( !s_bLog )
		return;

	// Whether the two stories are the same
	static TINT   s_iSync    = -1;
	static TFLOAT s_fLogWait = 0.0f;

	const TBOOL bKnown = s_bLocalValid && IsRemoteUsable();
	TINT        iSync  = -1;

	if ( bKnown )
		iSync = ( s_oLocal.uiAct == s_oRemote.uiAct && s_oLocal.uiTier == s_oRemote.uiTier && TUtil::MemCompare( s_oLocal.auiCompleted, s_oRemote.auiCompleted, sizeof( s_oLocal.auiCompleted ) ) == 0 ) ? 1 : 0;

	s_fLogWait -= a_fDeltaTime;

	if ( iSync != s_iSync || s_fLogWait <= 0.0f )
	{
		s_fLogWait = 10.0f;

		if ( iSync == 1 )
		{
			TINFO( "[coopstory] in sync: act %d tier %d (abs %d), %d quests done in both stories\n", s_oLocal.uiAct, s_oLocal.uiTier, GetAbsTier( s_oLocal ), CountDone( s_oLocal ) );
		}
		else if ( iSync == 0 )
		{
			TINT iMineOnly   = 0;
			TINT iTheirsOnly = 0;

			for ( TINT i = 0; i < s_iNumQuests; i++ )
			{
				if ( IsQuestDoneIn( s_oLocal, i ) && !IsQuestDoneIn( s_oRemote, i ) ) iMineOnly++;
				if ( !IsQuestDoneIn( s_oLocal, i ) && IsQuestDoneIn( s_oRemote, i ) ) iTheirsOnly++;
			}

			TINFO( "[coopstory] not in sync: me act %d tier %d (abs %d), partner act %d tier %d (abs %d); %d quests done only here, %d only there; %s\n", s_oLocal.uiAct, s_oLocal.uiTier, GetAbsTier( s_oLocal ), s_oRemote.uiAct, s_oRemote.uiTier, GetAbsTier( s_oRemote ), iMineOnly, iTheirsOnly, ( s_fSafeTime > 0.0f ) ? "a skip could be applied now" : s_szUnsafeWhy );
		}
		else
		{
			TINFO( "[coopstory] waiting: %s, %s\n", s_bLocalValid ? "my story is known" : "my story isn't known yet", !ACoopNet::IsConnected() ? "not connected" : ( !s_bHasRemote ? "nothing from the partner yet" : ( IsRemoteUsable() ? "the partner's story is known" : "the partner isn't in a story game (or another version)" ) ) );
		}

		s_iSync = iSync;
	}
}

// After CompleteQuest: the story is read again right away (before the prompt is built from it, and before the scene of
// a next tier loads later in the frame), and the other game is told now
static void OnStoryChanged( DWORD a_uiNow )
{
	if ( !s_bStoryChanged )
		return;

	s_bStoryChanged = TFALSE;
	UpdateLocalProgress();
	UpdateAheadTimes( a_uiNow );
	s_fSendWait = 0.0f;
	SendProgress( 0.0f );
}

// ACoopNet::Update, every frame while a session runs
static void OnUpdate( TFLOAT a_fDeltaTime )
{
	ParseSwitches();
	EnsureHud();

	const DWORD uiNow = GetTickCount();

	// Safe moments in a row: on the farm and in the barn the story can change, in the minigame of the assigned quest
	// that minigame can end as won
	const TCHAR* szWhy        = TNULL;
	const PLACE  ePrevPlace   = s_ePlace;
	s_ePlace                  = GetSafePlace( &szWhy );

	if ( s_ePlace == PLACE_FARM || s_ePlace == PLACE_BARN )
	{
		s_fSafeTime += a_fDeltaTime;

		if ( s_uiFarmUpTime == 0 )
		{
			s_uiFarmUpTime = ( uiNow != 0 ) ? uiNow : 1;

			if ( s_bLog )
				TINFO( "[coopstory] the farm is up\n" );
		}
	}
	else
	{
		if ( s_bLog && s_fSafeTime > 0.0f && ( s_iNumCatchUp > 0 || s_ePrompt != PROMPT_NONE ) )
			TINFO( "[coopstory] not safe any more: %s\n", szWhy ? szWhy : "in a minigame" );

		s_fSafeTime   = 0.0f;
		s_szUnsafeWhy = szWhy ? szWhy : "in a minigame";
	}

	s_fMiniGameTime = ( s_ePlace == PLACE_MINIGAME ) ? s_fMiniGameTime + a_fDeltaTime : 0.0f;

	if ( s_bLog && s_ePlace != ePrevPlace && ( s_ePlace == PLACE_BARN || s_ePlace == PLACE_MINIGAME ) )
		TINFO( "[coopstory] skips can be applied %s now\n", ( s_ePlace == PLACE_BARN ) ? "in the barn" : "in the minigame (it ends as won)" );

	UpdateLocalProgress();
	SendProgress( a_fDeltaTime );
	SendSkipCopies( a_fDeltaTime );
	SendSaveMessages( a_fDeltaTime );
	UpdateAheadTimes( uiNow );
	UpdateWonQuest( uiNow );
	UpdateCatchUp( a_fDeltaTime );
	OnStoryChanged( uiNow );
	UpdateFarmCleanUp();
	UpdatePrompt( a_fDeltaTime, uiNow );
	UpdatePartnerSave();
	UpdateClock( a_fDeltaTime, uiNow );
	UpdateSharedSteps( a_fDeltaTime );
	UpdateDevActions( uiNow );
	UpdateDevInstructions( a_fDeltaTime );
	OnStoryChanged( uiNow );
	UpdateLog( a_fDeltaTime );
	UpdateShots( a_fDeltaTime );

	// A notice counts down while it's shown (ACoopProgressHud::Update), one nobody could see in a minute is dropped
	if ( s_fNoticeTime > 0.0f && uiNow - s_uiNoticeTime > 60000 )
		s_fNoticeTime = 0.0f;
}

// The handlers are registered with ACoopNet when the program starts: nothing else has to call this module.
// NOTE: ACoopNet's handler tables are plain arrays, initialised before any constructor of the program runs
static struct ACoopProgressRegistration
{
	ACoopProgressRegistration()
	{
		ACoopNet::SetDataHandler( ACoopNet::CHANNEL_PROGRESS, OnProgressData );
		ACoopNet::SetDataHandler( ACoopNet::CHANNEL_SKIP, OnSkipData );
		ACoopNet::AddUpdateHandler( OnUpdate );
	}
} s_oRegistration;
