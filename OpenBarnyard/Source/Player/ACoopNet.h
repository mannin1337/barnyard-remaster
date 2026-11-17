#pragma once

#include <Math/TVector4.h>

class AUnit;

//-----------------------------------------------------------------------------
// Purpose: network co-op over a LAN (not in retail). Two running games, each
// with its own player, camera, save and world. They tell each other where
// their player is, and each one shows the other's player as a remote cow.
//
//  - One game hosts (it listens on a UDP port), the other joins: with an
//    address, or without one by asking the whole LAN who hosts (broadcast).
//    After that the two sides do the same thing.
//  - The remote cow is a "player" unit outside of the player sets, moved by the
//    packets through a steer without physics (ASSeqSteer). It plays what the
//    other player's cow plays (ACoopAnim, CHANNEL_ANIMATION). It only exists
//    while both players are in the same area (AREA_*: the farm, the barn by day,
//    the barn at night). It has the gender and the breed of the remote player's
//    cow (ASkins, CHANNEL_APPEARANCE). It is an obstacle for the local player:
//    each game moves only its own cow (the remote one is never pushed into the
//    local one, the local one bumps into it with its own physics).
//  - What the two players do to each other, the HUD of the partner and the
//    emotes are in ACoopPartner. The quests, the characters, the items and the
//    time of day belong to each game (the story: ACoopProgress).
//
// Turned on by the Co-op menu of the frontend (AFrontEndCoopState) or by
// "-nethost [port]" / "-netjoin [address[:port]]" on the command line.
//-----------------------------------------------------------------------------
class ACoopNet
{
public:
	enum MODE
	{
		MODE_OFF,
		MODE_HOST,
		MODE_JOIN,
	};

	// Where a player walks around: the two cows see each other in the same area
	enum AREA : TUINT8
	{
		AREA_NONE,      // elsewhere: the menus, loading, a minigame, a scene...
		AREA_FARM,      // the farm (ASimState on EnvMain)
		AREA_BARN_DAY,  // the barn explore state by day (EnvBarnIntDay)
		AREA_BARN_NIGHT // the barn explore state at night (EnvBarnInt)
	};

	inline static constexpr TINT DEFAULT_PORT = 27815;

	// States sent per second
	inline static constexpr TFLOAT SEND_RATE = 30.0f;

	// The other game is gone after this long without a packet (seconds). No packets come while a level loads (a
	// minigame, the barn, a save): the remote cow is hidden after STATE_STALE_TIME, the session stays
	inline static constexpr TFLOAT TIMEOUT = 10.0f;

	// The remote cow is hidden after this long without a state of the other game (seconds): it's loading or gone
	inline static constexpr TFLOAT STATE_STALE_TIME = 1.0f;

public:
	// Starts hosting. Returns TFALSE if the port can't be used
	static TBOOL Host( TINT a_iPort = DEFAULT_PORT );

	// Starts looking for the host: at a_szAddress ("192.168.0.5"), or on the whole LAN if it's TNULL or empty
	static TBOOL Join( const TCHAR* a_szAddress = TNULL, TINT a_iPort = DEFAULT_PORT );

	// Ends the session and removes the remote cow
	static void Stop();

	static MODE GetMode();

	// Whether the other game has been heard from lately
	static TBOOL IsConnected();

	// Whether "-nethost" or "-netjoin" is on the command line (the frontend doesn't end that session)
	static TBOOL IsForcedByCommandLine();

	// One line about the session for the frontend, e.g. "Hosting on 192.168.0.5. Waiting for player 2..."
	static void GetStatusText( TWCHAR* a_wszOut, TINT a_iSize );

	// ARootTask::OnUpdate, every frame: receives, sends and moves the remote cow
	static void Update( TFLOAT a_fDeltaTime );

	// Whether a_pUnit is the cow of the remote player
	static TBOOL IsRemotePlayerUnit( const AUnit* a_pUnit );

	// The cow of the remote player, TNULL while it isn't shown
	static AUnit* GetRemotePlayerUnit();

	// Removes the remote cow, the next Update spawns it again (ASkins: the remote player's cow got another gender)
	static void RespawnRemotePlayerUnit();

	// The area of this game's player (AREA_NONE while it isn't walking around in one)
	static AREA GetLocalArea();

	// The area of the other game's player from its last state (AREA_NONE if not connected or not known)
	static AREA GetRemoteArea();

	// This game's player if it walks around in an area (the farm or the barn), TNULL otherwise
	static AUnit* GetLocalPlayer();

	// Seconds since the last state of the other game (a large value if there's none)
	static TFLOAT GetRemoteStateAge();

	// The position and direction of the other game's player in its last state (TFALSE if there's none)
	static TBOOL GetRemoteState( Toshi::TVector4& a_rOutPosition, Toshi::TVector4& a_rOutDirection, TFLOAT& a_rfOutSpeed );

	// The floor of the barn (ABarnExploreMinigameState::FLOOR) at the height a_fY, the way the barn finds the player's
	static TINT GetBarnFloorAt( TFLOAT a_fY );

	//-----------------------------------------------------------------------------
	// Data channels: what other modules exchange besides the position (the look of
	// the cow, the story progress...). Each module owns a channel. A datagram is
	// delivered once or not at all, possibly out of order: send state again every
	// so often rather than relying on one packet.
	//-----------------------------------------------------------------------------

	enum CHANNEL : TUINT8
	{
		CHANNEL_APPEARANCE = 1, // the look of the sender's cow (skin, breed, gender)
		CHANNEL_ANIMATION  = 2, // the pose of the sender's cow: its model, animations and what's attached to it (ACoopAnim)
		CHANNEL_PROGRESS   = 3, // story progress
		CHANNEL_SKIP       = 4, // offers and answers to skip a mission
		CHANNEL_PARTNER    = 5, // who the sender is and what it's doing: name, area, minigame, scene... (ACoopPartner)
		CHANNEL_ACTION     = 6, // one-off events of the sender: squirts, emotes, hits on the other cow (ACoopPartner)
		CHANNEL_MINIGAME   = 8, // turn-based LAN minigames (ACoopMinigame)
		CHANNEL_NUMOF      = 16,
	};

	// Largest payload of one datagram (bytes)
	inline static constexpr TINT MAX_DATA_SIZE = 1200;

	using t_DataHandler   = void ( * )( const void* a_pData, TINT a_iSize );
	using t_UpdateHandler = void ( * )( TFLOAT a_fDeltaTime );

	// a_fnHandler gets each datagram of the channel that comes from the other game (one handler per channel)
	static void SetDataHandler( CHANNEL a_eChannel, t_DataHandler a_fnHandler );

	// a_fnHandler is called every frame by Update while a session runs, after the received data was handled
	// (up to 8 handlers; adding the same one again does nothing)
	static void AddUpdateHandler( t_UpdateHandler a_fnHandler );

	// Sends a_iSize bytes to the other game. TFALSE if there's no other game or the data doesn't fit
	static TBOOL SendData( CHANNEL a_eChannel, const void* a_pData, TINT a_iSize );
};
