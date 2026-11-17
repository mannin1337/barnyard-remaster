#pragma once

#include <Math/TVector4.h>

class AUnit;
class AMilkSquirtProjectileLauncher;

//-----------------------------------------------------------------------------
// Purpose: the other player in network co-op (ACoopNet, not in retail), as this
// game shows it and plays with it:
//  - who the partner is and what they're doing (CHANNEL_PARTNER, twice a second
//    and when it changes): the name of their cow, their area and activity (a
//    minigame and which one, a scene, a conversation, the menu, loading...);
//  - a name tag over the partner's cow and a marker on the minimap (the game's
//    own "player" icon, turned with the partner), and a line at the top of the
//    screen when the partner isn't there: "Partner: playing Chicken Launch";
//  - (stage 2) what the two players do to each other with the game's own moves,
//    the emotes and "Go to partner" (CHANNEL_ACTION).
//
// The info message for other modules (e.g. the LAN minigames):
//   ACoopPartner::GetPartnerActivity() / GetPartnerMiniGame() tell what the other
//   game's player does; ACTIVITY_MINIGAME comes with the index of the minigame
//   (AGameLoader::GetCurrMiniGame() of the sender, AMiniGameManager::GetMiniGame)
//   and its variant (AMiniGameManager::GetMiniGameVariant()).
//-----------------------------------------------------------------------------
class ACoopPartner
{
public:
	// What a player is doing (CHANNEL_PARTNER)
	enum ACTIVITY : TUINT8
	{
		ACTIVITY_UNKNOWN,      // nothing heard yet
		ACTIVITY_FRONTEND,     // in the menus of the frontend, no game is loaded
		ACTIVITY_LOADING,      // a level is loading or the screen fades between two
		ACTIVITY_FREE,         // walks around in an area (ACoopNet::AREA)
		ACTIVITY_MINIGAME,     // plays a minigame (see GetPartnerMiniGame)
		ACTIVITY_SCENE,        // a story scene or a scripted sequence plays
		ACTIVITY_CONVERSATION, // talks to a character
		ACTIVITY_MENU,         // the game is paused in a menu (start menu, map, inventory...)
		ACTIVITY_MICROGAME,    // a micro game of the farm (coins, trees, treasure hunt...)
		ACTIVITY_SHOP,         // the gopher shop
		ACTIVITY_COOKING,      // the kitchen or the cocktail bar
		ACTIVITY_SLEEPING,     // sleeps through the night
		ACTIVITY_NUMOF
	};

	// Info messages sent per second (and when something changes)
	inline static constexpr TFLOAT INFO_RATE = 2.0f;

	// The longest name of a cow sent (with the terminator)
	inline static constexpr TINT MAX_NAME = 16;

public:
	// ACoopNet::Update, every frame of a session after ACoopAnim: the partner's HUD, the info and the actions
	static void Update( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer, AUnit* a_pRemoteUnit );

	// ACoopNet: the remote cow is about to be removed (or went with the level) / the session ended
	static void OnRemoteUnitGone();
	static void OnSessionEnded();

	// What the other game's player does now (ACTIVITY_UNKNOWN while nothing is known)
	static ACTIVITY GetPartnerActivity();

	// The minigame the other game's player plays (index of AMiniGameManager, -1 if none) and its variant
	static TINT GetPartnerMiniGame( TINT* a_pOutVariant = TNULL );

	// The name of the other game's cow ("Partner" while it isn't known)
	static const TWCHAR* GetPartnerName();

	// What this game's player does now
	static ACTIVITY GetLocalActivity();

	//-----------------------------------------------------------------------------
	// Hooks of the game code
	//-----------------------------------------------------------------------------

	// AMilkSquirtProjectileLauncher::Fire: a_pLauncher squirts (a_bCanFire TFALSE: it's out of milk and sputters)
	static void OnSquirt( AMilkSquirtProjectileLauncher* a_pLauncher, TBOOL a_bCanFire );

	// A sound the game plays for a_pUnit outside of its animations (ABikeSteer::PlayHorn: the bell)
	static void OnLocalSound( AUnit* a_pUnit, TINT a_iSound );

	// APlayerSteer::UpdateWater: a_pUnit fell into deep water at a_rcPosition
	static void OnLocalSplash( AUnit* a_pUnit, const Toshi::TVector4& a_rcPosition );

	// AStartMenuState: whether "Go to partner" is offered: in a game with a partner, where this player's cow walks
	// around (the farm, the barn)
	static TBOOL IsGoToPartnerOffered();

	// AStartMenuState, once the menu is gone: takes the cow to the partner's, or says why not in the line at the top.
	//  - The same area (the farm, the same floor of the barn): the screen fades out, the cow is put next to the
	//    partner's, it fades in.
	//  - The partner is in the barn and this cow on the farm, or the other way round: through the barn door with the
	//    game's own transition (AGameStateController::StartGameState / FinishGameState), when the door would let this
	//    cow in (ABarnOpenRequirement) and nothing is going on here (ACoopProgress::IsSafeMoment in a story game).
	//    Out of the barn the cow is then put next to the partner's.
	static void GoToPartner();

	// APlayerControlComponent::ProcessCommand: TTRUE while a pad's emote button is held (its D-pad plays emotes then)
	static TBOOL IsEmoteChordHeld();

	// ASimplePlayerControlComponent::SetMovementEnabled: something sets whether a_pUnit can move. If it's the game
	// while an emote or a hit holds that cow, the movement is the game's to give back from then on
	static void OnMovementEnabledSet( AUnit* a_pUnit );
};
