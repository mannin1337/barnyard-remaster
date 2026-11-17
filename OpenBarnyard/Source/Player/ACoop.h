#pragma once

class AUnit;
class APlayerSet;
class AInteriorArea;

namespace Toshi
{
class TInputDevice;
class TInputDeviceController;
} // namespace Toshi

//-----------------------------------------------------------------------------
// Purpose: local co-op in the sim (not in retail). A second human player walks
// around the farm with the first one, on the same screen and with the same camera.
//
// How it fits into the game:
//  - the second player is slot 1 of the sim's player set, its unit is a second
//    "player" unit. The input of the device owned by a slot already goes to that
//    slot's unit only (AControlComponentManager::ProcessInput), every other
//    device (keyboard, mouse, the other controllers) drives the first player.
//  - the story, the camera, the HUD, the inventory and the save belong to the
//    first player. The second player moves and can use the shared interior entrances.
//  - the world around the players is streamed for the camera, so the second
//    player is brought back next to the first one when it gets too far away.
//
// It's turned on by the Co-op menu of the frontend (AFrontEndCoopState) or by
// "-coop [controller index]" on the command line, and only exists in the main
// game and the barn. The shop/cooking screens and other minigames keep their original presentation.
//-----------------------------------------------------------------------------
class ACoop
{
public:
	// The second player is brought back when it's further than this from the first one (metres, on the ground plane).
	// "-cooptether <metres>" on the command line changes it
	inline static constexpr TFLOAT TETHER_DISTANCE = 12.0f;

	// ...or this much higher/lower (fell out of the streamed collision)
	inline static constexpr TFLOAT TETHER_HEIGHT = 20.0f;

	// Where the second player is put: this far from the first one
	inline static constexpr TFLOAT PLACE_DISTANCE = 1.6f;

public:
	// Whether the next main game starts with a second player.
	// Set by the frontend, always TTRUE with "-coop" on the command line.
	static TBOOL IsRequested();
	static void  SetRequested( TBOOL a_bRequested );

	// Whether "-coop" is on the command line
	static TBOOL IsForcedByCommandLine();

	// The controller the second player would get if the game started now, TNULL if there's none:
	// the one picked with "-coop <index>", else the second controller, else the only one
	// (the first player then uses the keyboard and the mouse)
	static Toshi::TInputDeviceController* FindSecondPlayerController();

	// 1-based number of that controller in the input handler's list (for the UI), 0 if there's none
	static TINT GetSecondPlayerControllerNumber();

	// AGameLoader::SpawnSimPlayer, right before the first player's slot is added (AddHumanPlayer inserts in front of
	// the other humans, so the second player's slot has to be added first). a_bMainGame also includes the barn,
	// and is TFALSE for the other users of the sim player. Adds the second player's slot if co-op is requested and returns the controller the first
	// player's slot has to use (a_pFirstPlayerController, or TNULL if the second player got that one).
	static Toshi::TInputDeviceController* OnSpawnSimPlayer( APlayerSet* a_pPlayerSet, Toshi::TInputDeviceController* a_pFirstPlayerController, TBOOL a_bMainGame );

	// ASimState::UpdateSim: spawns the second player's unit once the first player stands in the world,
	// and keeps it close to the first player
	static void Update( TFLOAT a_fDeltaTime );

	// Puts the second player next to the first one right away (APlayerSteer: it fell into deep water)
	static void BringSecondPlayerBack();

	// The unit of the second player, TNULL if there's none
	static AUnit* GetSecondPlayerUnit();

	// Whether a_pUnit is the unit of the second player
	static TBOOL IsSecondPlayerUnit( const AUnit* a_pUnit );

	// Whether a_pDevice is the controller of the second player (while it has a slot in the current player set)
	static TBOOL IsSecondPlayerDevice( const Toshi::TInputDevice* a_pDevice );

	// P2 may initiate only the shared barn entrance/exit and gopher-shop door.
	// The existing interaction still checks locks, quests and the time of day.
	static TBOOL TryStartInteriorDoor( AUnit* a_pUnit, AInteriorArea* a_pArea );

	// AGTAStyleCameraHelper: the unit of the second player if a_pCameraTarget is the unit of the first one
	// (the camera then shows both of them), TNULL otherwise
	static AUnit* GetSharedCameraPartner( const AUnit* a_pCameraTarget );

	// AGTAStyleCameraHelper: how much further the shared camera stays when the players are a_fSeparation apart (metres)
	static TFLOAT GetSharedCameraExtraDistance( TFLOAT a_fSeparation );
};
