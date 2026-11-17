#include "pch.h"
#include "ACoopNet.h"
#include "ACoopAnim.h"
#include "ACoopPartner.h"
#include "ASkins.h"
#include "APlayerManager.h"
#include "APlayerProfileManager.h"
#include "Helpers/AGameLoader.h"
#include "Units/AUnitManager.h"
#include "Units/AUnitProxy.h"
#include "Units/AUnitPlayer.h"
#include "Units/AUnitTransform.h"
#include "Units/AUnitModelStaticData.h"
#include "Units/AUnitType.h"
#include "UnitMovement/Steering/APlayerSteer.h"
#include "UnitMovement/Steering/ASSeqSteer.h"
#include "Physics/ACollisionObject.h"
#include "Animation/AModel.h"
#include "Locale/AModStrings.h"
#include "Animation/AAnimatableObjectManager.h"
#include "MiniGame/ABarnExploreMinigameState.h"
#include "GameInterface/AGameStateController.h"
#include "Tasks/ARootTask.h"

#include <cmath>
#include <iphlpapi.h>

// NOTE: WinSock2.h comes with ToshiPCH.h
#pragma comment( lib, "ws2_32.lib" )
#pragma comment( lib, "iphlpapi.lib" )

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TPSTRING8_DECLARE( player );
TPSTRING8_DECLARE( ASimState );
TPSTRING8_DECLARE( ABarnExploreMinigameState );

// Development ("-netlog"): the call stack of a crash goes to stderr, there's no debugger on the players' computers
#include <DbgHelp.h>
#pragma comment( lib, "dbghelp.lib" )

static LONG WINAPI CoopCrashFilter( EXCEPTION_POINTERS* a_pInfo )
{
	HANDLE hProcess = GetCurrentProcess();
	SymSetOptions( SYMOPT_LOAD_LINES | SYMOPT_UNDNAME );
	SymInitialize( hProcess, NULL, TRUE );

	fprintf( stderr, "[crash] code %08x at %p" "\n", (unsigned)a_pInfo->ExceptionRecord->ExceptionCode, a_pInfo->ExceptionRecord->ExceptionAddress );

	CONTEXT      oContext = *a_pInfo->ContextRecord;
	STACKFRAME64 oFrame   = {};
	oFrame.AddrPC.Offset    = oContext.Eip;
	oFrame.AddrPC.Mode      = AddrModeFlat;
	oFrame.AddrFrame.Offset = oContext.Ebp;
	oFrame.AddrFrame.Mode   = AddrModeFlat;
	oFrame.AddrStack.Offset = oContext.Esp;
	oFrame.AddrStack.Mode   = AddrModeFlat;

	for ( int i = 0; i < 28 && StackWalk64( IMAGE_FILE_MACHINE_I386, hProcess, GetCurrentThread(), &oFrame, &oContext, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL ); i++ )
	{
		char         acSymbol[ sizeof( SYMBOL_INFO ) + 256 ] = {};
		SYMBOL_INFO* pSymbol = (SYMBOL_INFO*)acSymbol;
		pSymbol->SizeOfStruct = sizeof( SYMBOL_INFO );
		pSymbol->MaxNameLen   = 255;

		DWORD64         uiDisplacement = 0;
		DWORD           uiLineDisplacement = 0;
		IMAGEHLP_LINE64 oLine = {};
		oLine.SizeOfStruct = sizeof( oLine );

		const BOOL bSymbol = SymFromAddr( hProcess, oFrame.AddrPC.Offset, &uiDisplacement, pSymbol );
		const BOOL bLine   = SymGetLineFromAddr64( hProcess, oFrame.AddrPC.Offset, &uiLineDisplacement, &oLine );

		fprintf( stderr, "[crash] %2d %08x %s %s:%d" "\n", i, (unsigned)oFrame.AddrPC.Offset, bSymbol ? pSymbol->Name : "?", bLine ? oLine.FileName : "?", bLine ? (int)oLine.LineNumber : 0 );
	}

	fflush( stderr );
	return EXCEPTION_EXECUTE_HANDLER;
}

//-----------------------------------------------------------------------------
// Packets. Both games are the same build on the same kind of CPU, so the
// struct is sent as it is.
//-----------------------------------------------------------------------------

static constexpr TUINT32 PACKET_MAGIC = 0x31435942; // "BYC1"

enum PACKETTYPE : TUINT8
{
	PACKETTYPE_HELLO = 1, // the joining game looks for the host (also sent to the whole LAN)
	PACKETTYPE_WELCOME,   // the host's answer
	PACKETTYPE_STATE,     // where the sender's player is
	PACKETTYPE_BYE,       // the sender ended the session
	PACKETTYPE_DATA,      // a datagram of a data channel (CoopDataHeader + payload)
};

enum PACKETFLAGS : TUINT8
{
	PACKETFLAGS_NONE       = 0,
	PACKETFLAGS_IN_WORLD   = 1 << 0, // the sender's player walks around in an area, the rest of the state is valid
	PACKETFLAGS_AREA_SHIFT = 1,      // bits 1-3: the sender's ACoopNet::AREA (a build without them only sends the farm)
	PACKETFLAGS_AREA_MASK  = 7 << PACKETFLAGS_AREA_SHIFT,
};

#pragma pack( push, 1 )
struct CoopPacket
{
	TUINT32 uiMagic;
	TUINT8  uiType;
	TUINT8  uiFlags;
	TUINT16 uiSequence;
	TFLOAT  afPosition[ 3 ];
	TFLOAT  afDirection[ 2 ]; // x and z
	TFLOAT  fSpeed;           // measured speed, the remote cow's animation follows it
};

// PACKETTYPE_DATA: the header, then uiSize bytes of the channel's data
struct CoopDataHeader
{
	TUINT32 uiMagic;
	TUINT8  uiType;
	TUINT8  uiChannel;
	TUINT16 uiSize;
};
#pragma pack( pop )

static ACoopNet::t_DataHandler   s_afnDataHandlers[ ACoopNet::CHANNEL_NUMOF ] = {};
static ACoopNet::t_UpdateHandler s_afnUpdateHandlers[ 8 ]                    = {};
static TINT                      s_iNumUpdateHandlers                         = 0;

//-----------------------------------------------------------------------------
// Session
//-----------------------------------------------------------------------------

static ACoopNet::MODE s_eMode       = ACoopNet::MODE_OFF;
static SOCKET         s_hSocket     = INVALID_SOCKET;
static TBOOL          s_bWinsock    = TFALSE;
static TINT           s_iPort       = ACoopNet::DEFAULT_PORT;
static sockaddr_in    s_oTarget     = {}; // MODE_JOIN: where the hellos go (the host or the broadcast address)
static sockaddr_in    s_oPeer       = {}; // the other game
static TBOOL          s_bHasPeer    = TFALSE;
static DWORD          s_uiLastHeard = 0; // GetTickCount of the peer's last packet
static TFLOAT         s_fSendWait   = 0.0f;
static TFLOAT         s_fHelloWait  = 0.0f;
static TUINT16        s_uiSequence  = 0;
static TBOOL          s_bStartedByCommandLine = TFALSE;

// The peer's last state
static CoopPacket s_oRemote          = {};
static TBOOL      s_bHasRemoteState  = TFALSE;
static DWORD      s_uiRemoteStateTime = 0;
static TVector4   s_vRemoteVelocity  = TVector4::VEC_ZERO; // on the XZ plane, measured from the last states

// The area of this game's player, from the start of the last Update
static ACoopNet::AREA s_eLocalArea = ACoopNet::AREA_NONE;

// The local player's position at the last state sent, for its measured speed (the steers without kinematics, e.g.
// the bike's, can't tell their measured speed)
static TVector4 s_vLastSentPosition;
static DWORD    s_uiLastSentTime = 0;
static TBOOL    s_bHasLastSent   = TFALSE;

// The remote cow. A pointer to the reference, so that nothing is released when the program exits
static TRef<AUnitProxy>* s_pRemoteUnit = TNULL;
static ACoopNet::AREA    s_eRemoteUnitArea = ACoopNet::AREA_NONE; // where it was spawned
static TVector4          s_vShownPosition;
static TVector4          s_vShownDirection;
static TBOOL             s_bShown = TFALSE;
static DWORD             s_uiSpawnRetryTime = 0; // GetTickCount before which no spawn is tried again (it failed)

// "-netlog": time until the next line about the session
static TFLOAT s_fLogWait = 0.0f;

// What was sent since the last line of "-netlog": bytes of the UDP payloads (the IP and UDP headers come on top, 28
// bytes a datagram) and datagrams, the states and every data channel
static TUINT32 s_uiSentBytes     = 0;
static TUINT32 s_uiSentDatagrams = 0;
static TUINT32 s_auiChannelBytes[ 16 ];
static DWORD   s_uiCountStart    = 0;

// Development ("-netloss P", "-netdelay MS", "-netjitter MS", not in Final): the datagrams that come in are dropped
// with a probability of P percent and held back MS (+ up to the jitter) milliseconds, which also reorders them
static TINT s_iDevLossPercent = 0;
static TINT s_iDevDelayMs     = 0;
static TINT s_iDevJitterMs    = 0;

struct DelayedDatagram
{
	DWORD       uiReleaseTime;
	sockaddr_in oFrom;
	TINT        iSize;
	char        aData[ sizeof( CoopDataHeader ) + ACoopNet::MAX_DATA_SIZE ];
};

static constexpr TINT MAX_DELAYED = 192;
static DelayedDatagram* s_pDelayed     = TNULL; // allocated when the delay is used
static TINT             s_iNumDelayed  = 0;
static TUINT32          s_uiDevRandom  = 0x2545f491;
static TINT             s_iDevDropped  = 0;

static TUINT32 DevRandom()
{
	// xorshift32
	s_uiDevRandom ^= s_uiDevRandom << 13;
	s_uiDevRandom ^= s_uiDevRandom >> 17;
	s_uiDevRandom ^= s_uiDevRandom << 5;
	return s_uiDevRandom;
}

static TBOOL IsSameAddress( const sockaddr_in& a_rcA, const sockaddr_in& a_rcB )
{
	return a_rcA.sin_addr.s_addr == a_rcB.sin_addr.s_addr && a_rcA.sin_port == a_rcB.sin_port;
}

static TBOOL IsPeerAlive()
{
	return s_bHasPeer && ( GetTickCount() - s_uiLastHeard ) < DWORD( ACoopNet::TIMEOUT * 1000.0f );
}

static void SendPacket( PACKETTYPE a_eType, const sockaddr_in& a_rcTo, const CoopPacket* a_pState = TNULL )
{
	CoopPacket oPacket = {};

	if ( a_pState )
		oPacket = *a_pState;

	oPacket.uiMagic    = PACKET_MAGIC;
	oPacket.uiType     = a_eType;
	oPacket.uiSequence = ++s_uiSequence;

	sendto( s_hSocket, TREINTERPRETCAST( const char*, &oPacket ), sizeof( oPacket ), 0, TREINTERPRETCAST( const sockaddr*, &a_rcTo ), sizeof( a_rcTo ) );

	s_uiSentBytes += sizeof( oPacket );
	s_uiSentDatagrams++;
}

// Asks every network of this computer who hosts. The plain broadcast address only goes out on one adapter,
// which can be the wrong one on a computer with virtual adapters (VPN, virtual machines)
static void SendHelloToTheLAN()
{
	SendPacket( PACKETTYPE_HELLO, s_oTarget );

	IP_ADAPTER_INFO aAdapters[ 16 ];
	ULONG           ulSize = sizeof( aAdapters );

	if ( GetAdaptersInfo( aAdapters, &ulSize ) != NO_ERROR )
		return;

	for ( IP_ADAPTER_INFO* pAdapter = aAdapters; pAdapter; pAdapter = pAdapter->Next )
	{
		for ( IP_ADDR_STRING* pAddress = &pAdapter->IpAddressList; pAddress; pAddress = pAddress->Next )
		{
			const unsigned long ulAddress = inet_addr( pAddress->IpAddress.String );
			const unsigned long ulMask    = inet_addr( pAddress->IpMask.String );

			if ( ulAddress == INADDR_NONE || ulAddress == 0 || ulMask == INADDR_NONE || ulMask == 0 )
				continue;

			// The broadcast address of that network
			sockaddr_in oTo     = s_oTarget;
			oTo.sin_addr.s_addr = ulAddress | ~ulMask;

			SendPacket( PACKETTYPE_HELLO, oTo );
		}
	}
}

static TBOOL OpenSocket( TINT a_iBindPort )
{
	if ( !s_bWinsock )
	{
		WSADATA oData;

		if ( WSAStartup( MAKEWORD( 2, 2 ), &oData ) != 0 )
		{
			TERROR( "[coopnet] no Winsock\n" );
			return TFALSE;
		}

		s_bWinsock = TTRUE;
	}

	s_hSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

	if ( s_hSocket == INVALID_SOCKET )
	{
		TERROR( "[coopnet] couldn't create the socket (%d)\n", WSAGetLastError() );
		return TFALSE;
	}

	// Never blocks the game, and can ask the whole LAN
	u_long ulNonBlocking = 1;
	ioctlsocket( s_hSocket, FIONBIO, &ulNonBlocking );

	BOOL bBroadcast = TRUE;
	setsockopt( s_hSocket, SOL_SOCKET, SO_BROADCAST, TREINTERPRETCAST( const char*, &bBroadcast ), sizeof( bBroadcast ) );

	sockaddr_in oAddress     = {};
	oAddress.sin_family      = AF_INET;
	oAddress.sin_addr.s_addr = INADDR_ANY;
	oAddress.sin_port        = htons( u_short( a_iBindPort ) );

	if ( bind( s_hSocket, TREINTERPRETCAST( const sockaddr*, &oAddress ), sizeof( oAddress ) ) != 0 )
	{
		TERROR( "[coopnet] couldn't use UDP port %d (%d)\n", a_iBindPort, WSAGetLastError() );
		closesocket( s_hSocket );
		s_hSocket = INVALID_SOCKET;
		return TFALSE;
	}

	return TTRUE;
}

// The address of this computer on the LAN for the frontend's text ("" if there's none)
static void GetLocalAddress( TCHAR* a_szOut, TINT a_iSize )
{
	a_szOut[ 0 ] = '\0';

	IP_ADAPTER_INFO aAdapters[ 16 ];
	ULONG           ulSize = sizeof( aAdapters );

	if ( GetAdaptersInfo( aAdapters, &ulSize ) != NO_ERROR )
		return;

	for ( IP_ADAPTER_INFO* pAdapter = aAdapters; pAdapter; pAdapter = pAdapter->Next )
	{
		const TCHAR* szAddress = pAdapter->IpAddressList.IpAddress.String;
		const TCHAR* szGateway = pAdapter->GatewayList.IpAddress.String;

		if ( szAddress[ 0 ] == '\0' || TStringManager::String8Compare( szAddress, "0.0.0.0" ) == 0 )
			continue;

		// The adapter with a gateway is the one on the LAN, the virtual ones (VPN, virtual machines) have none
		const TBOOL bHasGateway = szGateway[ 0 ] != '\0' && TStringManager::String8Compare( szGateway, "0.0.0.0" ) != 0;

		if ( bHasGateway || a_szOut[ 0 ] == '\0' )
			TStringManager::String8CopySafe( a_szOut, szAddress, a_iSize );

		if ( bHasGateway )
			return;
	}
}

//-----------------------------------------------------------------------------
// The remote cow
//-----------------------------------------------------------------------------

static AUnit* GetRemoteUnit()
{
	return ( s_pRemoteUnit && *s_pRemoteUnit ) ? ( *s_pRemoteUnit )->GetUnit() : TNULL;
}

static void ForgetRemoteUnit()
{
	if ( s_pRemoteUnit )
	{
		delete s_pRemoteUnit;
		s_pRemoteUnit = TNULL;
	}

	s_bShown          = TFALSE;
	s_eRemoteUnitArea = ACoopNet::AREA_NONE;

	// The puppets and the markers of the remote cow go with it
	ACoopAnim::OnRemoteUnitGone();
	ACoopPartner::OnRemoteUnitGone();
}

static void RemoveRemoteUnit()
{
	AUnit* pUnit = GetRemoteUnit();

	// First: the markers and the puppets keep pointers to the unit
	ACoopAnim::OnRemoteUnitGone();
	ACoopPartner::OnRemoteUnitGone();

	if ( pUnit && AUnitManager::IsSingletonCreated() )
	{
		TINFO( "[coopnet] remote player removed\n" );
		AUnitManager::GetSingleton()->DestroyUnitNextFrame( pUnit );
	}

	ForgetRemoteUnit();
}

// The area this game's player walks around in, and the player (TNULL with AREA_NONE)
static ACoopNet::AREA FindLocalArea( AUnit** a_ppOutPlayer )
{
	*a_ppOutPlayer = TNULL;

	if ( !APlayerManager::IsSingletonCreated() || !AUnitManager::IsSingletonCreated() || !ARootTask::IsSingletonCreated() || !AGameStateController::IsSingletonCreated() )
		return ACoopNet::AREA_NONE;

	// Loading, a minigame or a story scene of its own (the farm stays "loaded" during those scenes)
	if ( ARootTask::GetSingleton()->IsTransitioning() || AGameLoader::IsChangingTerrain() || AGameLoader::IsReturningToFrontEnd() ||
	     AGameLoader::GetScriptedSequence() != -1 || AGameLoader::IsInMiniGame() )
		return ACoopNet::AREA_NONE;

	AGameStateController* pController = AGameStateController::GetSingleton();

	if ( pController->GetNumStates() <= 0 )
		return ACoopNet::AREA_NONE;

	ACoopNet::AREA eArea = ACoopNet::AREA_NONE;

	if ( AGameLoader::IsBarnExploreMiniGame() )
	{
		// Day and night are two barns (each game has its own clock)
		if ( pController->FindGameStateOnStack( TPS8( ABarnExploreMinigameState ) ) )
		{
			const AGameLoader::Terrain eTerrain = AGameLoader::GetCurrentTerrain();

			if ( eTerrain == AGameLoader::Terrain_EnvBarnIntDay )
				eArea = ACoopNet::AREA_BARN_DAY;
			else if ( eTerrain == AGameLoader::Terrain_EnvBarnInt )
				eArea = ACoopNet::AREA_BARN_NIGHT;
		}
	}
	else if ( AGameLoader::IsMainGameLoaded() && !AGameLoader::IsGameStateLibraryLoaded() && AGameLoader::GetCurrentTerrain() == AGameLoader::Terrain_EnvMain &&
	          pController->FindGameStateOnStack( TPS8( ASimState ) ) )
	{
		// The micro games of the farm (coins, shaking trees, treasure...) are states on top of the sim, still the farm
		eArea = ACoopNet::AREA_FARM;
	}

	if ( eArea == ACoopNet::AREA_NONE )
		return ACoopNet::AREA_NONE;

	APlayerManager* pPlayerManager = APlayerManager::GetSingleton();
	APlayerSet*     pPlayerSet     = pPlayerManager->GetPlayerSet( APlayerManager::PLAYERSET_SIM );

	// The minigames switch to their own player set
	if ( pPlayerManager->GetCurrentPlayerSet() != pPlayerSet || pPlayerSet->GetHumanPlayerCount() < 1 )
		return ACoopNet::AREA_NONE;

	// On the bike it's the rider (ABikeManager puts it in the slot of the cow)
	AUnit* pPlayer = pPlayerSet->GetPlayerUnit( 0 );

	if ( !pPlayer || !pPlayer->GetSteer() || pPlayer->IsDying() || ACoopNet::IsRemotePlayerUnit( pPlayer ) )
		return ACoopNet::AREA_NONE;

	// Still waiting for the terrain around the goal it's placed at
	ASteer* pSteer = pPlayer->GetSteer();

	if ( pSteer->IsA( &TGetClass( APlayerSteer ) ) && TSTATICCAST( APlayerSteer, pSteer )->HasGoal() )
		return ACoopNet::AREA_NONE;

	*a_ppOutPlayer = pPlayer;
	return eArea;
}

static ACoopNet::AREA GetPacketArea( const CoopPacket& a_rcPacket )
{
	if ( !( a_rcPacket.uiFlags & PACKETFLAGS_IN_WORLD ) )
		return ACoopNet::AREA_NONE;

	const TINT iArea = ( a_rcPacket.uiFlags & PACKETFLAGS_AREA_MASK ) >> PACKETFLAGS_AREA_SHIFT;

	// The builds before the areas only sent the farm
	if ( iArea == ACoopNet::AREA_NONE )
		return ACoopNet::AREA_FARM;

	return ( iArea <= ACoopNet::AREA_BARN_NIGHT ) ? ACoopNet::AREA( iArea ) : ACoopNet::AREA_NONE;
}

// The floor of the barn at the height a_fY, the way ABarnExploreMinigameState::UpdateFloor finds the player's
// (the Y axis points down)
static TINT GetBarnFloor( TFLOAT a_fY )
{
	if ( a_fY > 3.246f )
		return ABarnExploreMinigameState::FLOOR_LOFT;

	if ( a_fY < -4.0f )
		return ABarnExploreMinigameState::FLOOR_BASEMENT;

	return ABarnExploreMinigameState::FLOOR_GROUND;
}

// The model index of the "player" unit for the remote cow in a_eArea, -1 if its model isn't loaded
static TINT GetRemoteModelIndex( ACoopNet::AREA a_eArea )
{
	TINT iIndex;

	if ( a_eArea == ACoopNet::AREA_FARM )
	{
		// The model of the remote player's gender (ASkins loads it), the local player's while that isn't known
		iIndex = ASkins::GetRemoteModelIndex();
	}
	else
	{
		// The barn only has the local player's barn model (2: male, 3: female)
		const TBOOL bFemale = APlayerProfileManager::IsSingletonCreated() && APlayerProfileManager::GetSingleton()->GetMainGender() == APlayerProfile::GENDER_FEMALE;
		iIndex              = bFemale ? 3 : 2;
	}

	AUnitType* pType = AUnitManager::GetSingleton()->FindUnit( "player" );

	if ( !pType )
		return -1;

	AUnitModelStaticData* pModelData = TSTATICCAST( AUnitModelStaticData, pType->FindStaticData( &TGetClass( AUnitModelStaticData ) ) );

	if ( !pModelData || iIndex < 0 || iIndex >= pModelData->GetNumModels() || !AAnimatableObjectManager::IsSingletonCreated() )
		return -1;

	// NOTE: AAnimatedUnit::CreateAnimatableObject succeeds without a model, the unit would have none
	if ( !AAnimatableObjectManager::GetSingleton()->FindType( pModelData->GetModelName( iIndex ) ) )
		return -1;

	return iIndex;
}

// The collision of the remote cow: the local player bumps into it (TYPE_SIMPLE, its steer ignores TYPE_UNIT),
// the cameras look through it (TYPE_IGNORED)
static constexpr ACollisionObject::TYPE REMOTE_COLLISION_TYPE = ACollisionObject::TYPE_SIMPLE | ACollisionObject::TYPE_IGNORED;

// Spawns the remote cow at a_rcPosition
static AUnit* SpawnRemoteUnit( const TVector4& a_rcPosition, const TVector4& a_rcDirection, ACoopNet::AREA a_eArea )
{
	const TINT iModelIndex = GetRemoteModelIndex( a_eArea );

	if ( iModelIndex < 0 )
	{
		TERROR( "[coopnet] the model of the remote player isn't loaded here, it isn't shown\n" );
		return TNULL;
	}

	AUnitTransform oTransform( a_rcPosition, a_rcDirection, 0, iModelIndex, -1, TTRUE );
	AUnit*         pUnit = AUnitManager::GetSingleton()->SpawnUnit( TPS8( player ), &oTransform );

	if ( !pUnit )
	{
		TERROR( "[coopnet] couldn't spawn the remote player\n" );
		return TNULL;
	}

	// NOTE: APlayerManager gives a new player unit the first slot without a unit. The sim's only slot has
	// the local player, so the remote cow stays out of the player sets and nothing takes it for "the player"

	AAnimatedUnit* pAnimatedUnit = TSTATICCAST( AAnimatedUnit, pUnit );

	if ( !pAnimatedUnit->GetAnimatableObject() )
	{
		TERROR( "[coopnet] the remote player has no model\n" );
		AUnitManager::GetSingleton()->DestroyUnitNextFrame( pUnit );
		return TNULL;
	}

	// NOTE: the material of the model is shared with the local cows of the same gender, the remote cow's hide is its
	// own (ASkins::OnRemoteUnitSpawned)
	pAnimatedUnit->GetModelInstance()->SetShadowMode( AModelInstance::SHADOWMODE_DYNAMIC );

	// No physics: the packets say where it is
	if ( !pUnit->ChangeSteer( &TGetClass( ASSeqSteer ) ) )
	{
		TERROR( "[coopnet] couldn't change the steer of the remote player\n" );
		AUnitManager::GetSingleton()->DestroyUnitNextFrame( pUnit );
		return TNULL;
	}

	pUnit->GetSteer()->SetPosition( a_rcPosition );
	pUnit->GetSteer()->SetDirection( a_rcDirection );

	// ACollisionManager moves its collision object with the steer
	if ( ACollisionObject* pCollisionObject = TSTATICCAST( AVisibleUnit, pUnit )->GetCollisionObject() )
		pCollisionObject->SetType( REMOTE_COLLISION_TYPE );

	TINFO( "[coopnet] remote player spawned (unit %d, model %d, area %d) at %.2f %.2f %.2f\n", pUnit->GetId(), iModelIndex, a_eArea, a_rcPosition.x, a_rcPosition.y, a_rcPosition.z );

	// The remote player's hide (ASkins knows the models of the farm: in the barn the remote cow has the local look)
	if ( a_eArea == ACoopNet::AREA_FARM )
		ASkins::OnRemoteUnitSpawned( pUnit );

	return pUnit;
}

// Keeps a_rPosition (of the remote cow) out of the local player: each game moves only its own cow, the remote one
// stops at the local one instead of walking into it (the local one is stopped by the remote one's collision)
static void KeepOutOfLocalPlayer( TVector4& a_rPosition, const TVector4& a_rcDirection, AUnit* a_pLocalPlayer, AUnit* a_pRemoteUnit )
{
	ACollisionObject* pLocalObject  = TSTATICCAST( AVisibleUnit, a_pLocalPlayer )->GetCollisionObject();
	ACollisionObject* pRemoteObject = TSTATICCAST( AVisibleUnit, a_pRemoteUnit )->GetCollisionObject();

	if ( !pLocalObject || !pRemoteObject || !TSTATICCAST( AVisibleUnit, a_pLocalPlayer )->IsVisible() )
		return;

	const TVector4& rcLocal = *a_pLocalPlayer->GetSteer()->GetPosition();

	// Not at the same height (one stands on something, or jumps over the other)
	if ( TMath::Abs( rcLocal.y - a_rPosition.y ) > 1.2f )
		return;

	const TFLOAT fMinDistance = pLocalObject->GetObjectRadius() + pRemoteObject->GetObjectRadius();
	TFLOAT       fX           = a_rPosition.x - rcLocal.x;
	TFLOAT       fZ           = a_rPosition.z - rcLocal.z;
	const TFLOAT fDistanceSq  = fX * fX + fZ * fZ;

	if ( fDistanceSq >= fMinDistance * fMinDistance )
		return;

	TFLOAT fDistance = TMath::Sqrt( fDistanceSq );

	if ( fDistance < 0.01f )
	{
		// Right on top of each other: behind the remote cow
		fX        = -a_rcDirection.x;
		fZ        = -a_rcDirection.z;
		fDistance = 1.0f;
	}

	a_rPosition.x = rcLocal.x + fX / fDistance * fMinDistance;
	a_rPosition.z = rcLocal.z + fZ / fDistance * fMinDistance;
}

// Spawns, moves or removes the remote cow
static void UpdateRemoteUnit( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer )
{
	const ACoopNet::AREA eRemoteArea = ( IsPeerAlive() && s_bHasRemoteState ) ? GetPacketArea( s_oRemote ) : ACoopNet::AREA_NONE;
	const TBOOL          bSameArea   = a_pLocalPlayer && eRemoteArea != ACoopNet::AREA_NONE && eRemoteArea == s_eLocalArea;

	// The unit went with the level, or the area changed under it, or one of the players isn't there. A unit that is
	// still there is destroyed too, also while the local player isn't (waiting for the terrain at a goal, a fade): it
	// would stay as a second, frozen cow otherwise. A level that goes takes the unit anyway, marking it changes nothing
	if ( s_pRemoteUnit && ( !GetRemoteUnit() || s_eRemoteUnitArea != s_eLocalArea || !bSameArea ) )
		RemoveRemoteUnit();

	if ( !bSameArea )
		return;

	// Where the remote player is by now: moved on along its velocity for a little while (on the XZ plane, the
	// height follows the states: a jump keeps its arc)
	const TFLOAT fAge = TMath::Min( TFLOAT( GetTickCount() - s_uiRemoteStateTime ) * 0.001f, 0.15f );

	TVector4 vDirection( s_oRemote.afDirection[ 0 ], 0.0f, s_oRemote.afDirection[ 1 ], 1.0f );

	if ( vDirection.MagnitudeSqXZ() < 0.0001f )
		vDirection = TVector4::VEC_POSX;
	else
		vDirection.Normalise();

	TVector4 vTarget( s_oRemote.afPosition[ 0 ] + s_vRemoteVelocity.x * fAge, s_oRemote.afPosition[ 1 ], s_oRemote.afPosition[ 2 ] + s_vRemoteVelocity.z * fAge, 1.0f );

	AUnit* pUnit = GetRemoteUnit();

	if ( !pUnit )
	{
		ForgetRemoteUnit();

		// A spawn that failed isn't tried every frame
		if ( TINT( GetTickCount() - s_uiSpawnRetryTime ) < 0 )
			return;

		pUnit = SpawnRemoteUnit( vTarget, vDirection, s_eLocalArea );

		if ( !pUnit )
		{
			s_uiSpawnRetryTime = GetTickCount() + 5000;
			return;
		}

		s_pRemoteUnit     = new TRef<AUnitProxy>( pUnit->GetSelfProxy() );
		s_eRemoteUnitArea = s_eLocalArea;
	}

	ASteer* pSteer = pUnit->GetSteer();

	if ( !pSteer || !pSteer->IsA( &TGetClass( ASSeqSteer ) ) )
		return;

	if ( !s_bShown || TVector4::DistanceSq( s_vShownPosition, vTarget ) > 6.0f * 6.0f )
	{
		// Too far to slide there
		s_vShownPosition  = vTarget;
		s_vShownDirection = vDirection;
		s_bShown          = TTRUE;
	}
	else
	{
		const TFLOAT fBlend = TMath::Min( a_fDeltaTime * 15.0f, 1.0f );

		s_vShownPosition.x += ( vTarget.x - s_vShownPosition.x ) * fBlend;
		s_vShownPosition.y += ( vTarget.y - s_vShownPosition.y ) * fBlend;
		s_vShownPosition.z += ( vTarget.z - s_vShownPosition.z ) * fBlend;

		s_vShownDirection.x += ( vDirection.x - s_vShownDirection.x ) * fBlend;
		s_vShownDirection.z += ( vDirection.z - s_vShownDirection.z ) * fBlend;
		s_vShownDirection.y = 0.0f;

		if ( s_vShownDirection.MagnitudeSqXZ() < 0.0001f )
			s_vShownDirection = vDirection;
		else
			s_vShownDirection.Normalise();
	}

	s_vShownPosition.w  = 1.0f;
	s_vShownDirection.w = 1.0f;

	KeepOutOfLocalPlayer( s_vShownPosition, s_vShownDirection, a_pLocalPlayer, pUnit );

	// NOTE: ASSeqSteer::SetPosition clears the speed, so the speed goes last
	pSteer->SetPosition( s_vShownPosition );
	pSteer->SetDirection( s_vShownDirection );
	pSteer->SetSpeed( s_oRemote.fSpeed );

	// Shown while its states come, on the floor of the barn the local player is on, and while ACoopAnim doesn't
	// hide it (the partner is shown riding, or hidden on the other side)
	TBOOL bVisible = ACoopNet::GetRemoteStateAge() < ACoopNet::STATE_STALE_TIME;

	if ( bVisible && ( s_eLocalArea == ACoopNet::AREA_BARN_DAY || s_eLocalArea == ACoopNet::AREA_BARN_NIGHT ) )
		bVisible = GetBarnFloor( s_vShownPosition.y ) == ABarnExploreMinigameState::ms_iFloor;

	ACoopAnim::SetRemoteShown( bVisible );

	// Its collision type is set again every frame: ACollisionManager::SweepAndSlide clears TYPE_IGNORED of objects
	// it pushes the local player out of
	if ( ACollisionObject* pCollisionObject = TSTATICCAST( AVisibleUnit, pUnit )->GetCollisionObject() )
		pCollisionObject->SetType( REMOTE_COLLISION_TYPE );
}

//-----------------------------------------------------------------------------
// Receiving and sending
//-----------------------------------------------------------------------------

// Handles one datagram that came from a_rcFrom
static void HandleDatagram( const char* a_pBuffer, TINT a_iSize, const sockaddr_in& a_rcFrom )
{
	const TBOOL bFromPeer = s_bHasPeer && IsSameAddress( a_rcFrom, s_oPeer );

	// A datagram of a data channel
	if ( a_iSize >= TINT( sizeof( CoopDataHeader ) ) )
	{
		CoopDataHeader oHeader;
		TUtil::MemCopy( &oHeader, a_pBuffer, sizeof( oHeader ) );

		if ( oHeader.uiMagic == PACKET_MAGIC && oHeader.uiType == PACKETTYPE_DATA )
		{
			if ( bFromPeer && oHeader.uiChannel < ACoopNet::CHANNEL_NUMOF && TINT( oHeader.uiSize ) == a_iSize - TINT( sizeof( oHeader ) ) )
			{
				s_uiLastHeard = GetTickCount();

				if ( s_afnDataHandlers[ oHeader.uiChannel ] )
					s_afnDataHandlers[ oHeader.uiChannel ]( a_pBuffer + sizeof( oHeader ), oHeader.uiSize );
			}

			return;
		}
	}

	CoopPacket oPacket;

	if ( a_iSize != sizeof( oPacket ) )
		return;

	TUtil::MemCopy( &oPacket, a_pBuffer, sizeof( oPacket ) );

	if ( oPacket.uiMagic != PACKET_MAGIC )
		return;

	switch ( oPacket.uiType )
	{
		case PACKETTYPE_HELLO:
			if ( s_eMode != ACoopNet::MODE_HOST )
				break;

			// One other player: a new one only gets in when the old one is gone
			if ( !bFromPeer && IsPeerAlive() )
				break;

			if ( !bFromPeer )
			{
				TINFO( "[coopnet] player 2 joined from %s:%d\n", inet_ntoa( a_rcFrom.sin_addr ), ntohs( a_rcFrom.sin_port ) );

				s_oPeer           = a_rcFrom;
				s_bHasPeer        = TTRUE;
				s_bHasRemoteState = TFALSE;
			}

			s_uiLastHeard = GetTickCount();
			SendPacket( PACKETTYPE_WELCOME, a_rcFrom );
			break;
		case PACKETTYPE_WELCOME:
			if ( s_eMode != ACoopNet::MODE_JOIN )
				break;

			if ( !bFromPeer )
			{
				if ( IsPeerAlive() )
					break;

				TINFO( "[coopnet] found the host at %s:%d\n", inet_ntoa( a_rcFrom.sin_addr ), ntohs( a_rcFrom.sin_port ) );

				s_oPeer           = a_rcFrom;
				s_bHasPeer        = TTRUE;
				s_bHasRemoteState = TFALSE;
			}

			s_uiLastHeard = GetTickCount();
			break;
		case PACKETTYPE_STATE:
		{
			if ( !bFromPeer )
			{
				// The other game was loading for longer than the timeout (no packets while a level loads) and goes on
				// where it was: it's taken back right away, unless another one is connected
				TBOOL bTakeBack = !IsPeerAlive() && s_eMode == ACoopNet::MODE_HOST;

				if ( !IsPeerAlive() && s_eMode == ACoopNet::MODE_JOIN )
					bTakeBack = s_oTarget.sin_addr.s_addr == INADDR_BROADCAST || ( s_oTarget.sin_addr.s_addr == a_rcFrom.sin_addr.s_addr && s_oTarget.sin_port == a_rcFrom.sin_port );

				if ( !bTakeBack )
					break;

				TINFO( "[coopnet] the other player is back (%s:%d)\n", inet_ntoa( a_rcFrom.sin_addr ), ntohs( a_rcFrom.sin_port ) );

				s_oPeer           = a_rcFrom;
				s_bHasPeer        = TTRUE;
				s_bHasRemoteState = TFALSE;

				if ( s_eMode == ACoopNet::MODE_HOST )
					SendPacket( PACKETTYPE_WELCOME, a_rcFrom );
			}

			s_uiLastHeard = GetTickCount();

			// Not an older one than the last (the counter wraps around). A large step back is the other game started again
			const TINT iStep = TINT16( oPacket.uiSequence - s_oRemote.uiSequence );

			if ( s_bHasRemoteState && iStep <= 0 && iStep > -300 )
				break;

			if ( !std::isfinite( oPacket.afPosition[ 0 ] ) || !std::isfinite( oPacket.afPosition[ 1 ] ) || !std::isfinite( oPacket.afPosition[ 2 ] ) ||
			     !std::isfinite( oPacket.afDirection[ 0 ] ) || !std::isfinite( oPacket.afDirection[ 1 ] ) || !std::isfinite( oPacket.fSpeed ) ||
			     TMath::Abs( oPacket.afPosition[ 0 ] ) > 100000.0f || TMath::Abs( oPacket.afPosition[ 1 ] ) > 100000.0f || TMath::Abs( oPacket.afPosition[ 2 ] ) > 100000.0f )
				break;

			// The velocity on the XZ plane between the last two states (not across a jump of the position or the area)
			const DWORD uiNow = GetTickCount();

			if ( s_bHasRemoteState && GetPacketArea( oPacket ) == GetPacketArea( s_oRemote ) && iStep > 0 )
			{
				const TFLOAT fTime = TFLOAT( uiNow - s_uiRemoteStateTime ) * 0.001f;
				const TFLOAT fX    = oPacket.afPosition[ 0 ] - s_oRemote.afPosition[ 0 ];
				const TFLOAT fZ    = oPacket.afPosition[ 2 ] - s_oRemote.afPosition[ 2 ];

				if ( fTime > 0.008f && fTime < 0.25f && fX * fX + fZ * fZ < 4.0f )
				{
					s_vRemoteVelocity.x += ( fX / fTime - s_vRemoteVelocity.x ) * 0.5f;
					s_vRemoteVelocity.z += ( fZ / fTime - s_vRemoteVelocity.z ) * 0.5f;
				}
				else if ( fX * fX + fZ * fZ >= 4.0f )
				{
					s_vRemoteVelocity = TVector4::VEC_ZERO;
				}

				// Its own speed bounds the estimate (a late state isn't a burst of speed)
				const TFLOAT fMaxSpeed = TMath::Max( oPacket.fSpeed, 0.0f ) + 1.0f;
				const TFLOAT fSpeedSq  = s_vRemoteVelocity.x * s_vRemoteVelocity.x + s_vRemoteVelocity.z * s_vRemoteVelocity.z;

				if ( fSpeedSq > fMaxSpeed * fMaxSpeed )
				{
					const TFLOAT fScale = fMaxSpeed / TMath::Sqrt( fSpeedSq );
					s_vRemoteVelocity.x *= fScale;
					s_vRemoteVelocity.z *= fScale;
				}
			}
			else
			{
				s_vRemoteVelocity = TVector4::VEC_ZERO;
			}

			s_oRemote           = oPacket;
			s_oRemote.fSpeed    = TMath::Min( TMath::Max( oPacket.fSpeed, 0.0f ), 30.0f );
			s_bHasRemoteState   = TTRUE;
			s_uiRemoteStateTime = uiNow;
			break;
		}
		case PACKETTYPE_BYE:
			if ( bFromPeer )
			{
				TINFO( "[coopnet] the other player left\n" );
				s_bHasPeer        = TFALSE;
				s_bHasRemoteState = TFALSE;
			}

			break;
	}
}

static void Receive()
{
	const TBOOL bDevNetwork = s_iDevLossPercent > 0 || s_iDevDelayMs > 0 || s_iDevJitterMs > 0;

	// A handful per frame is plenty for one peer
	for ( TINT i = 0; i < 64; i++ )
	{
		// Large enough for any datagram of the peer: a larger one fails with WSAEMSGSIZE and is dropped
		alignas( 4 ) char aBuffer[ sizeof( CoopDataHeader ) + ACoopNet::MAX_DATA_SIZE ];

		sockaddr_in oFrom     = {};
		int         iFromSize = sizeof( oFrom );

		const int iSize = recvfrom( s_hSocket, aBuffer, sizeof( aBuffer ), 0, TREINTERPRETCAST( sockaddr*, &oFrom ), &iFromSize );

		if ( iSize == SOCKET_ERROR )
		{
			// Too large for the buffer: dropped, the next one may be fine
			if ( WSAGetLastError() == WSAEMSGSIZE )
				continue;

			// Nothing waiting (WSAEWOULDBLOCK), or the last packet couldn't be delivered (WSAECONNRESET): go on next frame
			break;
		}

		if ( !bDevNetwork )
		{
			HandleDatagram( aBuffer, iSize, oFrom );
			continue;
		}

		// Development: a bad network
		if ( s_iDevLossPercent > 0 && TINT( DevRandom() % 100 ) < s_iDevLossPercent )
		{
			s_iDevDropped++;
			continue;
		}

		if ( s_iDevDelayMs <= 0 && s_iDevJitterMs <= 0 )
		{
			HandleDatagram( aBuffer, iSize, oFrom );
			continue;
		}

		if ( !s_pDelayed )
			s_pDelayed = new DelayedDatagram[ MAX_DELAYED ];

		if ( s_iNumDelayed >= MAX_DELAYED )
		{
			s_iDevDropped++;
			continue;
		}

		DelayedDatagram& rDelayed = s_pDelayed[ s_iNumDelayed++ ];
		rDelayed.uiReleaseTime    = GetTickCount() + DWORD( s_iDevDelayMs ) + ( s_iDevJitterMs > 0 ? DWORD( DevRandom() % DWORD( s_iDevJitterMs + 1 ) ) : 0 );
		rDelayed.oFrom            = oFrom;
		rDelayed.iSize            = iSize;
		TUtil::MemCopy( rDelayed.aData, aBuffer, iSize );
	}

	if ( !s_pDelayed )
		return;

	// The held back datagrams whose time has come, the earliest first (the jitter reorders them)
	for ( ;; )
	{
		const DWORD uiNow    = GetTickCount();
		TINT        iEarliest = -1;

		for ( TINT i = 0; i < s_iNumDelayed; i++ )
		{
			if ( TINT( uiNow - s_pDelayed[ i ].uiReleaseTime ) >= 0 && ( iEarliest < 0 || TINT( s_pDelayed[ i ].uiReleaseTime - s_pDelayed[ iEarliest ].uiReleaseTime ) < 0 ) )
				iEarliest = i;
		}

		if ( iEarliest < 0 )
			break;

		alignas( 4 ) char aBuffer[ sizeof( CoopDataHeader ) + ACoopNet::MAX_DATA_SIZE ];
		const TINT        iSize = s_pDelayed[ iEarliest ].iSize;
		const sockaddr_in oFrom = s_pDelayed[ iEarliest ].oFrom;
		TUtil::MemCopy( aBuffer, s_pDelayed[ iEarliest ].aData, iSize );

		s_pDelayed[ iEarliest ] = s_pDelayed[ --s_iNumDelayed ];

		HandleDatagram( aBuffer, iSize, oFrom );
	}
}

static void Send( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer )
{
	if ( s_bHasPeer && !IsPeerAlive() )
	{
		TINFO( "[coopnet] lost the other player\n" );
		s_bHasPeer        = TFALSE;
		s_bHasRemoteState = TFALSE;
	}

	if ( !s_bHasPeer )
	{
		// The joining game keeps asking for the host
		if ( s_eMode == ACoopNet::MODE_JOIN )
		{
			s_fHelloWait -= a_fDeltaTime;

			if ( s_fHelloWait <= 0.0f )
			{
				s_fHelloWait = 1.0f;

				if ( s_oTarget.sin_addr.s_addr == INADDR_BROADCAST )
					SendHelloToTheLAN();
				else
					SendPacket( PACKETTYPE_HELLO, s_oTarget );
			}
		}

		return;
	}

	s_fSendWait -= a_fDeltaTime;

	if ( s_fSendWait > 0.0f )
		return;

	s_fSendWait = 1.0f / ACoopNet::SEND_RATE;

	CoopPacket oState = {};

	if ( a_pLocalPlayer )
	{
		ASteer* pSteer = a_pLocalPlayer->GetSteer();

		TVector4 vPosition;
		pSteer->CopyPosition( vPosition );

		TVector4 vDirection;
		pSteer->CopyDirection( vDirection );

		// The speed on the XZ plane since the last state (the steers without kinematics, the bike's, can't measure it)
		const DWORD uiNow  = GetTickCount();
		TFLOAT      fSpeed = 0.0f;

		if ( s_bHasLastSent && uiNow != s_uiLastSentTime )
		{
			const TFLOAT fX = vPosition.x - s_vLastSentPosition.x;
			const TFLOAT fZ = vPosition.z - s_vLastSentPosition.z;
			fSpeed          = TMath::Sqrt( fX * fX + fZ * fZ ) / ( TFLOAT( uiNow - s_uiLastSentTime ) * 0.001f );

			// A jump of the position isn't a speed
			if ( fSpeed > 30.0f )
				fSpeed = 0.0f;
		}

		if ( pSteer->GetKinematics() )
			fSpeed = pSteer->GetMeasuredSpeed();

		s_vLastSentPosition = vPosition;
		s_uiLastSentTime    = uiNow;
		s_bHasLastSent      = TTRUE;

		oState.uiFlags          = TUINT8( PACKETFLAGS_IN_WORLD | ( ( s_eLocalArea << PACKETFLAGS_AREA_SHIFT ) & PACKETFLAGS_AREA_MASK ) );
		oState.afPosition[ 0 ]  = vPosition.x;
		oState.afPosition[ 1 ]  = vPosition.y;
		oState.afPosition[ 2 ]  = vPosition.z;
		oState.afDirection[ 0 ] = vDirection.x;
		oState.afDirection[ 1 ] = vDirection.z;
		oState.fSpeed           = fSpeed;
	}
	else
	{
		s_bHasLastSent = TFALSE;
	}

	SendPacket( PACKETTYPE_STATE, s_oPeer, &oState );
}

//-----------------------------------------------------------------------------
// Command line: "-nethost [port]", "-netjoin [address[:port]]", "-netlog"
//-----------------------------------------------------------------------------

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

static TBOOL IsLogEnabled()
{
	static TBOOL s_bParsed = TFALSE;
	static TBOOL s_bLog    = TFALSE;

	if ( !s_bParsed )
	{
		s_bParsed = TTRUE;
		s_bLog    = FindSwitch( "-netlog" );
	}

	return s_bLog;
}

// Starts the session the command line asks for, once
static void StartFromCommandLine()
{
	static TBOOL s_bParsed = TFALSE;

	if ( s_bParsed )
		return;

	s_bParsed = TTRUE;

	if ( IsLogEnabled() )
		SetUnhandledExceptionFilter( CoopCrashFilter );

	const TCHAR* szArgs = TNULL;

#ifndef TOSHI_FINAL
	// Development: a bad network ("-netloss 20 -netdelay 80 -netjitter 60")
	if ( FindSwitch( "-netloss", &szArgs ) )
		s_iDevLossPercent = TMath::Min( TMath::Max( atoi( szArgs ), 0 ), 100 );

	if ( FindSwitch( "-netdelay", &szArgs ) )
		s_iDevDelayMs = TMath::Min( TMath::Max( atoi( szArgs ), 0 ), 5000 );

	if ( FindSwitch( "-netjitter", &szArgs ) )
		s_iDevJitterMs = TMath::Min( TMath::Max( atoi( szArgs ), 0 ), 5000 );

	if ( s_iDevLossPercent > 0 || s_iDevDelayMs > 0 || s_iDevJitterMs > 0 )
	{
		s_uiDevRandom ^= GetCurrentProcessId() * 2654435761u;
		// (No per cent sign: the log formats its line once more)
		TINFO( "[coopnet] development network: %d in 100 of the datagrams that come in are lost, the others %d ms late (+ up to %d ms)\n", s_iDevLossPercent, s_iDevDelayMs, s_iDevJitterMs );
	}
#endif

	if ( FindSwitch( "-nethost", &szArgs ) )
	{
		while ( *szArgs == ' ' ) szArgs++;

		const TINT iPort = ( *szArgs >= '0' && *szArgs <= '9' ) ? atoi( szArgs ) : ACoopNet::DEFAULT_PORT;

		s_bStartedByCommandLine = ACoopNet::Host( iPort );
	}
	else if ( FindSwitch( "-netjoin", &szArgs ) )
	{
		while ( *szArgs == ' ' ) szArgs++;

		// "address", "address:port" or nothing (the whole LAN is asked)
		TCHAR szAddress[ 64 ];
		TINT  iLength = 0;
		TINT  iPort   = ACoopNet::DEFAULT_PORT;

		while ( ( ( *szArgs >= '0' && *szArgs <= '9' ) || *szArgs == '.' ) && iLength < TARRAYSIZE( szAddress ) - 1 )
			szAddress[ iLength++ ] = *szArgs++;

		szAddress[ iLength ] = '\0';

		if ( *szArgs == ':' )
			iPort = atoi( szArgs + 1 );

		s_bStartedByCommandLine = ACoopNet::Join( szAddress, iPort );
	}
}

//-----------------------------------------------------------------------------
// ACoopNet
//-----------------------------------------------------------------------------

TBOOL ACoopNet::Host( TINT a_iPort )
{
	Stop();

	if ( a_iPort <= 0 || a_iPort > 65535 )
		a_iPort = DEFAULT_PORT;

	if ( !OpenSocket( a_iPort ) )
		return TFALSE;

	s_eMode = MODE_HOST;
	s_iPort = a_iPort;

	TINFO( "[coopnet] hosting on UDP port %d\n", a_iPort );
	return TTRUE;
}

TBOOL ACoopNet::Join( const TCHAR* a_szAddress, TINT a_iPort )
{
	Stop();

	if ( a_iPort <= 0 || a_iPort > 65535 )
		a_iPort = DEFAULT_PORT;

	// Any free port of this computer
	if ( !OpenSocket( 0 ) )
		return TFALSE;

	s_oTarget                 = {};
	s_oTarget.sin_family      = AF_INET;
	s_oTarget.sin_port        = htons( u_short( a_iPort ) );
	s_oTarget.sin_addr.s_addr = INADDR_BROADCAST;

	if ( a_szAddress && a_szAddress[ 0 ] != '\0' )
	{
		const unsigned long ulAddress = inet_addr( a_szAddress );

		if ( ulAddress == INADDR_NONE )
		{
			TERROR( "[coopnet] '%s' isn't an address like 192.0.2.123\n", a_szAddress );
			closesocket( s_hSocket );
			s_hSocket = INVALID_SOCKET;
			return TFALSE;
		}

		s_oTarget.sin_addr.s_addr = ulAddress;
	}

	s_eMode      = MODE_JOIN;
	s_iPort      = a_iPort;
	s_fHelloWait = 0.0f;

	TINFO( "[coopnet] looking for the host at %s:%d\n", ( s_oTarget.sin_addr.s_addr == INADDR_BROADCAST ) ? "the whole LAN" : inet_ntoa( s_oTarget.sin_addr ), a_iPort );
	return TTRUE;
}

void ACoopNet::Stop()
{
	if ( s_eMode == MODE_OFF )
		return;

	if ( s_hSocket != INVALID_SOCKET )
	{
		if ( s_bHasPeer )
			SendPacket( PACKETTYPE_BYE, s_oPeer );

		closesocket( s_hSocket );
		s_hSocket = INVALID_SOCKET;
	}

	RemoveRemoteUnit();

	s_eMode                 = MODE_OFF;
	s_bHasPeer              = TFALSE;
	s_bHasRemoteState       = TFALSE;
	s_bStartedByCommandLine = TFALSE;
	s_eLocalArea            = AREA_NONE;
	s_bHasLastSent          = TFALSE;

	ACoopAnim::OnSessionEnded();
	ACoopPartner::OnSessionEnded();

	TINFO( "[coopnet] session ended\n" );
}

ACoopNet::MODE ACoopNet::GetMode()
{
	return s_eMode;
}

TBOOL ACoopNet::IsConnected()
{
	return s_eMode != MODE_OFF && IsPeerAlive();
}

TBOOL ACoopNet::IsForcedByCommandLine()
{
	return s_bStartedByCommandLine;
}

void ACoopNet::GetStatusText( TWCHAR* a_wszOut, TINT a_iSize )
{
	a_wszOut[ 0 ] = L'\0';

	// The texts are texts of the mod (AModStrings), the address goes into the %s of the text
	MODSTRING eText = MODSTRING_NUMOF;
	TCHAR     szAddress[ 64 ];
	szAddress[ 0 ] = '\0';

	if ( s_eMode == MODE_HOST )
	{
		GetLocalAddress( szAddress, sizeof( szAddress ) );

		if ( IsPeerAlive() )
		{
			eText = MODSTRING_COOPNET_HOSTING_CONNECTED;
			TStringManager::String8Copy( szAddress, inet_ntoa( s_oPeer.sin_addr ), sizeof( szAddress ) );
		}
		else
		{
			eText = ( szAddress[ 0 ] != '\0' ) ? MODSTRING_COOPNET_HOSTING_ON : MODSTRING_COOPNET_HOSTING;
		}
	}
	else if ( s_eMode == MODE_JOIN )
	{
		if ( IsPeerAlive() )
		{
			eText = MODSTRING_COOPNET_CONNECTED;
			TStringManager::String8Copy( szAddress, inet_ntoa( s_oPeer.sin_addr ), sizeof( szAddress ) );
		}
		else
		{
			eText = MODSTRING_COOPNET_LOOKING;
		}
	}

	if ( eText == MODSTRING_NUMOF )
		return;

	// The address is plain ASCII
	TWCHAR wszAddress[ 64 ];
	TINT   i = 0;

	for ( ; szAddress[ i ] != '\0' && i < TARRAYSIZE( wszAddress ) - 1; i++ )
		wszAddress[ i ] = TWCHAR( szAddress[ i ] );

	wszAddress[ i ] = L'\0';

	TStringManager::String16Format( a_wszOut, a_iSize, AModStrings::Get( eText ), wszAddress );
}

void ACoopNet::Update( TFLOAT a_fDeltaTime )
{
	StartFromCommandLine();

	if ( s_eMode == MODE_OFF || s_hSocket == INVALID_SOCKET )
		return;

	AUnit* pLocalPlayer = TNULL;
	s_eLocalArea        = FindLocalArea( &pLocalPlayer );

	Receive();

	for ( TINT i = 0; i < s_iNumUpdateHandlers; i++ )
		s_afnUpdateHandlers[ i ]( a_fDeltaTime );

	Send( a_fDeltaTime, pLocalPlayer );
	UpdateRemoteUnit( a_fDeltaTime, pLocalPlayer );

	// The pose of the remote cow (its animations, its model, what it carries) and of this game's cow for the other one
	AUnit* pRemote = GetRemoteUnit();
	ACoopAnim::Update( a_fDeltaTime, pLocalPlayer, pRemote );

	// A puppet of another entity follows the remote cow's animation (ASkins, after ACoopAnim has set it)
	if ( pRemote )
		ASkins::UpdateRemotePuppet( pRemote, a_fDeltaTime );

	// The partner's markers, status and what the two players do to each other
	ACoopPartner::Update( a_fDeltaTime, pLocalPlayer, pRemote );

	if ( IsLogEnabled() )
	{
		s_fLogWait -= a_fDeltaTime;

		if ( s_fLogWait <= 0.0f )
		{
			s_fLogWait = 2.0f;

			if ( pLocalPlayer && pRemote && pRemote->GetSteer() )
			{
				const TVector4 vLocal  = *pLocalPlayer->GetSteer()->GetPosition();
				const TVector4 vRemote = *pRemote->GetSteer()->GetPosition();

				TINFO( "[coopnet] me %.2f %.2f %.2f | other %.2f %.2f %.2f (speed %.2f, area %d)\n", vLocal.x, vLocal.y, vLocal.z, vRemote.x, vRemote.y, vRemote.z, s_oRemote.fSpeed, s_eLocalArea );
			}
			else
			{
				TINFO( "[coopnet] %s, me in area %d, other in area %d, %s\n", IsPeerAlive() ? "connected" : "not connected", s_eLocalArea, GetRemoteArea(), pRemote ? "shown" : "not shown" );
			}

#ifndef TOSHI_FINAL
			if ( s_iDevLossPercent > 0 || s_iDevDelayMs > 0 || s_iDevJitterMs > 0 )
				TINFO( "[coopnet] development network: %d datagrams dropped so far, %d held back\n", s_iDevDropped, s_iNumDelayed );
#endif

			// What goes out a second (UDP payloads; +28 bytes of headers a datagram on the wire)
			const TFLOAT fSeconds = TMath::Max( TFLOAT( GetTickCount() - s_uiCountStart ) * 0.001f, 0.001f );

			if ( s_uiCountStart != 0 )
			{
				TINFO( "[coopnet] sent %.0f bytes/s in %.1f datagrams/s (states %.0f, pose %.0f, look %.0f, story %.0f, info %.0f, events %.0f bytes/s)\n",
				       s_uiSentBytes / fSeconds, s_uiSentDatagrams / fSeconds, ( s_uiSentBytes - s_auiChannelBytes[ 1 ] - s_auiChannelBytes[ 2 ] - s_auiChannelBytes[ 3 ] - s_auiChannelBytes[ 4 ] - s_auiChannelBytes[ 5 ] - s_auiChannelBytes[ 6 ] ) / fSeconds,
				       s_auiChannelBytes[ 2 ] / fSeconds, s_auiChannelBytes[ 1 ] / fSeconds, ( s_auiChannelBytes[ 3 ] + s_auiChannelBytes[ 4 ] ) / fSeconds, s_auiChannelBytes[ 5 ] / fSeconds, s_auiChannelBytes[ 6 ] / fSeconds );
			}

			s_uiSentBytes     = 0;
			s_uiSentDatagrams = 0;
			s_uiCountStart    = GetTickCount();
			TUtil::MemClear( s_auiChannelBytes, sizeof( s_auiChannelBytes ) );
		}
	}
}

ACoopNet::AREA ACoopNet::GetLocalArea()
{
	return ( s_eMode != MODE_OFF ) ? s_eLocalArea : AREA_NONE;
}

ACoopNet::AREA ACoopNet::GetRemoteArea()
{
	return ( s_eMode != MODE_OFF && IsPeerAlive() && s_bHasRemoteState ) ? GetPacketArea( s_oRemote ) : AREA_NONE;
}

AUnit* ACoopNet::GetLocalPlayer()
{
	AUnit* pPlayer = TNULL;
	FindLocalArea( &pPlayer );
	return pPlayer;
}

TFLOAT ACoopNet::GetRemoteStateAge()
{
	if ( !s_bHasRemoteState )
		return 1000.0f;

	return TFLOAT( GetTickCount() - s_uiRemoteStateTime ) * 0.001f;
}

TBOOL ACoopNet::GetRemoteState( TVector4& a_rOutPosition, TVector4& a_rOutDirection, TFLOAT& a_rfOutSpeed )
{
	if ( s_eMode == MODE_OFF || !s_bHasRemoteState || !IsPeerAlive() || !( s_oRemote.uiFlags & PACKETFLAGS_IN_WORLD ) )
		return TFALSE;

	a_rOutPosition.Set( s_oRemote.afPosition[ 0 ], s_oRemote.afPosition[ 1 ], s_oRemote.afPosition[ 2 ], 1.0f );
	a_rOutDirection.Set( s_oRemote.afDirection[ 0 ], 0.0f, s_oRemote.afDirection[ 1 ], 1.0f );
	a_rfOutSpeed = s_oRemote.fSpeed;
	return TTRUE;
}

TINT ACoopNet::GetBarnFloorAt( TFLOAT a_fY )
{
	return GetBarnFloor( a_fY );
}

TBOOL ACoopNet::IsRemotePlayerUnit( const AUnit* a_pUnit )
{
	return a_pUnit && s_pRemoteUnit && a_pUnit == GetRemoteUnit();
}

AUnit* ACoopNet::GetRemotePlayerUnit()
{
	return s_pRemoteUnit ? GetRemoteUnit() : TNULL;
}

void ACoopNet::RespawnRemotePlayerUnit()
{
	if ( s_pRemoteUnit )
		RemoveRemoteUnit();
}

void ACoopNet::SetDataHandler( CHANNEL a_eChannel, t_DataHandler a_fnHandler )
{
	TASSERT( a_eChannel < CHANNEL_NUMOF );

	if ( a_eChannel < CHANNEL_NUMOF )
		s_afnDataHandlers[ a_eChannel ] = a_fnHandler;
}

void ACoopNet::AddUpdateHandler( t_UpdateHandler a_fnHandler )
{
	for ( TINT i = 0; i < s_iNumUpdateHandlers; i++ )
	{
		if ( s_afnUpdateHandlers[ i ] == a_fnHandler )
			return;
	}

	TASSERT( s_iNumUpdateHandlers < TARRAYSIZE( s_afnUpdateHandlers ) );

	if ( a_fnHandler && s_iNumUpdateHandlers < TARRAYSIZE( s_afnUpdateHandlers ) )
		s_afnUpdateHandlers[ s_iNumUpdateHandlers++ ] = a_fnHandler;
}

TBOOL ACoopNet::SendData( CHANNEL a_eChannel, const void* a_pData, TINT a_iSize )
{
	if ( s_eMode == MODE_OFF || s_hSocket == INVALID_SOCKET || !IsPeerAlive() )
		return TFALSE;

	if ( a_eChannel >= CHANNEL_NUMOF || a_iSize < 0 || a_iSize > MAX_DATA_SIZE || ( a_iSize > 0 && !a_pData ) )
		return TFALSE;

	alignas( 4 ) char aBuffer[ sizeof( CoopDataHeader ) + MAX_DATA_SIZE ];

	CoopDataHeader oHeader;
	oHeader.uiMagic   = PACKET_MAGIC;
	oHeader.uiType    = PACKETTYPE_DATA;
	oHeader.uiChannel = a_eChannel;
	oHeader.uiSize    = TUINT16( a_iSize );

	TUtil::MemCopy( aBuffer, &oHeader, sizeof( oHeader ) );

	if ( a_iSize > 0 )
		TUtil::MemCopy( aBuffer + sizeof( oHeader ), a_pData, a_iSize );

	const int iSent = sendto( s_hSocket, aBuffer, int( sizeof( oHeader ) + a_iSize ), 0, TREINTERPRETCAST( const sockaddr*, &s_oPeer ), sizeof( s_oPeer ) );

	s_uiSentBytes += TUINT32( sizeof( oHeader ) + a_iSize );
	s_uiSentDatagrams++;
	s_auiChannelBytes[ a_eChannel & 15 ] += TUINT32( sizeof( oHeader ) + a_iSize );
	return iSent != SOCKET_ERROR;
}
