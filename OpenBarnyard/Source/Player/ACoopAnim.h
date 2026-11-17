#pragma once

#include <Math/TVector4.h>

class AUnit;
class AAnimatableObject;
class AAnimatableObjectType;
class AAnimationBreakpoint;

//-----------------------------------------------------------------------------
// Purpose: the pose of the cows in network co-op (ACoopNet, not in retail).
// Each game sends what the model of its player shows, 30 times a second on
// CHANNEL_ANIMATION: the model (the cow, or the rider while on the bike), its
// animation set, the animations it plays (the main ones and the overlays, with
// their time, speed and weight), its rotation (facing and tilt) and the models
// attached to it (a carried item, the sunglasses, Pip, the bike...) with their
// own animations. The other game shows the same on the remote cow:
//  - the animations are played by name with the game's own transitions and
//    kept in time (jumps, landings, squirting, pickups, ladders, talking...);
//  - the attached models are copies (no units: nothing there can be picked up
//    or pushed, they come and go with the sender's);
//  - another model than the cow's (the rider on the bike) is a puppet model in
//    its place, without the controllers of the sender (nothing reads a bike's
//    physics here), the cow itself is hidden meanwhile;
//  - the breakpoints of the copied animations only play their sounds and
//    particles here: the ones that move items, the inventory or the story belong
//    to the sender's game.
// A pose the remote cow can't show (a model that isn't loaded here) falls back
// to the cow, whose animation then follows its speed as before.
//-----------------------------------------------------------------------------
class ACoopAnim
{
public:
	// Poses sent per second
	inline static constexpr TFLOAT SEND_RATE = 30.0f;

public:
	// ACoopNet::Update, every frame of a session after the remote cow was moved: sends the pose of this game's player
	// (a_pLocalPlayer, TNULL while not in an area) and shows the other game's on the remote cow (a_pRemoteUnit)
	static void Update( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer, AUnit* a_pRemoteUnit );

	// ACoopNet: whether the remote cow may be shown now (its states come, it's on the local player's floor of the barn)
	static void SetRemoteShown( TBOOL a_bShown );

	// AAlienAnimController::OnUpdate for the remote cow: places its model as the sender's is (the steer's position
	// plus the model's offset, the model's rotation). TFALSE while no pose is known: the controller places it and
	// animates it by its speed
	static TBOOL UpdateRemoteModel( AUnit* a_pUnit, AAnimatableObject* a_pObject, TFLOAT a_fDeltaTime );

	// AAnimatableObject::Update: whether a_pBreakpoint of an animation of a_pObject may run. On the remote cow and
	// the models shown with it only the sounds and particles do
	static TBOOL IsBreakpointAllowed( AAnimatableObject* a_pObject, AAnimationBreakpoint* a_pBreakpoint );

	// AAnimatableObject::~AAnimatableObject: a_pObject is gone (the remote cow's model takes the copies attached to it)
	static void OnObjectDeleted( AAnimatableObject* a_pObject );

	// AAnimatableObjectManager::DestroyType: the models of a_pType made here go first
	static void OnTypeDestroyed( AAnimatableObjectType* a_pType );

	// ACoopNet: the remote cow is removed (or went with the level) / the session ended
	static void OnRemoteUnitGone();
	static void OnSessionEnded();

	// The model the partner is shown with right now (the remote cow's or the rider puppet), TNULL if none is shown
	static AAnimatableObject* GetRemoteAvatar();

	// Whether the partner is shown riding the bike (the cow is hidden, the rider puppet is shown)
	static TBOOL IsRemoteRiding();

	// The name of the main animation the partner's model plays (empty if none is known), e.g. for the skins puppets
	static const TCHAR* GetRemoteMainAnimation();

	// Whether the partner's cow is female (its model's type, as the partner's game has it)
	static TBOOL IsRemoteFemale();

	// Whether the local player's pose says it plays a_szAnimation right now (the main or an overlay animation)
	static TBOOL IsLocalPlaying( AUnit* a_pLocalPlayer, const TCHAR* a_szAnimation );
};
