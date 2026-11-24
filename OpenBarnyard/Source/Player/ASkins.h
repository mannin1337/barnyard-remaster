#pragma once

class AUnit;
class PBPropertyValue;

namespace Toshi
{
class TTRB;
} // namespace Toshi

//-----------------------------------------------------------------------------
// Hook for the network co-op's animation code (the remote cow):
//
//   ASkins::UpdateRemotePuppet( AUnit* a_pRemoteUnit, TFLOAT a_fDeltaTime )
//
// Call it once a frame for the remote cow, after its animation was set. If the
// other player chose a character instead of the cow, it keeps a puppet of that
// character on the remote cow: the cow is hidden (its skeleton, attachments and
// breakpoints keep working), the puppet takes its place and plays its current
// animation by name (sprint, run, walk and idle stand in for the ones it
// lacks). It only reads the cow, which stays a normal "player" unit. Calling it
// more than once a frame does nothing more, with the cow skin it does nothing.
//
// The puppet follows the cow right before the models are drawn: its model
// transform, its main animation (AToshiAnimationInterface::GetAnim and its
// transition) and its visibility (AVisibleUnit::SetVisible: while the cow is
// hidden, e.g. on the bike, the puppet is too). Only the farm's remote cow, the
// one ASkins::OnRemoteUnitSpawned was told about, gets one: a unit spawned
// without it (the barn) is left alone, so is any other unit passed in.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: the look of the player's cow (not in retail), chosen in the Skins
// tab of the Mods window (Alt + ~) or with the console variables player.gender,
// player.breed and player.skin, and the look of the other player's cow in the
// network co-op (ACoopNet).
//
//  - The gender and the breed are the main profile's, so the save, the
//    portraits, the minigames and the cutscenes follow them. A breed is put
//    on the cow right away (the 14 hides are loaded with the farm). A gender
//    needs the other models: the farm is loaded again, through the quick save
//    the game uses when the player leaves the farm for a minigame.
//  - A character (the skin) other than the cow is a puppet: the player's cow
//    still does everything, hidden, and a model of the character stands in its
//    place and plays its animations by name. Its assets are loaded when it's
//    chosen and stay until the farm is unloaded. Only on the farm: the bike,
//    the barn, the minigames and the cutscenes show the cow. The skin is a
//    setting of this computer (the registry), the Skins tab saves it.
//  - The look goes to the other game on ACoopNet::CHANNEL_APPEARANCE. The
//    remote cow gets the model of its gender, a hide of its own (the skin
//    material of a model is shared by every cow of that gender, so while the
//    remote cow is drawn its meshes use a copy of it with the other hide) and
//    the puppet of its character.
//  - Changes are made on the farm. The ones asked for elsewhere (the console,
//    the command line) wait for it.
//-----------------------------------------------------------------------------
class ASkins
{
public:
	inline static constexpr TINT NUM_GENDERS = 2;
	inline static constexpr TINT NUM_BREEDS  = 7;

	// Longest skin name (player.skin), with its terminator
	inline static constexpr TINT MAX_SKIN_NAME = 24;

	// The skin of the cow itself (with the gender and the breed)
	inline static constexpr const TCHAR* COW_SKIN = "cow";

public:
	// The breeds are the costumes of the profile: 0 American, 1 Angus, 2 Beefmaster, 3 Spaniard, 4 Longhorn, 5 Brahman, 6 Holstein
	static const TCHAR* GetBreedName( TINT a_iBreed );
	static const TCHAR* GetGenderName( TINT a_iGender );

	// ARootTask::OnUpdate, every frame: does the changes asked for and keeps the console variables up to date
	static void Update( TFLOAT a_fDeltaTime );

	// Asks for another gender / breed of the player's cow. Done on the farm, a new gender loads the farm again.
	// The Skins tab and the console variables call them
	static void RequestGender( TINT a_iGender );
	static void RequestBreed( TINT a_iBreed );

	// The character of the player: "cow", one of the Skins tab or the name of a loaded model type. As the Skins tab
	// does: it's saved in the registry. TFALSE if it's neither one of the Skins tab nor a loaded model type (it's
	// taken anyway, such a type may be loaded later)
	static TBOOL ChooseSkin( const TCHAR* a_szSkin );

	// The skin of the registry (HKEY_CURRENT_USER\Software\THQ\Barnyard, Skin), "cow" if none was saved. Read once,
	// it's the start value of player.skin (APrimeDebug::RegisterSettings)
	static const TCHAR* GetSavedSkin();

	// The characters of the Skins tab, the cow first: their player.skin names and their labels
	static TINT         GetNumSkins();
	static const TCHAR* GetSkinName( TINT a_iIndex );
	static const TCHAR* GetSkinLabel( TINT a_iIndex );

	// The Skins tab of the Mods window (AImGui::RenderSettings)
	static void RenderImGui();

	// AAssetLoader, lib_maingame: after the assets of the player character were loaded (a_pPlayerCharacter is the
	// "playercharacter" property of the library) / after they were unloaded
	static void OnMainGameAssetsLoaded( const PBPropertyValue* a_pPlayerCharacter, Toshi::TTRB* a_pTRB );
	static void OnMainGameAssetsUnloaded();

	// The existing BYS1 channel advertises the saved cow behind any farm-only puppet.
	static TBOOL GetRemoteCowAppearance( TINT& a_rGender, TINT& a_rBreed );

	// Mud's two human units each draw their own hide, even when they share a model.
	static TBOOL ApplyMudHide( TINT a_iSlot, AUnit* a_pUnit, TINT a_iGender, TINT a_iBreed );
	static void  DropMudHide( TINT a_iSlot );
	static void  ReleaseMudHides();

	// ACoopNet::SpawnRemoteUnit: the model index of the "player" unit to spawn for the remote cow (the one of the
	// remote player's gender if its models can be loaded, the local player's otherwise)
	static TINT GetRemoteModelIndex();

	// ACoopNet::SpawnRemoteUnit: the remote cow was spawned (it gets the remote player's hide)
	static void OnRemoteUnitSpawned( AUnit* a_pUnit );

	// The hook described at the top of this file
	static void UpdateRemotePuppet( AUnit* a_pRemoteUnit, TFLOAT a_fDeltaTime );

public:
	// The storage of the console variables player.gender and player.breed (APrimeDebug::RegisterSettings). They show
	// the look of the cow (-1 before a game is loaded), setting them calls RequestGender / RequestBreed
	inline static TINT ms_iGenderVar = -1;
	inline static TINT ms_iBreedVar  = -1;
};
