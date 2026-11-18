#include "pch.h"
#include "ACoop.h"
#include "ACoopNet.h"
#include "ACoopPartner.h"
#include "APlayerManager.h"
#include "APlayerProfileManager.h"
#include "Helpers/AGameLoader.h"
#include "Input/AInputHandler.h"
#include "Units/AUnitManager.h"
#include "Units/AUnitProxy.h"
#include "Units/AUnitPlayer.h"
#include "Units/AUnitTransform.h"
#include "Units/ASimplePlayerControlComponent.h"
#include "Units/AInteractionManager.h"
#include "UnitMovement/PathfindingNetworks/AInteriorArea.h"
#include "UnitMovement/Steering/APlayerSteer.h"
#include "UnitMovement/PathfindingNetworks/AInteriorGoal.h"
#include "Animation/AModel.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TPSTRING8_DECLARE( player );

// Slot of the second player in the sim's player set
static constexpr TINT SECOND_PLAYER_SLOT = 1;

// Set by the frontend's Co-op menu
static TBOOL s_bRequested = TFALSE;

// The second player's slot was added by the last AGameLoader::SpawnSimPlayer
static TBOOL s_bSlotAdded = TFALSE;

// Time until the next try to spawn the second player / to bring it back
static TFLOAT s_fSpawnWait  = 0.0f;
static TFLOAT s_fTetherWait = 0.0f;

// "-cooplog": time until the next line with the positions of the players
static TFLOAT s_fLogWait = 0.0f;

//-----------------------------------------------------------------------------
// Command line: "-coop [controller index]" and "-cooplog"
//-----------------------------------------------------------------------------

struct CoopCommandLine
{
	TBOOL bCoop       = TFALSE;
	TINT  iController = -1;     // index in the input handler's controller list, -1: automatic
	TBOOL  bKeyboard  = TFALSE; // "-coop kb": the second player has the keyboard (tests, see "-devkeys")
	TBOOL  bLog       = TFALSE;
	TFLOAT fTether    = ACoop::TETHER_DISTANCE; // "-cooptether <metres>"
};

// Returns TTRUE if the whole switch is on the command line ("-coop" isn't "-cooplog"), a_ppOutArgs receives what follows it.
// a_szFrom continues a search after an earlier match (the switch can be given more than once)
static TBOOL FindSwitch( const TCHAR* a_szSwitch, const TCHAR** a_ppOutArgs = TNULL, const TCHAR* a_szFrom = TNULL )
{
	const TINT   iLength       = TStringManager::String8Length( a_szSwitch );
	const TCHAR* szCommandLine = AGameLoader::Dev_GetCommandLine();
	const TCHAR* szFound       = a_szFrom ? a_szFrom : szCommandLine;

	while ( szFound && ( szFound = TStringManager::String8FindString( szFound, a_szSwitch ) ) != TNULL )
	{
		// The switch has to start an argument: "-coop" isn't the end of another argument or of a folder in the program's path
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

static const CoopCommandLine& GetCoopCommandLine()
{
	static CoopCommandLine s_oCommandLine;
	static TBOOL           s_bParsed = TFALSE;

	if ( !s_bParsed )
	{
		s_bParsed = TTRUE;

		const TCHAR* szArgs = TNULL;

		// The last "-coop" with an option wins: a launcher's plain "-coop" can be followed by the user's "-coop kb"
		while ( FindSwitch( "-coop", &szArgs, szArgs ) )
		{
			s_oCommandLine.bCoop = TTRUE;

			// Optional controller index, or "kb"
			while ( *szArgs == ' ' ) szArgs++;

			if ( szArgs[ 0 ] == 'k' && szArgs[ 1 ] == 'b' && ( szArgs[ 2 ] == '\0' || szArgs[ 2 ] == ' ' || szArgs[ 2 ] == '"' ) )
			{
				s_oCommandLine.bKeyboard   = TTRUE;
				s_oCommandLine.iController = -1;
			}
			else if ( *szArgs >= '0' && *szArgs <= '9' )
			{
				TINT iIndex = 0;

				while ( *szArgs >= '0' && *szArgs <= '9' && iIndex < 100 )
					iIndex = iIndex * 10 + ( *szArgs++ - '0' );

				s_oCommandLine.iController = iIndex;
				s_oCommandLine.bKeyboard   = TFALSE;
			}
		}

		s_oCommandLine.bLog = FindSwitch( "-cooplog" );

		if ( FindSwitch( "-cooptether", &szArgs ) )
		{
			const TFLOAT fTether = TFLOAT( atof( szArgs ) );

			if ( fTether >= 3.0f && fTether <= 45.0f )
				s_oCommandLine.fTether = fTether;
		}
	}

	return s_oCommandLine;
}

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

static APlayerSet* GetSimPlayerSet()
{
	return APlayerManager::GetSingleton()->GetPlayerSet( APlayerManager::PLAYERSET_SIM );
}

// Slot of the second player, TNULL unless the sim's player set is the current one and has it
static APlayerSlot* GetSecondPlayerSlot()
{
	if ( !s_bSlotAdded || !APlayerManager::IsSingletonCreated() )
		return TNULL;

	APlayerSet* pPlayerSet = GetSimPlayerSet();

	// The frontend and the minigames reset the set or switch to the other one
	if ( APlayerManager::GetSingleton()->GetCurrentPlayerSet() != pPlayerSet || pPlayerSet->GetHumanPlayerCount() <= SECOND_PLAYER_SLOT )
		return TNULL;

	return pPlayerSet->GetPlayerSlot( SECOND_PLAYER_SLOT );
}

static TINT FindSecondPlayerControllerIndex()
{
	if ( !AInputHandler::IsSingletonCreated() || GetCoopCommandLine().bKeyboard )
		return -1;

	AInputHandler* pInput = AInputHandler::GetSingleton();
	const TINT iNumControllers = pInput->GetNumControllers();
	APlayerSlot* pSecond = GetSecondPlayerSlot();
	TInputDeviceController* pFirstController = pSecond ? GetSimPlayerSet()->GetPlayerSlot( 0 )->m_pController : pInput->GetMainController();

	// The list contains connected devices only. A reserved pointer survives disconnects
	// even when this display index changes, so another pad can never steal its slot.
	if ( pSecond && pSecond->m_pController )
	{
		for ( TINT i = 0; i < iNumControllers; i++ )
			if ( pInput->GetController( i ) == pSecond->m_pController )
				return i;
		return -1;
	}

	const TINT iPicked = GetCoopCommandLine().iController;
	if ( iPicked >= 0 )
		return iPicked < iNumControllers && ( !pSecond || pInput->GetController( iPicked ) != pFirstController ) ? iPicked : -1;

	// At startup prefer the second pad. While playing, only claim a free one.
	if ( !pSecond )
	{
		if ( iNumControllers >= 2 )
			return pInput->GetController( 1 ) != pFirstController ? 1 : 0;
		return iNumControllers == 1 ? 0 : -1;
	}

	for ( TINT i = 0; i < iNumControllers; i++ )
		if ( pInput->GetController( i ) != pFirstController )
			return i;
	return -1;
}

// Puts the second player on the ground next to the first one, facing the same way
static void PlaceNextToFirstPlayer( AUnit* a_pFirst, AUnit* a_pSecond )
{
	ASteer* pSecondSteer = a_pSecond->GetSteer();

	if ( !pSecondSteer || !pSecondSteer->IsA( &TGetClass( APlayerSteer ) ) )
		return;

	APlayerSteer* pSteer      = TSTATICCAST( APlayerSteer, pSecondSteer );
	ASteer*       pFirstSteer = a_pFirst->GetSteer();

	const TVector4 vFirstPosition = *pFirstSteer->GetPosition();

	TVector4 vForward;
	pFirstSteer->CopyDirection( vForward );
	vForward.y = 0.0f;
	vForward.w = 1.0f;

	if ( vForward.MagnitudeSqXZ() < 0.01f )
		vForward = TVector4::VEC_POSX;
	else
		vForward.Normalise();

	// To the right, to the left, behind or in front: the first place with ground at about the same height.
	// NOTE: the Y axis points down. Without ground the players share the place, a bit above it
	static const TFLOAT s_aAngles[] = { TMath::HALF_PI, -TMath::HALF_PI, TMath::PI, 0.0f };

	TVector4 vPosition = vFirstPosition;
	vPosition.y -= 0.5f;

	for ( TINT i = 0; i < TARRAYSIZE( s_aAngles ); i++ )
	{
		TVector4 vOffset = vForward;
		vOffset.AsVector3().RotateY( s_aAngles[ i ] );
		vOffset.Multiply( ACoop::PLACE_DISTANCE );

		TVector4 vCandidate( vFirstPosition.x + vOffset.x, vFirstPosition.y, vFirstPosition.z + vOffset.z, 1.0f );

		// 0.0f is "nothing below"
		const TFLOAT fGroundHeight = pSteer->GetGroundHeight( vCandidate );

		if ( fGroundHeight != 0.0f && TMath::Abs( fGroundHeight - vFirstPosition.y ) < 1.5f )
		{
			vPosition   = vCandidate;
			vPosition.y = fGroundHeight - 0.2f;
			break;
		}
	}

	// Leave the ladder, the jump or the fall it was in
	if ( pSteer->GetState() == APlayerSteer::STATE_CLIMBING )
		pSteer->EndState( APlayerSteer::STATE_CLIMBING );

	// The same way AGameLoader places the player at a point that isn't a goal of the network
	AInteriorGoal oGoal;
	oGoal.m_vForward   = vForward;
	oGoal.m_fPositionX = vPosition.x;
	oGoal.m_fPositionY = vPosition.y;
	oGoal.m_fPositionZ = vPosition.z;

	pSteer->PlaceAtGoal( &oGoal, TTRUE, TFALSE );
	pSteer->SetSpeed( 0.0f );

	// Through the falling state: it's the one that ends the take-off of a jump, the player couldn't jump again otherwise
	if ( pSteer->GetState() == APlayerSteer::STATE_JUMPING )
		pSteer->SetState( APlayerSteer::STATE_FALLING );

	pSteer->SetState( APlayerSteer::STATE_WALKING );
}

// Spawns the unit of the second player next to the first one
static AUnit* SpawnSecondPlayer( APlayerSet* a_pPlayerSet, AUnit* a_pFirst )
{
	// The same model as the first player: only the main profile's gender and costume are loaded
	APlayerProfileManager* pProfileManager = APlayerProfileManager::GetSingleton();
	const TBOOL            bIsFemale       = pProfileManager->GetMainGender() == APlayerProfile::GENDER_FEMALE;

	TVector4 vDirection;
	a_pFirst->GetSteer()->CopyDirection( vDirection );

	// The barn loads the indoor player variants, just like AGameLoader::SpawnSimPlayer.
	const TINT iSkin = AGameLoader::IsBarnExploreMiniGame() ? ( bIsFemale ? 3 : 2 ) : ( bIsFemale ? 1 : 0 );
	AUnitTransform oTransform( *a_pFirst->GetSteer()->GetPosition(), vDirection, 0, iSkin, -1, TTRUE );
	AUnit*         pSecond = AUnitManager::GetSingleton()->SpawnUnit( TPS8( player ), &oTransform );

	if ( !pSecond )
	{
		TERROR( "[coop] couldn't spawn the second player\n" );
		return TNULL;
	}

	// APlayerManager gives a new player unit the first slot without a unit, make sure it's the second player's
	a_pPlayerSet->SetPlayerUnit( pSecond, SECOND_PLAYER_SLOT );

	AAnimatedUnit* pAnimatedSecond = TSTATICCAST( AAnimatedUnit, pSecond );
	pAnimatedSecond->ApplyMaterial( pProfileManager->GetMainCostume(), TTRUE );
	pAnimatedSecond->GetModelInstance()->SetShadowMode( AModelInstance::SHADOWMODE_DYNAMIC );

	// The camera belongs to the first player
	ASimplePlayerControlComponent* pControl = TSTATICCAST( ASimplePlayerControlComponent, pSecond->FindComponent( &TGetClass( ASimplePlayerControlComponent ) ) );

	if ( pControl )
		pControl->SetCameraInput( TFALSE );

	PlaceNextToFirstPlayer( a_pFirst, pSecond );

	const TVector4* pPosition = pSecond->GetSteer()->GetPosition();
	TINFO( "[coop] second player spawned (unit %d) at %.2f %.2f %.2f, controller: %s\n", pSecond->GetId(), pPosition->x, pPosition->y, pPosition->z, a_pPlayerSet->GetPlayerSlot( SECOND_PLAYER_SLOT )->m_pController ? "yes" : "NONE" );

	return pSecond;
}

//-----------------------------------------------------------------------------
// ACoop
//-----------------------------------------------------------------------------

TBOOL ACoop::IsRequested()
{
	return s_bRequested || IsForcedByCommandLine();
}

void ACoop::SetRequested( TBOOL a_bRequested )
{
	if ( s_bRequested != a_bRequested )
		TINFO( "[coop] %s\n", a_bRequested ? "requested for the next game" : "request cleared" );

	s_bRequested = a_bRequested;
}

TBOOL ACoop::IsForcedByCommandLine()
{
	return GetCoopCommandLine().bCoop;
}

TInputDeviceController* ACoop::FindSecondPlayerController()
{
	if ( GetCoopCommandLine().bKeyboard )
		return TNULL;

	APlayerSlot* pSlot = GetSecondPlayerSlot();
	if ( pSlot && pSlot->m_pController )
		return pSlot->m_pController;

	const TINT iIndex = FindSecondPlayerControllerIndex();
	TInputDeviceController* pController = iIndex >= 0 ? AInputHandler::GetSingleton()->GetController( iIndex ) : TNULL;
	if ( pSlot && pController )
	{
		pSlot->m_pController = pController;
		TINFO( "[coop] waiting second player assigned controller %d (hot-plug)\n", iIndex + 1 );
	}
	return pController;
}

TINT ACoop::GetSecondPlayerControllerNumber()
{
	return FindSecondPlayerControllerIndex() + 1;
}

TInputDeviceController* ACoop::OnSpawnSimPlayer( APlayerSet* a_pPlayerSet, TInputDeviceController* a_pFirstPlayerController, TBOOL a_bMainGame )
{
	s_bSlotAdded  = TFALSE;
	s_fSpawnWait  = 0.0f;
	s_fTetherWait = 0.0f;
	s_fLogWait    = 0.0f;

	// NOTE: with network co-op (ACoopNet) the other player is in its own game, there's no second local player
	if ( !a_bMainGame || !IsRequested() || ACoopNet::GetMode() != ACoopNet::MODE_OFF )
		return a_pFirstPlayerController;

	TInputDeviceController* pSecondController = FindSecondPlayerController();

	if ( GetCoopCommandLine().bKeyboard )
	{
		// The slot only compares the pointer with the source of the input events (APlayerSet::GetPlayerSlotIndexByDevice),
		// so the keyboard can stand in for a controller. The first player then has the mouse and the controllers
		pSecondController = TREINTERPRETCAST( TInputDeviceController*, AInputHandler::GetSingleton()->GetKeyboardDevice() );
	}

	a_pPlayerSet->AddHumanPlayer( pSecondController, -1 );
	s_bSlotAdded = TTRUE;

	if ( GetCoopCommandLine().bKeyboard )
		TINFO( "[coop] second player's slot added, it has the keyboard\n" );
	else
		TINFO( "[coop] second player's slot added, controller %d of %d\n", GetSecondPlayerControllerNumber(), AInputHandler::GetSingleton()->GetNumControllers() );

	if ( !pSecondController )
		TWARN( "[coop] no controller for the second player: connect one to continue without restarting\n" );

	// One device can't be in two slots (the first match wins): the first player then has the keyboard, the mouse and the other controllers
	return ( pSecondController == a_pFirstPlayerController ) ? TNULL : a_pFirstPlayerController;
}

void ACoop::Update( TFLOAT a_fDeltaTime )
{
	APlayerSlot* pSlot = GetSecondPlayerSlot();

	if ( !pSlot )
		return;

	if ( ( !AGameLoader::IsMainGameLoaded() && !AGameLoader::IsBarnExploreMiniGame() ) || AGameLoader::IsChangingTerrain() )
		return;

	APlayerSet* pPlayerSet = GetSimPlayerSet();
	AUnit*      pFirst     = pPlayerSet->GetPlayerUnit( 0 );

	if ( !pFirst || !pFirst->GetSteer() )
		return;

	// The first player is still waiting for the terrain around the goal it's placed at
	ASteer* pFirstSteer = pFirst->GetSteer();

	if ( pFirstSteer->IsA( &TGetClass( APlayerSteer ) ) && TSTATICCAST( APlayerSteer, pFirstSteer )->HasGoal() )
		return;

	AUnit* pSecond = pSlot->m_pUnitProxy ? pSlot->m_pUnitProxy->GetUnit() : TNULL;

	if ( pSecond && !pSecond->IsPlayer() )
	{
		// Another unit of the player class (a bike) was created while the slot was free
		pSlot->m_pUnitProxy = TNULL;
		pSecond             = TNULL;
	}

	if ( !pSecond )
	{
		s_fSpawnWait -= a_fDeltaTime;

		if ( s_fSpawnWait > 0.0f )
			return;

		s_fSpawnWait  = 1.0f;
		s_fTetherWait = 1.0f;

		pSecond = SpawnSecondPlayer( pPlayerSet, pFirst );

		if ( !pSecond )
			return;
	}

	ASteer* pSecondSteer = pSecond->GetSteer();

	if ( !pSecondSteer )
		return;

	ACoopPartner::EnsureWorldHud();

	const TVector4 vFirstPosition  = *pFirstSteer->GetPosition();
	const TVector4 vSecondPosition = *pSecondSteer->GetPosition();

	if ( GetCoopCommandLine().bLog )
	{
		s_fLogWait -= a_fDeltaTime;

		if ( s_fLogWait <= 0.0f )
		{
			s_fLogWait = 2.0f;
			TINFO( "[coop] P1 %.2f %.2f %.2f | P2 %.2f %.2f %.2f | distance %.2f\n", vFirstPosition.x, vFirstPosition.y, vFirstPosition.z, vSecondPosition.x, vSecondPosition.y, vSecondPosition.z, TMath::Sqrt( TVector4::DistanceSqXZ( vFirstPosition, vSecondPosition ) ) );
		}
	}

	// Keep the second player close: the terrain's collision is only there around the camera
	if ( s_fTetherWait > 0.0f )
	{
		s_fTetherWait -= a_fDeltaTime;
		return;
	}

	const TFLOAT fTether = GetCoopCommandLine().fTether;
	const TBOOL  bTooFar = TVector4::DistanceSqXZ( vFirstPosition, vSecondPosition ) > fTether * fTether;
	const TBOOL bFell   = TMath::Abs( vSecondPosition.y - vFirstPosition.y ) > TETHER_HEIGHT;

	if ( bTooFar || bFell )
	{
		TINFO( "[coop] second player brought back (%s)\n", bTooFar ? "too far" : "height" );

		PlaceNextToFirstPlayer( pFirst, pSecond );
		s_fTetherWait = 1.0f;
	}
}

void ACoop::BringSecondPlayerBack()
{
	AUnit* pSecond = GetSecondPlayerUnit();
	AUnit* pFirst  = pSecond ? GetSimPlayerSet()->GetPlayerUnit( 0 ) : TNULL;

	if ( !pFirst || !pFirst->GetSteer() || pFirst == pSecond )
		return;

	TINFO( "[coop] second player brought back (water)\n" );

	PlaceNextToFirstPlayer( pFirst, pSecond );
	s_fTetherWait = 1.0f;
}

AUnit* ACoop::GetSecondPlayerUnit()
{
	APlayerSlot* pSlot = GetSecondPlayerSlot();

	if ( !pSlot || !pSlot->m_pUnitProxy )
		return TNULL;

	return pSlot->m_pUnitProxy->GetUnit();
}

TBOOL ACoop::IsSecondPlayerUnit( const AUnit* a_pUnit )
{
	return a_pUnit && a_pUnit == GetSecondPlayerUnit();
}

TBOOL ACoop::IsSecondPlayerDevice( const TInputDevice* a_pDevice )
{
	APlayerSlot* pSlot = GetSecondPlayerSlot();

	return pSlot && a_pDevice && pSlot->m_pController == a_pDevice;
}

TBOOL ACoop::TryStartInteriorDoor( AUnit* a_pUnit, AInteriorArea* a_pArea )
{
	if ( !IsSecondPlayerUnit( a_pUnit ) || !a_pArea || !a_pArea->m_strName ||
	     HASANYFLAG( a_pArea->m_eFlags, AInteriorArea::FLAGS_RUNTIME_SPECIAL ) )
		return TFALSE;

	const auto& strTrigger = *a_pArea->m_strName;
	if ( strTrigger != TPS8D( "park2.triggers.enterbarn" ) &&
	     strTrigger != TPS8D( "barn.triggers.leavebarn" ) &&
	     strTrigger != TPS8D( "gophershop" ) )
		return TFALSE;

	AUnit* pFirst = GetSimPlayerSet()->GetPlayerUnit( 0 );
	if ( !pFirst || pFirst->HasInteraction() || a_pUnit->HasInteraction() )
		return TFALSE;

	if ( !AInteractionManager::GetSingleton()->AssignInteractionForced( a_pUnit, strTrigger ) )
		return TFALSE;

	if ( GetCoopCommandLine().bLog )
		TINFO( "[coop] P2 unit %d started interior door %s\n", a_pUnit->GetId(), strTrigger.GetString() );

	return TTRUE;
}

AUnit* ACoop::GetSharedCameraPartner( const AUnit* a_pCameraTarget )
{
	if ( !a_pCameraTarget || ( !AGameLoader::IsMainGameLoaded() && !AGameLoader::IsBarnExploreMiniGame() ) )
		return TNULL;

	AUnit* pSecond = GetSecondPlayerUnit();

	if ( !pSecond || !pSecond->GetSteer() || !pSecond->IsPlayer() )
		return TNULL;

	// Only the camera of the first player (also while it's the bike it rides)
	if ( GetSimPlayerSet()->GetPlayerUnit( 0 ) != a_pCameraTarget || !a_pCameraTarget->GetSteer() )
		return TNULL;

	// Not a partner that's out of the tether's reach (ACoop::Update doesn't run under every state of the sim)
	const TFLOAT fReach = GetCoopCommandLine().fTether * 1.5f;

	if ( TVector4::DistanceSqXZ( *a_pCameraTarget->GetSteer()->GetPosition(), *pSecond->GetSteer()->GetPosition() ) > fReach * fReach )
		return TNULL;

	return pSecond;
}

TFLOAT ACoop::GetSharedCameraExtraDistance( TFLOAT a_fSeparation )
{
	// Side by side it's the camera of the single player game
	const TFLOAT fTether = GetCoopCommandLine().fTether;
	const TFLOAT fExtra  = ( ( ( a_fSeparation < fTether ) ? a_fSeparation : fTether ) - 2.0f ) * 0.8f;

	return ( fExtra > 0.0f ) ? fExtra : 0.0f;
}
