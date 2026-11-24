#include "pch.h"
#include "ASkins.h"
#include "ACoopNet.h"
#include "APlayerManager.h"
#include "APlayerProfileManager.h"
#include "Animation/AAnimatableObjectManager.h"
#include "Animation/AModel.h"
#include "Animation/AToshiAnimationInterface.h"
#include "Assets/AAssetLoader.h"
#include "Assets/AKeyFrameLibraryManager.h"
#include "Assets/AMaterialLibraryManager.h"
#include "GameInterface/AGameStateController.h"
#include "GameInterface/ASimState.h"
#include "Helpers/ABikeManager.h"
#include "Helpers/AGameLoader.h"
#include "Helpers/ASkinLightingManager.h"
#include "MiniGame/AGateCrashAvatar.h"
#include "Render/ARenderer.h"
#include "Render/ASkinShader/ASkinShader.h"
#include "Render/ASkinShader/ASkinMaterial.h"
#include "Tasks/ARootTask.h"
#include "Units/AAnimatedUnit.h"
#include "Units/AUnitManager.h"
#include "Units/AUnitModelStaticData.h"
#include "Units/AUnitType.h"
#include "UnitMovement/Steering/APlayerSteer.h"

#include <Render/TModel.h>
#include <Toshi/TGlobalEmitter.h>
#include <Toshi/TScheduler.h>
#include <Plugins/PPropertyParser/PBProperties.h>

// Defines PRIMEDEBUG (the console, player.skin) in the builds that have it: included in every build
#include "Debug/APrimeDebug.h"

#if defined( TRENDERINTERFACE_DX8 ) || defined( TRENDERINTERFACE_DX11 )
#  define ASKINS_IMGUI
#  include <imgui.h>
#endif

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TPSTRING8_DECLARE( sprint_lp );
TPSTRING8_DECLARE( run_lp );
TPSTRING8_DECLARE( walk_lp );
TPSTRING8_DECLARE( idle_lp );

static const TCHAR* s_aBreedNames[ ASkins::NUM_BREEDS ] = {
	"American",
	"Angus",
	"Beefmaster",
	"Spaniard",
	"Longhorn",
	"Brahman",
	"Holstein",
};

// The hides are the material libraries p<m|f><costume>, named as AAssetLoader::LoadPlayerCharacter builds them
static const TCHAR* s_aCostumeNames[ ASkins::NUM_BREEDS ] = {
	"american",
	"angus",
	"beefmstr",
	"spaniard",
	"longhorn",
	"brahman",
	"holsten",
};

static const TCHAR* s_aGenderNames[ ASkins::NUM_GENDERS ] = {
	"Male",
	"Female",
};

static TBOOL IsGender( TINT a_iGender )
{
	return a_iGender == APlayerProfile::GENDER_MALE || a_iGender == APlayerProfile::GENDER_FEMALE;
}

static TBOOL IsBreed( TINT a_iBreed )
{
	return a_iBreed >= 0 && a_iBreed < ASkins::NUM_BREEDS;
}

//-----------------------------------------------------------------------------
// The characters (player.skin)
//-----------------------------------------------------------------------------

// A character the player can be: a model type of the game, the keyframe library of its model's skeleton and a
// material library with its textures (found in the game's data, the farm may have them loaded already). The
// puppet plays the cow's animations it has by name, sprint, run, walk and idle stand in for the others
struct SkinInfo
{
	const TCHAR* szName;   // player.skin, the network
	const TCHAR* szLabel;  // the Skins tab
	const TCHAR* szMoves;  // what its animations show, for the Skins tab
	const TCHAR* szType;   // Data/ModelTypes/<type>.trb
	const TCHAR* szKeylib; // Data/Models/<keylib>.tkl
	const TCHAR* szMatlib; // Data/Matlibs/<matlib>.ttl
};

static const SkinInfo s_aSkins[] = {
	{ "cow", "Your cow", "everything", TNULL, TNULL, TNULL },
	// The cows of the chasing chicks minigame have the player's animations
	{ "otis", "Otis", "walks, runs, jumps", "cc2_otis", "CC2_char", "SS_Otis" },
	{ "bud", "Bud", "walks, runs, jumps", "cc2_bud", "CC2_char", "SS_Bud" },
	{ "eddie", "Eddie", "walks, runs, jumps", "cc2_eddie", "CC2_char", "SS_Eddie" },
	{ "igg", "Igg", "walks, runs, jumps", "cc2_igg", "CC2_char", "SS_Igg" },
	{ "daisy", "Daisy", "walks, runs", "daisy", "cow_anims_golf", "SS_Daisy" },
	{ "bessy", "Bessy", "walks, runs", "bessy", "cow_anims_golf", "SS_Bessy" },
	{ "root", "Root", "walks, runs", "root", "root", "root" },
	{ "chick", "Chick", "walks, runs", "chick", "cow_anims", "cow_anims" },
	{ "coyote", "Coyote", "walks, runs", "generic_coyote", "genchar_common", "SS_Gen_coyote" },
	{ "hanna", "Hanna", "walks", "hanna", "cow_anims4", "cow_anims4" },
	{ "ben", "Ben", "walks", "ben", "cow_anims3", "SS_Ben" },
	{ "farmer", "The farmer", "walks", "farmer", "farmer", "SS_Farmer" },
	{ "goat", "A goat", "walks", "generic_goat", "genchar_common", "SSMJ" },
	{ "hen", "A hen", "walks", "generic_hen", "genchar_common", "SS_GenHen" },
	{ "sheep", "A sheep", "walks", "generic_sheep", "generic_char", "generic_char" },
	{ "horse", "A horse", "walks", "generic_horse", "generic_char", "SSMJ" },
	{ "pig", "Pig", "stands", "pig", "cow_anims4", "SS_Pig" },
	{ "freddy", "Freddy", "stands", "freddy", "cow_anims2", "SS_freddy" },
	{ "miles", "Miles", "stands", "miles", "barnsim_common", "SS_Miles" },
	{ "duke", "Duke", "stands", "duke", "barnsim_common", "SSPool" },
	{ "peck", "Peck", "stands", "peck", "barnsim_common", "SS_Peck" },
};

static constexpr TINT NUM_SKINS = TARRAYSIZE( s_aSkins );

static TBOOL IsCowSkin( const TCHAR* a_szSkin )
{
	return !a_szSkin || a_szSkin[ 0 ] == '\0' || TStringManager::String8Compare( a_szSkin, ASkins::COW_SKIN ) == 0;
}

// The index of the skin in s_aSkins, -1 if it isn't one of them
static TINT FindSkin( const TCHAR* a_szSkin )
{
	for ( TINT i = 0; i < NUM_SKINS; i++ )
	{
		if ( TStringManager::String8Compare( a_szSkin, s_aSkins[ i ].szName ) == 0 )
			return i;
	}

	return -1;
}

// A skin name as it's used: lower case letters, digits and '_', "cow" if nothing is left
static void CleanSkinName( TCHAR* a_szOut, const TCHAR* a_szName )
{
	TINT iLength = 0;

	for ( TINT i = 0; a_szName[ i ] != '\0' && i < ASkins::MAX_SKIN_NAME - 1 && iLength < ASkins::MAX_SKIN_NAME - 1; i++ )
	{
		TCHAR c = a_szName[ i ];

		if ( c >= 'A' && c <= 'Z' )
			c = TCHAR( c - 'A' + 'a' );

		if ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' )
			a_szOut[ iLength++ ] = c;
	}

	a_szOut[ iLength ] = '\0';

	if ( iLength == 0 )
		TStringManager::String8Copy( a_szOut, ASkins::COW_SKIN );
}

//-----------------------------------------------------------------------------
// The player's cow
//-----------------------------------------------------------------------------

// What was asked for, -1: nothing
static TINT s_iWantedGender = -1;
static TINT s_iWantedBreed  = -1;

// The character of the player (player.skin, the Skins tab)
static TCHAR s_szSkin[ ASkins::MAX_SKIN_NAME ] = "cow";

// The skin of the registry, saved by the Skins tab (HKEY_CURRENT_USER\Software\THQ\Barnyard, the launcher's key)
static const TCHAR* s_szSettingsKey = "Software\\THQ\\Barnyard";
static const TCHAR* s_szSkinValue   = "Skin";

static void SaveSkin( const TCHAR* a_szSkin )
{
	// NOTE: the key isn't there before the launcher saved its settings
	HKEY  hKey;
	TBOOL bSaved = TFALSE;

	if ( ERROR_SUCCESS == RegCreateKeyExA( HKEY_CURRENT_USER, s_szSettingsKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL ) )
	{
		bSaved = ERROR_SUCCESS == RegSetValueExA( hKey, s_szSkinValue, 0, REG_SZ, (const BYTE*)a_szSkin, DWORD( TStringManager::String8Length( a_szSkin ) + 1 ) );
		RegCloseKey( hKey );
	}

	TINFO( "[skins] skin %s %s\n", a_szSkin, bSaved ? "saved" : "couldn't be saved" );
}

//-----------------------------------------------------------------------------
// The assets of the farm: the 14 hides (AAssetLoader only loads the player's),
// for the remote cow the models of the other gender, and the characters
//-----------------------------------------------------------------------------

static constexpr TINT MAX_GENDER_ASSETS = 8;
static constexpr TINT MAX_ASSET_NAME    = 48;

// The keyframe libraries and types of a gender, from the "playercharacter" property of lib_maingame
struct GenderAssets
{
	TCHAR aszKeylibs[ MAX_GENDER_ASSETS ][ MAX_ASSET_NAME ];
	TINT  iNumKeylibs;
	TCHAR aszTypes[ MAX_GENDER_ASSETS ][ MAX_ASSET_NAME ];
	TINT  iNumTypes;
	TBOOL abTypeLoadedHere[ MAX_GENDER_ASSETS ]; // created by ASkins, which destroys it
	TBOOL bTried;                                // ASkins loaded them (or tried to)
};

// The assets of a character, loaded when it's first shown and kept until the farm is unloaded
struct SkinAssets
{
	TBOOL bLoaded;
	TBOOL bTypeLoadedHere; // the farm didn't have the type, ASkins destroys it
};

static TBOOL        s_bFarmAssets         = TFALSE; // between OnMainGameAssetsLoaded and OnMainGameAssetsUnloaded
static TBOOL        s_bDisableBreakpoints = TFALSE; // "disablebreakpoints" of the property, as AAssetLoader loads the types
static GenderAssets s_aGenderAssets[ ASkins::NUM_GENDERS ];
static TBOOL        s_abHideLoaded[ ASkins::NUM_GENDERS ][ ASkins::NUM_BREEDS ];
static SkinAssets   s_aSkinAssets[ NUM_SKINS ];

static void GetHideLibraryName( TCHAR* a_szOut, TINT a_iSize, TINT a_iGender, TINT a_iBreed )
{
	TStringManager::String8Format( a_szOut, a_iSize, "p%c%s", ( a_iGender == APlayerProfile::GENDER_FEMALE ) ? 'f' : 'm', s_aCostumeNames[ a_iBreed ] );
}

// The type of the "player" unit's model a_iIndex (0: male, 1: female on the farm), TNULL if it isn't loaded
static AAnimatableObjectType* FindPlayerModelType( TINT a_iIndex )
{
	if ( !AUnitManager::IsSingletonCreated() || !AAnimatableObjectManager::IsSingletonCreated() )
		return TNULL;

	AUnitType* pType = AUnitManager::GetSingleton()->FindUnit( "player" );

	if ( !pType )
		return TNULL;

	AUnitModelStaticData* pModelData = TSTATICCAST( AUnitModelStaticData, pType->FindStaticData( &TGetClass( AUnitModelStaticData ) ) );

	if ( !pModelData || a_iIndex < 0 || a_iIndex >= pModelData->GetNumModels() )
		return TNULL;

	return AAnimatableObjectManager::GetSingleton()->FindType( pModelData->GetModelName( a_iIndex ) );
}

// Copies the names of the keyframe libraries and the types of a gender of the "playercharacter" property
static void ReadGenderAssets( GenderAssets& a_rAssets, const PBProperties* a_pGender )
{
	TUtil::MemClear( &a_rAssets, sizeof( a_rAssets ) );

	if ( !a_pGender )
		return;

	if ( const PBPropertyValue* pKeylibs = a_pGender->GetProperty( "keylib" ) )
	{
		const PBPropertyValueArray* pArray = pKeylibs->GetArray();

		for ( TUINT i = 0; i < pArray->GetSize() && a_rAssets.iNumKeylibs < MAX_GENDER_ASSETS; i++ )
			TStringManager::String8CopySafe( a_rAssets.aszKeylibs[ a_rAssets.iNumKeylibs++ ], pArray->GetValue( i )->GetString(), MAX_ASSET_NAME );
	}

	if ( const PBPropertyValue* pTypes = a_pGender->GetProperty( "AnimObjTypes" ) )
	{
		const PBPropertyValueArray* pArray = pTypes->GetArray();

		for ( TUINT i = 0; i < pArray->GetSize() && a_rAssets.iNumTypes < MAX_GENDER_ASSETS; i++ )
			TStringManager::String8CopySafe( a_rAssets.aszTypes[ a_rAssets.iNumTypes++ ], pArray->GetValue( i )->GetString(), MAX_ASSET_NAME );
	}
}

// Loads the models of a_iGender for the remote cow (what AAssetLoader::LoadPlayerCharacter loads for the player).
// TTRUE if the "player" unit can be spawned with that gender's model
static TBOOL LoadGenderModels( TINT a_iGender )
{
	if ( !IsGender( a_iGender ) )
		return TFALSE;

	if ( FindPlayerModelType( a_iGender ) )
		return TTRUE;

	GenderAssets& rAssets = s_aGenderAssets[ a_iGender ];

	// Only on the farm, and only once: what isn't there after that won't be
	if ( !s_bFarmAssets || rAssets.bTried )
		return TFALSE;

	rAssets.bTried = TTRUE;

	TINFO( "[skins] loading the %s cow for the other player (%d keyframe libraries, %d types)\n", s_aGenderNames[ a_iGender ], rAssets.iNumKeylibs, rAssets.iNumTypes );

	TTRB* pTRB = AAssetLoader::GetAssetTRB( AAssetType_AssetPack );

	// The animations first, the types use them
	for ( TINT i = 0; i < rAssets.iNumKeylibs; i++ )
		AKeyFrameLibraryManager::GetSingleton()->LoadLibrary( TPString8( rAssets.aszKeylibs[ i ] ), pTRB );

	AAnimatableObjectManager* pAnimObjManager = AAnimatableObjectManager::GetSingleton();

	for ( TINT i = 0; i < rAssets.iNumTypes; i++ )
	{
		// Types aren't counted: one that is there already (the bike of the other gender?) stays the loader's
		if ( pAnimObjManager->FindType( TPString8( rAssets.aszTypes[ i ] ) ) )
			continue;

		pAnimObjManager->LoadAnimObjType( TPString8( rAssets.aszTypes[ i ] ), TNULL, s_bDisableBreakpoints );
		rAssets.abTypeLoadedHere[ i ] = TTRUE;
	}

	const TBOOL bLoaded = FindPlayerModelType( a_iGender ) != TNULL;

	if ( !bLoaded )
		TERROR( "[skins] the %s cow couldn't be loaded, the other player's cow keeps your gender\n", s_aGenderNames[ a_iGender ] );

	return bLoaded;
}

static void UnloadGenderModels( TINT a_iGender )
{
	GenderAssets& rAssets = s_aGenderAssets[ a_iGender ];

	if ( !rAssets.bTried )
		return;

	AAnimatableObjectManager* pAnimObjManager = AAnimatableObjectManager::GetSingleton();

	for ( TINT i = 0; i < rAssets.iNumTypes; i++ )
	{
		if ( rAssets.abTypeLoadedHere[ i ] )
			pAnimObjManager->DestroyType( TPString8( rAssets.aszTypes[ i ] ) );

		rAssets.abTypeLoadedHere[ i ] = TFALSE;
	}

	for ( TINT i = 0; i < rAssets.iNumKeylibs; i++ )
		AKeyFrameLibraryManager::GetSingleton()->UnrefLibrary( TPString8( rAssets.aszKeylibs[ i ] ) );

	rAssets.bTried = TFALSE;
	TINFO( "[skins] the %s cow of the other player unloaded\n", s_aGenderNames[ a_iGender ] );
}

// The model type of a skin, its assets are loaded if it's one of s_aSkins (only on the farm, they're unloaded with
// it). Another name is a model type that has to be loaded already. TNULL for the cow or if there's no such type
static AAnimatableObjectType* FindSkinType( const TCHAR* a_szSkin )
{
	if ( IsCowSkin( a_szSkin ) || !AAnimatableObjectManager::IsSingletonCreated() )
		return TNULL;

	AAnimatableObjectManager* pAnimObjManager = AAnimatableObjectManager::GetSingleton();
	const TINT                iSkin           = FindSkin( a_szSkin );

	if ( iSkin < 0 )
		return pAnimObjManager->FindType( TPString8( a_szSkin ) );

	const SkinInfo& rSkin   = s_aSkins[ iSkin ];
	SkinAssets&     rAssets = s_aSkinAssets[ iSkin ];

	if ( !rAssets.bLoaded && s_bFarmAssets )
	{
		rAssets.bLoaded = TTRUE;

		TTRB* pTRB = AAssetLoader::GetAssetTRB( AAssetType_AssetPack );

		// The keyframes first: the skeleton of the model finds its library by name when the model is loaded
		AKeyFrameLibraryManager::GetSingleton()->LoadLibrary( TPString8( rSkin.szKeylib ), pTRB );
		AMaterialLibraryManager::GetSingleton()->LoadLibrary( TPString8( rSkin.szMatlib ), pTRB, TFALSE );

		// The type of an NPC of the farm may be there already
		if ( !pAnimObjManager->FindType( TPString8( rSkin.szType ) ) )
		{
			// Without breakpoints: a puppet doesn't play them
			pAnimObjManager->LoadAnimObjType( TPString8( rSkin.szType ), TNULL, TTRUE );
			rAssets.bTypeLoadedHere = TTRUE;
		}

		TINFO( "[skins] %s loaded (type %s%s, keyframes %s, textures %s)\n", rSkin.szLabel, rSkin.szType, rAssets.bTypeLoadedHere ? "" : " of the farm", rSkin.szKeylib, rSkin.szMatlib );
	}

	return pAnimObjManager->FindType( TPString8( rSkin.szType ) );
}

static void UnloadSkinAssets()
{
	for ( TINT i = 0; i < NUM_SKINS; i++ )
	{
		SkinAssets& rAssets = s_aSkinAssets[ i ];

		if ( !rAssets.bLoaded )
			continue;

		const SkinInfo& rSkin = s_aSkins[ i ];

		if ( rAssets.bTypeLoadedHere )
			AAnimatableObjectManager::GetSingleton()->DestroyType( TPString8( rSkin.szType ) );

		AKeyFrameLibraryManager::GetSingleton()->UnrefLibrary( TPString8( rSkin.szKeylib ) );
		AMaterialLibraryManager::GetSingleton()->UnrefLibrary( TPString8( rSkin.szMatlib ), TFALSE );

		rAssets.bLoaded         = TFALSE;
		rAssets.bTypeLoadedHere = TFALSE;
		TINFO( "[skins] %s unloaded\n", rSkin.szLabel );
	}
}

//-----------------------------------------------------------------------------
// How a cow is shown: the player's cow and the remote cow. The render callback
// of a cow's model is hooked while it has a hide of its own (the remote cow:
// its meshes use a copy of the shared skin material) or while it's hidden (a
// puppet of a character takes its place)
//-----------------------------------------------------------------------------

struct CowLook
{
	const TCHAR* szWho;   // for the log
	AUnit*       pUnit;   // the cow, TNULL: none (cleared before the unit is destroyed)
	TINT         iGender; // the remote cow: the gender of its model
	TINT         iBreed;  // the remote cow: the hide it shows, -1: the material of its model

	// The render callback of the cow's model while it's hooked
	AModelInstance*                  pModelInstance;
	TModelInstance::t_CustomRenderCB fnOrigRenderCb;
	void*                            pOrigUserData;
	ASkinMaterial*                   pOriginalMaterial;
	ASkinMaterial*                   pSwapMaterial;
	TBOOL                            bHidden;

	// The puppet
	AAnimatableObject* pPuppet;
	TCHAR              szPuppetAnim[ 64 ];                    // the animation it was told to play (a TPString8 would outlive the string pool)
	TBOOL              bPuppetPlaced;                         // it took the cow's place (the cow is hidden from then on)
	TCHAR              szPuppetSkin[ ASkins::MAX_SKIN_NAME ]; // the skin it shows
	TCHAR              szFailedSkin[ ASkins::MAX_SKIN_NAME ]; // a skin that couldn't be shown on this cow (not tried again)
};

static CowLook s_oLocalCow;
static CowLook s_oRemoteCow;

static void HideRenderCallback( TModelInstance* a_pInstance, void* a_pUserData )
{
	CowLook* pLook = TSTATICCAST( CowLook, a_pUserData );

	if ( pLook->bHidden )
	{
		// A puppet stands in its place. Nothing is drawn, the skeleton is posed as for drawing (the bones keep
		// working for what is attached to them)
		a_pInstance->GetSkeletonInstance()->UpdateState( TTRUE );
		return;
	}

	TModelLOD& rLOD = a_pInstance->GetModel()->GetLOD( a_pInstance->GetLOD() );

	if ( pLook->pSwapMaterial )
		MudAvatar::ReplaceMaterial( rLOD.ppMeshes, rLOD.iNumMeshes, pLook->pOriginalMaterial, pLook->pSwapMaterial );

	// The meshes are queued on their material now, the swap one is drawn with its own textures
	if ( pLook->fnOrigRenderCb )
		pLook->fnOrigRenderCb( a_pInstance, pLook->pOrigUserData );
	else
		AModelInstance::RenderInstanceCallback( a_pInstance, pLook->pModelInstance );

	if ( pLook->pSwapMaterial )
		MudAvatar::ReplaceMaterial( rLOD.ppMeshes, rLOD.iNumMeshes, pLook->pSwapMaterial, pLook->pOriginalMaterial );
}

static void HookCow( CowLook& a_rLook )
{
	if ( a_rLook.pModelInstance || !a_rLook.pUnit )
		return;

	AModelInstance* pModelInstance = TSTATICCAST( AAnimatedUnit, a_rLook.pUnit )->GetModelInstance();
	TModelInstance* pInstance      = pModelInstance->GetSceneObject()->GetInstance();

	a_rLook.pModelInstance = pModelInstance;
	a_rLook.fnOrigRenderCb = pInstance->GetCustomRenderMethod();
	a_rLook.pOrigUserData  = pInstance->GetCustomRenderUserData();

	pInstance->SetCustomRenderMethod( HideRenderCallback, &a_rLook );
}

// The cow's model gets its render callback back once nothing needs the hook. Only while the cow exists
static void UnhookCowIfUnused( CowLook& a_rLook )
{
	if ( !a_rLook.pModelInstance || a_rLook.bHidden || a_rLook.pSwapMaterial )
		return;

	TModelInstance* pInstance = a_rLook.pModelInstance->GetSceneObject()->GetInstance();

	if ( pInstance->GetCustomRenderMethod() == HideRenderCallback )
		pInstance->SetCustomRenderMethod( a_rLook.fnOrigRenderCb, a_rLook.pOrigUserData );

	a_rLook.pModelInstance = TNULL;
}

// The cow is drawn with the material of its model again
static void DropHide( CowLook& a_rLook )
{
	if ( a_rLook.pSwapMaterial )
	{
		a_rLook.pSwapMaterial->Destroy();
		a_rLook.pSwapMaterial     = TNULL;
		a_rLook.pOriginalMaterial = TNULL;
		UnhookCowIfUnused( a_rLook );
	}

	a_rLook.iBreed = -1;
}

// The puppet goes, the cow is shown again
static void DropPuppet( CowLook& a_rLook )
{
	if ( a_rLook.pPuppet )
	{
		// Not one of AAnimatableObjectManager's objects: deleted here, with its model instance
		delete a_rLook.pPuppet;
		a_rLook.pPuppet = TNULL;
		TINFO( "[skins] %s cow isn't %s any more\n", a_rLook.szWho, a_rLook.szPuppetSkin );
	}

	a_rLook.szPuppetAnim[ 0 ] = '\0';
	a_rLook.bPuppetPlaced     = TFALSE;
	a_rLook.szPuppetSkin[ 0 ] = '\0';

	if ( a_rLook.bHidden )
	{
		a_rLook.bHidden = TFALSE;
		UnhookCowIfUnused( a_rLook );
	}
}

// Everything of the look of a cow that still exists goes (the cow is drawn as itself)
static void ReleaseCow( CowLook& a_rLook )
{
	DropPuppet( a_rLook );
	DropHide( a_rLook );

	a_rLook.pUnit             = TNULL;
	a_rLook.iGender           = -1;
	a_rLook.szFailedSkin[ 0 ] = '\0';
}

// The look of a cow that's gone: only what's ours is destroyed, its model isn't touched
static void ForgetCow( CowLook& a_rLook )
{
	if ( a_rLook.pPuppet )
	{
		delete a_rLook.pPuppet;
		a_rLook.pPuppet = TNULL;
	}

	if ( a_rLook.pSwapMaterial )
		a_rLook.pSwapMaterial->Destroy();

	a_rLook.pSwapMaterial     = TNULL;
	a_rLook.pOriginalMaterial = TNULL;
	a_rLook.pModelInstance    = TNULL;
	a_rLook.bHidden           = TFALSE;
	a_rLook.bPuppetPlaced     = TFALSE;
	a_rLook.szPuppetAnim[ 0 ] = '\0';
	a_rLook.szPuppetSkin[ 0 ] = '\0';
	a_rLook.szFailedSkin[ 0 ] = '\0';
	a_rLook.pUnit             = TNULL;
	a_rLook.iGender           = -1;
	a_rLook.iBreed            = -1;
}

// Puts the puppet of a_szSkin on the cow of a_rLook. It isn't one of AAnimatableObjectManager's objects: it has no
// anim controllers, its animations are played and updated by ASkins and their breakpoints don't run (the hidden cow
// runs its own). TFALSE if the skin has no model
static TBOOL CreatePuppet( CowLook& a_rLook, const TCHAR* a_szSkin )
{
	AAnimatableObjectType* pType = FindSkinType( a_szSkin );

	if ( !pType )
	{
		TStringManager::String8Copy( a_rLook.szFailedSkin, a_szSkin );
		TWARN( "[skins] %s cow can't be %s: no such model type is loaded\n", a_rLook.szWho, a_szSkin );
		return TFALSE;
	}

	AAnimatableObject* pPuppet = new AAnimatableObject();

	if ( !pPuppet->Create( pType, TNULL, AAnimatableObject::CREATE_FLAGS_NO_ANIM_CONTROLLER ) )
	{
		delete pPuppet;
		TStringManager::String8Copy( a_rLook.szFailedSkin, a_szSkin );
		TERROR( "[skins] %s cow can't be %s: its model couldn't be created\n", a_rLook.szWho, a_szSkin );
		return TFALSE;
	}

	// A bunch (flowers, grass...) makes more objects of its type, AAnimatableObjectManager's, attached to the first
	T2SList<AAnimatableObject>& rAttached = pPuppet->GetAttachedObjects();

	if ( !rAttached.IsEmpty() )
	{
		for ( TINT i = 0; i < 64 && !rAttached.IsEmpty(); i++ )
			AAnimatableObjectManager::GetSingleton()->DestroyAnimatableObject( rAttached.Head() );

		delete pPuppet;
		TStringManager::String8Copy( a_rLook.szFailedSkin, a_szSkin );
		TWARN( "[skins] %s cow can't be %s: it's a bunch of objects, not a character\n", a_rLook.szWho, a_szSkin );
		return TFALSE;
	}

	// Drawn like a unit's model (AAnimatedUnit::CreateAnimatableObject), lit where it stands. Not shown before it's
	// in the cow's place (UpdatePuppet)
	AModelInstance* pModelInstance = pPuppet->GetModelInstance().Get();
	TSceneObject*   pSceneObject   = pModelInstance->GetSceneObject();

	pSceneObject->DisableOpacity();
	pSceneObject->GetFadeDistances().x = 50.0f;
	pSceneObject->GetFadeDistances().y = 50.1f;
	pSceneObject->GetFadeParams().x    = 10.000153f;
	pModelInstance->SetShadowMode( AModelInstance::SHADOWMODE_DYNAMIC );
	pPuppet->SetSkeletonUpdating( TFALSE, TFALSE );

	a_rLook.pPuppet           = pPuppet;
	a_rLook.bPuppetPlaced     = TFALSE;
	a_rLook.szPuppetAnim[ 0 ] = '\0';
	TStringManager::String8Copy( a_rLook.szPuppetSkin, a_szSkin );

	TINFO( "[skins] %s cow is shown as %s (model type %s)\n", a_rLook.szWho, a_szSkin, pType->GetName().GetString() );
	return TTRUE;
}

// The animation of the puppet for the cow's a_rcCowAnim: the same one if it has it, else sprint, run, walk or idle
// as the name or the speed of the cow says
static TPString8 PickPuppetAnimation( AAnimatableObject* a_pPuppet, const TPString8& a_rcCowAnim, TFLOAT a_fSpeed )
{
	ANamedAnimationSetRef pSet = a_pPuppet->GetToshiAnimInterface().GetAnimationSet();

	if ( !pSet.Get() )
		return TPString8();

	if ( !a_rcCowAnim.IsEmpty() && pSet->GetNamedAnimation( a_rcCowAnim ) )
		return a_rcCowAnim;

	const TPString8* apMoves[] = { &TPS8( sprint_lp ), &TPS8( run_lp ), &TPS8( walk_lp ), &TPS8( idle_lp ) };
	const TCHAR*     szName    = a_rcCowAnim.IsEmpty() ? "" : a_rcCowAnim.GetString();

	TINT iFirst;

	if ( strstr( szName, "sprint" ) )
		iFirst = 0;
	else if ( strstr( szName, "run" ) && !strstr( szName, "jump" ) )
		iFirst = 1;
	else if ( strstr( szName, "walk" ) )
		iFirst = 2;
	else
		iFirst = ( a_fSpeed > 7.0f ) ? 0 : ( a_fSpeed > 3.5f ) ? 1 : ( a_fSpeed > 0.3f ) ? 2 : 3;

	for ( TINT i = iFirst; i < TARRAYSIZE( apMoves ); i++ )
	{
		if ( pSet->GetNamedAnimation( *apMoves[ i ] ) )
			return *apMoves[ i ];
	}

	return TPString8();
}

// Before the models are updated and drawn (ARenderer's animation update): the puppet takes the place, the visibility
// and the animation of the cow as they are this frame
static void UpdatePuppet( CowLook& a_rLook, TFLOAT a_fDeltaTime )
{
	if ( !a_rLook.pPuppet || !a_rLook.pUnit )
		return;

	AAnimatableObject* pCow            = TSTATICCAST( AAnimatedUnit, a_rLook.pUnit )->GetAnimatableObject();
	AModelInstance*    pCowInstance    = pCow->GetModelInstance().Get();
	AModelInstance*    pPuppetInstance = a_rLook.pPuppet->GetModelInstance().Get();

	// Shown when the cow would be
	const TBOOL bCowShown = pCowInstance->IsUpdatingSkeleton();

	if ( bCowShown != pPuppetInstance->IsUpdatingSkeleton() )
		a_rLook.pPuppet->SetSkeletonUpdating( bCowShown, TFALSE );

	if ( !bCowShown )
		return;

	TMatrix44 mTransform;
	pCowInstance->GetLocalTransform( mTransform );
	pPuppetInstance->SetTransform( mTransform );

	// The cow's animation: the one it's going to if it's changing
	AToshiAnimationInterface& rCowAnims    = pCow->GetToshiAnimInterface();
	AToshiAnimationInterface& rPuppetAnims = a_rLook.pPuppet->GetToshiAnimInterface();
	AToshiAnimationRef*       pCowAnim     = rCowAnims.GetAnim();
	TPString8                 strCowAnim;

	if ( pCowAnim )
	{
		strCowAnim = rCowAnims.GetTransitionName( pCowAnim );

		if ( strCowAnim.IsEmpty() && pCowAnim->GetNamedAnimation() )
			strCowAnim = pCowAnim->GetNamedAnimation()->GetName();
	}

	ASteer*         pSteer  = a_rLook.pUnit->GetSteer();
	const TFLOAT    fSpeed  = pSteer ? TMath::Max( pSteer->GetMeasuredSpeed(), pSteer->GetSpeed() ) : 0.0f;
	const TPString8 strAnim = PickPuppetAnimation( a_rLook.pPuppet, strCowAnim, fSpeed );

	// Again when it ended (an animation that doesn't loop)
	if ( !rPuppetAnims.GetAnim() )
		a_rLook.szPuppetAnim[ 0 ] = '\0';

	if ( !strAnim.IsEmpty() && TStringManager::String8Compare( strAnim.GetString(), a_rLook.szPuppetAnim ) != 0 )
	{
		rPuppetAnims.PlayAnim( strAnim, -1.0f, TFALSE );
		TStringManager::String8CopySafe( a_rLook.szPuppetAnim, strAnim.GetString(), sizeof( a_rLook.szPuppetAnim ) );

#ifndef TOSHI_FINAL
		TINFO( "[skins] %s cow as %s plays %s (the cow: %s, speed %.1f)\n", a_rLook.szWho, a_rLook.szPuppetSkin, strAnim.GetString(), strCowAnim.IsEmpty() ? "-" : strCowAnim.GetString(), fSpeed );
#endif
	}

	// The same speed as the cow's animation when it's the same one
	if ( pCowAnim && strAnim == strCowAnim )
	{
		if ( AToshiAnimationRef* pPuppetAnim = rPuppetAnims.FindAnimationRef( strAnim ) )
			rPuppetAnims.SetAnimSpeed( pPuppetAnim, rCowAnims.GetAnimSpeed( pCowAnim ) );
	}

	// The breakpoints of the puppet's animations aren't run
	AToshiAnimationInterface::AnimEventList oEvents;
	rPuppetAnims.Update( a_fDeltaTime, oEvents );

	if ( !a_rLook.bPuppetPlaced )
	{
		// In its place from now on: the cow isn't drawn any more
		a_rLook.bPuppetPlaced = TTRUE;
		a_rLook.bHidden       = TTRUE;
		HookCow( a_rLook );
	}
}

static TBOOL __stdcall OnAnimationUpdateStart( CowLook* a_pCaller, ARenderer* a_pOwner, TBOOL** a_pData )
{
	const TFLOAT fDeltaTime = g_oSystemManager.GetScheduler()->GetCurrentDeltaTime();

	UpdatePuppet( s_oLocalCow, fDeltaTime );
	UpdatePuppet( s_oRemoteCow, fDeltaTime );
	return TTRUE;
}

// The unit is still whole here (its OnDestroy comes next), so its model can be given back its render callback
static void OnUnitDestroyed( void* a_pReceiver, const AUnitDestroyEvent& a_rcEvent )
{
	if ( !a_rcEvent.pUnit )
		return;

	if ( a_rcEvent.pUnit == s_oLocalCow.pUnit )
		ReleaseCow( s_oLocalCow );

	if ( a_rcEvent.pUnit == s_oRemoteCow.pUnit )
		ReleaseCow( s_oRemoteCow );
}

//-----------------------------------------------------------------------------
// The other player's cow
//-----------------------------------------------------------------------------

// CHANNEL_APPEARANCE: the look of the sender's cow, sent when it changes and every second (a datagram can be lost)
static constexpr TUINT32 APPEARANCE_MAGIC  = 0x31535942; // "BYS1"
static constexpr TFLOAT  APPEARANCE_RESEND = 1.0f;

#pragma pack( push, 1 )
struct AppearancePacket
{
	TUINT32 uiMagic;
	TUINT32 uiNonce;                         // made up by each run of the game, which counts its changes from 0
	TUINT16 uiChange;                        // counts the changes of the look (not the resends): an older datagram is dropped
	TUINT8  uiGender;                        // APlayerProfile::GENDER_MALE or GENDER_FEMALE
	TUINT8  uiBreed;                         // the costume, 0 - NUM_BREEDS - 1
	TCHAR   szSkin[ ASkins::MAX_SKIN_NAME ]; // player.skin, terminated
};
#pragma pack( pop )

TSTATICASSERT( sizeof( AppearancePacket ) == 36 );

// What was sent last
static TUINT32 s_uiNonce     = 0;
static TUINT16 s_uiChange    = 0;
static TINT    s_iSentGender = -1;
static TINT    s_iSentBreed  = -1;
static TCHAR   s_szSentSkin[ ASkins::MAX_SKIN_NAME ];
static TFLOAT  s_fResendWait = 0.0f;
static TBOOL   s_bSentToPeer = TFALSE; // the last one reached a peer (SendData)

// The look of the other player's cow, from their last datagram
struct RemoteLook
{
	TBOOL   bKnown;
	TUINT32 uiNonce;
	TUINT16 uiChange;
	TINT    iGender;
	TINT    iBreed;
	TCHAR   szSkin[ ASkins::MAX_SKIN_NAME ];
};

static RemoteLook s_oRemoteLook;

TBOOL ASkins::GetRemoteCowAppearance( TINT& a_rGender, TINT& a_rBreed )
{
	if ( !s_oRemoteLook.bKnown || !ACoopNet::IsConnected() ) return TFALSE;
	a_rGender = s_oRemoteLook.iGender;
	a_rBreed = s_oRemoteLook.iBreed;
	return TTRUE;
}

// The gender of the model GetRemoteModelIndex gave last
static TINT s_iSpawnGender = APlayerProfile::GENDER_MALE;

// The skin material of a_pModel called a_szName (shared by every instance of the model)
static ASkinMaterial* FindSkinMaterial( TModel* a_pModel, const TCHAR* a_szName )
{
	for ( TINT iLOD = 0; iLOD < a_pModel->GetNumLODs(); iLOD++ )
	{
		TModelLOD& rLOD = a_pModel->GetLOD( iLOD );

		for ( TINT i = 0; i < rLOD.iNumMeshes; i++ )
		{
			TMaterial* pMaterial = rLOD.ppMeshes[ i ]->GetMaterial();

			if ( pMaterial && MudAvatar::BodyMesh( a_szName, pMaterial->GetName(),
			         pMaterial->GetClass()->IsA( &TGetClass( ASkinMaterial ) ), pMaterial->GetTextureNum() != 0 ) )
				return TSTATICCAST( ASkinMaterial, pMaterial );
		}
	}

	return TNULL;
}

// Only the meshes queued for this instance see the clone. Other units can share the original.
static TBOOL SetInstanceHide( CowLook& rCow, AAnimatableObject* pObject, TINT a_iBreed, TTexture* pTexture )
{
	AAnimatableObjectType* pType = pObject->GetObjectType();
	const TCHAR* szMaterial = pType->GetMaterialName();
	if ( !szMaterial || !pTexture ) return TFALSE;
	if ( !rCow.pSwapMaterial )
	{
		AModelInstance* pModelInstance = pObject->GetModelInstance().Get();
		TModelInstance* pInstance      = pModelInstance->GetSceneObject()->GetInstance();
		ASkinMaterial*  pOriginal      = FindSkinMaterial( pInstance->GetModel(), szMaterial );

		if ( !pOriginal )
		{
			TERROR( "[skins] cow model has no textured skin material %s\n", szMaterial );
			return TFALSE;
		}

		// Drawn like the original: the same blending and flags, another hide and its lighting
		ASkinMaterial* pSwap = ASkinShader::GetSingleton()->CreateMaterial( TNULL );
		pSwap->Create( pOriginal->GetBlendMode() );
		pSwap->SetName( "skins_remote_hide" );
		pSwap->SetFlags( pOriginal->GetFlags() & ( TMaterial::FLAGS_NO_CULL | TMaterial::FLAGS_GLOW ), TTRUE );

		rCow.pOriginalMaterial = pOriginal;
		rCow.pSwapMaterial     = pSwap;
		HookCow( rCow );
	}

	rCow.pSwapMaterial->SetTexture( pTexture );

	// The lighting textures of the breed, as ASkinLightingManager::ApplySkinLight sets them
	const TCHAR*                     szSkinLight = pType->GetSkinLighting( a_iBreed );
	ASkinLightingManager::SkinLight* pLight      = TNULL;

	if ( szSkinLight && ASkinLightingManager::IsSingletonCreated() )
		pLight = ASkinLightingManager::GetSingleton()->FindSkinLight( szSkinLight );

	if ( pLight )
	{
		rCow.pSwapMaterial->SetLightingTexture( ASkinMaterial::LT_0, pLight->pTex2 );
		rCow.pSwapMaterial->SetLightingTexture( ASkinMaterial::LT_1, pLight->pTex1 );
		rCow.pSwapMaterial->SetLightingTexture( ASkinMaterial::LT_2, pLight->pTex4 );
		rCow.pSwapMaterial->SetLightingTexture( ASkinMaterial::LT_3, pLight->pTex3 );
		rCow.pSwapMaterial->SetTextureNum( 5 );
	}

	return TTRUE;
}

static void ShowHide( TINT a_iBreed )
{
	CowLook& rCow = s_oRemoteCow;
	if ( !rCow.pUnit || !IsBreed( a_iBreed ) ) return;
	rCow.iBreed = a_iBreed;
	AAnimatableObject* pObject = TSTATICCAST( AAnimatedUnit, rCow.pUnit )->GetAnimatableObject();
	AAnimatableObjectType* pType = pObject ? pObject->GetObjectType() : TNULL;
	const TCHAR* szTexture = pType ? pType->GetSkinTexture( a_iBreed ) : TNULL;
	TTexture* pTexture = szTexture ? AMaterialLibraryManager::GetSingleton()->FindTexture( szTexture ) : TNULL;
	if ( !pTexture || !pType->GetMaterialName() )
	{
		TERROR( "[skins] the other player's cow can't show the %s hide (%s isn't loaded)\n", s_aBreedNames[ a_iBreed ], szTexture ? szTexture : "no texture" );
		return;
	}
	const TBOOL bApplied = rCow.iGender != APlayerProfileManager::GetSingleton()->GetMainGender()
	    ? TSTATICCAST( AAnimatedUnit, rCow.pUnit )->ApplyMaterial( a_iBreed, TTRUE )
	    : SetInstanceHide( rCow, pObject, a_iBreed, pTexture );
	TINFO( "[skins] the other player's cow shows the %s %s hide (%s%s)\n", s_aGenderNames[ rCow.iGender ], s_aBreedNames[ a_iBreed ], szTexture, bApplied ? "" : ", not applied" );
}

static CowLook s_aMudCows[2];
static TBOOL s_abMudHideRefs[ASkins::NUM_GENDERS][ASkins::NUM_BREEDS];

TBOOL ASkins::ApplyMudHide( TINT slot, AUnit* unit, TINT gender, TINT breed )
{
	if ( slot < 0 || slot > 1 || !unit || !IsGender( gender ) || !IsBreed( breed ) ) return TFALSE;
	AAnimatableObject* object = TSTATICCAST( AAnimatedUnit, unit )->GetAnimatableObject();
	AAnimatableObjectType* type = object ? object->GetObjectType() : TNULL;
	const TCHAR* textureName = type ? type->GetSkinTexture( breed ) : TNULL;
	if ( !textureName ) return TFALSE;

	auto* materials = AMaterialLibraryManager::GetSingleton();
	TTexture* texture = materials->FindTexture( textureName );
	if ( !texture && !s_abMudHideRefs[gender][breed] )
	{
		TCHAR library[32];
		GetHideLibraryName( library, sizeof( library ), gender, breed );
		materials->LoadLibrary( TPString8( library ), AAssetLoader::GetAssetTRB( AAssetType_AssetPack ), TFALSE );
		s_abMudHideRefs[gender][breed] = TTRUE;
		texture = materials->FindTexture( textureName );
	}
	CowLook& cow = s_aMudCows[slot];
	cow.pUnit = unit;
	const TBOOL applied = SetInstanceHide( cow, object, breed, texture );
	TINFO( "[mud-avatar] slot=%d gender=%d breed=%d material=%s texture=%s applied=%d\n",
	       slot, gender, breed, type->GetMaterialName() ? type->GetMaterialName() : "-", textureName, applied );
	return applied;
}

void ASkins::DropMudHide( TINT slot )
{
	DropHide( s_aMudCows[slot] );
	s_aMudCows[slot].pUnit = TNULL;
}

void ASkins::ReleaseMudHides()
{
	for ( TINT gender = 0; gender < NUM_GENDERS; ++gender )
		for ( TINT breed = 0; breed < NUM_BREEDS; ++breed )
			if ( s_abMudHideRefs[gender][breed] )
			{
				TCHAR library[32];
				GetHideLibraryName( library, sizeof( library ), gender, breed );
				AMaterialLibraryManager::GetSingleton()->UnrefLibrary( TPString8( library ), TFALSE );
				s_abMudHideRefs[gender][breed] = TFALSE;
			}
}

// CHANNEL_APPEARANCE
static void OnAppearanceData( const void* a_pData, TINT a_iSize )
{
	if ( a_iSize != TINT( sizeof( AppearancePacket ) ) )
		return;

	AppearancePacket oPacket;
	TUtil::MemCopy( &oPacket, a_pData, sizeof( oPacket ) );

	if ( oPacket.uiMagic != APPEARANCE_MAGIC || !IsGender( oPacket.uiGender ) || !IsBreed( oPacket.uiBreed ) )
		return;

	RemoteLook& rLook = s_oRemoteLook;

	// An older datagram of the same run (the counter wraps around)
	if ( rLook.bKnown && oPacket.uiNonce == rLook.uiNonce && TINT16( oPacket.uiChange - rLook.uiChange ) < 0 )
		return;

	oPacket.szSkin[ ASkins::MAX_SKIN_NAME - 1 ] = '\0';

	TCHAR szSkin[ ASkins::MAX_SKIN_NAME ];
	CleanSkinName( szSkin, oPacket.szSkin );

	const TBOOL bChanged = !rLook.bKnown || rLook.iGender != oPacket.uiGender || rLook.iBreed != oPacket.uiBreed || TStringManager::String8Compare( rLook.szSkin, szSkin ) != 0;

	rLook.bKnown   = TTRUE;
	rLook.uiNonce  = oPacket.uiNonce;
	rLook.uiChange = oPacket.uiChange;
	rLook.iGender  = oPacket.uiGender;
	rLook.iBreed   = oPacket.uiBreed;
	TStringManager::String8Copy( rLook.szSkin, szSkin );

	if ( bChanged )
		TINFO( "[skins] the other player's cow: %s %s, skin %s%s\n", s_aGenderNames[ rLook.iGender ], s_aBreedNames[ rLook.iBreed ], rLook.szSkin, ( IsCowSkin( rLook.szSkin ) || FindSkin( rLook.szSkin ) >= 0 ) ? "" : " (not in the Skins tab: shown if a model type of that name is loaded here)" );
}

// The look of the player's cow goes to the other game when it changes and every second
static void SendLook( TFLOAT a_fDeltaTime )
{
	if ( !APlayerProfileManager::IsSingletonCreated() )
		return;

	APlayerProfileManager* pProfileManager = APlayerProfileManager::GetSingleton();
	const TINT             iGender         = pProfileManager->GetMainGender();
	const TINT             iBreed          = pProfileManager->GetMainCostume();

	// No game loaded yet
	if ( !IsGender( iGender ) || !IsBreed( iBreed ) )
		return;

	const TBOOL bChanged = iGender != s_iSentGender || iBreed != s_iSentBreed || TStringManager::String8Compare( s_szSentSkin, s_szSkin ) != 0;

	if ( bChanged )
	{
		s_uiChange++;
		s_iSentGender = iGender;
		s_iSentBreed  = iBreed;
		TStringManager::String8Copy( s_szSentSkin, s_szSkin );
		s_fResendWait = 0.0f;
	}

	s_fResendWait -= a_fDeltaTime;

	if ( s_fResendWait > 0.0f )
		return;

	s_fResendWait = APPEARANCE_RESEND;

	if ( s_uiNonce == 0 )
		s_uiNonce = ( GetTickCount() ^ ( GetCurrentProcessId() << 16 ) ) | 1;

	AppearancePacket oPacket = {};
	oPacket.uiMagic  = APPEARANCE_MAGIC;
	oPacket.uiNonce  = s_uiNonce;
	oPacket.uiChange = s_uiChange;
	oPacket.uiGender = TUINT8( iGender );
	oPacket.uiBreed  = TUINT8( iBreed );
	TStringManager::String8Copy( oPacket.szSkin, s_szSkin );

	const TBOOL bSent = ACoopNet::SendData( ACoopNet::CHANNEL_APPEARANCE, &oPacket, sizeof( oPacket ) );

	if ( bSent && ( bChanged || !s_bSentToPeer ) )
		TINFO( "[skins] your cow sent to the other player: %s %s, skin %s\n", s_aGenderNames[ iGender ], s_aBreedNames[ iBreed ], s_szSkin );

	s_bSentToPeer = bSent;
}

// The remote cow takes the look of the remote player: another model for another gender, its hide for a breed
static void UpdateRemoteCow()
{
	// A new player sends their own look
	if ( !ACoopNet::IsConnected() )
		s_oRemoteLook.bKnown = TFALSE;

	AUnit* pRemoteUnit = ACoopNet::GetRemotePlayerUnit();

	if ( !pRemoteUnit || pRemoteUnit != s_oRemoteCow.pUnit || !s_oRemoteLook.bKnown )
		return;

	if ( s_oRemoteLook.iGender != s_oRemoteCow.iGender && LoadGenderModels( s_oRemoteLook.iGender ) )
	{
		TINFO( "[skins] the other player's cow is %s now, it's spawned again\n", s_aGenderNames[ s_oRemoteLook.iGender ] );
		ACoopNet::RespawnRemotePlayerUnit();
		return;
	}

	if ( s_oRemoteCow.iBreed != s_oRemoteLook.iBreed )
		ShowHide( s_oRemoteLook.iBreed );
}

// ACoopNet::AddUpdateHandler: every frame of a session, after the datagrams were handled
static void OnCoopNetUpdate( TFLOAT a_fDeltaTime )
{
	SendLook( a_fDeltaTime );
	UpdateRemoteCow();
}

//-----------------------------------------------------------------------------
// Doing what was asked
//-----------------------------------------------------------------------------

// The player's cow if it stands on the farm, TNULL otherwise
static AAnimatedUnit* GetCowOnFarm()
{
	if ( !s_bFarmAssets || !APlayerManager::IsSingletonCreated() || !AUnitManager::IsSingletonCreated() || !APlayerProfileManager::IsSingletonCreated() )
		return TNULL;

	if ( !AGameLoader::IsMainGameLoaded() || AGameLoader::IsBarnExploreMiniGame() || AGameLoader::IsChangingTerrain() || AGameLoader::IsReturningToFrontEnd() )
		return TNULL;

	APlayerManager* pPlayerManager = APlayerManager::GetSingleton();
	APlayerSet*     pPlayerSet     = pPlayerManager->GetPlayerSet( APlayerManager::PLAYERSET_SIM );

	// The minigames switch to their own player set
	if ( pPlayerManager->GetCurrentPlayerSet() != pPlayerSet || pPlayerSet->GetHumanPlayerCount() < 1 )
		return TNULL;

	AAnimatedUnit* pCow = TSTATICCAST( AAnimatedUnit, pPlayerSet->GetPlayerUnit( 0 ) );

	if ( !pCow || !pCow->GetAnimatableObject() || !pCow->GetSteer() )
		return TNULL;

	// Still waiting for the terrain around the goal it's placed at: where it stands isn't known yet
	ASteer* pSteer = pCow->GetSteer();

	if ( pSteer->IsA( &TGetClass( APlayerSteer ) ) && TSTATICCAST( APlayerSteer, pSteer )->HasGoal() )
		return TNULL;

	return pCow;
}

static TBOOL IsOnBike()
{
	return ABikeManager::IsSingletonCreated() && ABikeManager::GetSingleton()->IsPlayerOnBike();
}

// A scripted sequence (a cutscene) plays over the sim
static TBOOL IsInCutscene()
{
	if ( !AGameStateController::IsSingletonCreated() )
		return TFALSE;

	// Never freed: a static one would be released after the string pool is gone
	static TPString8* s_pSSeqState = new TPString8( "ASSeqState" );
	return AGameStateController::GetSingleton()->IsStateOnStack( *s_pSSeqState );
}

// The farm can be loaded again: the player walks around in the sim, nothing else is going on
static TBOOL CanReloadFarm()
{
	if ( ARootTask::GetSingleton()->IsTransitioning() || AGameStateController::IsBusy() || IsOnBike() )
		return TFALSE;

	AGameStateController* pController = AGameStateController::GetSingleton();

	return pController->GetNumStates() > 0 && pController->GetCurrentState()->IsExactly( &TGetClass( ASimState ) );
}

static void PutBreedOn( AAnimatedUnit* a_pCow, TINT a_iBreed )
{
	APlayerProfileManager* pProfileManager = APlayerProfileManager::GetSingleton();
	const TINT             iGender         = pProfileManager->GetMainGender();

	// The profile keeps it: the save, the portraits and the minigames follow
	pProfileManager->GetMainProfile()->SetCostume( a_iBreed );

	// The material of the model is shared, the second player of the same screen gets it too
	const TBOOL bApplied = a_pCow->ApplyMaterial( a_iBreed, TTRUE );

	AAnimatableObjectType* pType       = a_pCow->GetAnimatableObject()->GetObjectType();
	const TCHAR*           szTexture   = pType->GetSkinTexture( a_iBreed );
	const TCHAR*           szSkinLight = pType->GetSkinLighting( a_iBreed );
	TINFO( "[skins] your cow is %s %s now (%s, light %s%s)\n", s_aGenderNames[ iGender ], s_aBreedNames[ a_iBreed ], szTexture ? szTexture : "no texture", szSkinLight ? szSkinLight : "none", bApplied ? "" : ", not applied" );
}

// Set from the start of a farm reload until the cow stands on the farm again (for the log)
static TBOOL s_bFarmReloading = TFALSE;

static void StartFarmReload( AAnimatedUnit* a_pCow, TINT a_iGender, TINT a_iBreed )
{
	APlayerProfileManager* pProfileManager = APlayerProfileManager::GetSingleton();
	const TINT             iOldGender      = pProfileManager->GetMainGender();
	const TINT             iOldBreed       = pProfileManager->GetMainCostume();
	const TVector4         vPosition       = *a_pCow->GetSteer()->GetPosition();

	// In the profile before the quick save keeps it: the farm is loaded with it, the quick save gives it back
	pProfileManager->SetMainGender( a_iGender );
	pProfileManager->GetMainProfile()->SetCostume( a_iBreed );

	if ( !AGameLoader::ReloadMainGame() )
	{
		pProfileManager->SetMainGender( iOldGender );
		pProfileManager->GetMainProfile()->SetCostume( iOldBreed );
		TWARN( "[skins] the farm couldn't be loaded again, the gender stays\n" );
		return;
	}

	s_bFarmReloading = TTRUE;
	TINFO( "[skins] your cow will be %s %s: loading the farm again (it stands at %.2f %.2f %.2f)\n", s_aGenderNames[ a_iGender ], s_aBreedNames[ a_iBreed ], vPosition.x, vPosition.y, vPosition.z );
}

// The player's cow shows the puppet of the chosen character on the farm, the cow itself on the bike, during
// cutscenes and elsewhere
static void UpdateLocalPuppet()
{
	AAnimatedUnit* pCow    = GetCowOnFarm();
	const TBOOL    bPuppet = pCow && !IsCowSkin( s_szSkin ) && !IsOnBike() && !IsInCutscene();
	AUnit*         pTarget = bPuppet ? pCow : TNULL;
	CowLook&       rLook   = s_oLocalCow;

	if ( rLook.pUnit == pTarget && ( !pTarget || TStringManager::String8Compare( rLook.szPuppetSkin, s_szSkin ) == 0 ) )
		return;

	// A skin that couldn't be shown on this cow isn't tried every frame
	if ( pTarget && rLook.pUnit == pTarget && TStringManager::String8Compare( rLook.szFailedSkin, s_szSkin ) == 0 )
		return;

	// The cow it was on still exists (a destroyed one was released already): it's shown as itself again
	if ( rLook.pUnit )
	{
		DropPuppet( rLook );

		if ( rLook.pUnit != pTarget )
			ReleaseCow( rLook );
	}

	if ( !pTarget )
		return;

	rLook.pUnit = pTarget;
	CreatePuppet( rLook, s_szSkin );
}

// The skin of player.skin. TFALSE if it's neither one of the Skins tab nor a loaded model type (it's taken anyway,
// such a type may be loaded later)
static TBOOL TakeSkin( const TCHAR* a_szSkin, TBOOL a_bSave )
{
	TCHAR szSkin[ ASkins::MAX_SKIN_NAME ];
	CleanSkinName( szSkin, a_szSkin );

	TBOOL bKnown = FindSkin( szSkin ) >= 0;

	if ( !bKnown && AAnimatableObjectManager::IsSingletonCreated() )
		bKnown = AAnimatableObjectManager::GetSingleton()->FindType( TPString8( szSkin ) ) != TNULL;

	if ( !bKnown )
		TWARN( "[skins] skin %s isn't in the Skins tab: it's shown if a model type of that name is loaded\n", szSkin );

	const TBOOL bChanged = TStringManager::String8Compare( szSkin, s_szSkin ) != 0;
	TStringManager::String8Copy( s_szSkin, szSkin );

#ifdef PRIMEDEBUG
	APrimeDebug::SetPlayerSkin( s_szSkin );
#endif

	if ( bChanged )
		TINFO( "[skins] skin %s\n", s_szSkin );

	if ( a_bSave )
		SaveSkin( s_szSkin );

	return bKnown;
}

#ifdef PRIMEDEBUG
// player.skin, typed in the console or on the command line (not saved, the Skins tab saves)
static void ReadSkinVariable()
{
	const TCHAR* szVariable = APrimeDebug::GetPlayerSkin();

	if ( !szVariable || TStringManager::String8Compare( szVariable, s_szSkin ) == 0 )
		return;

	TakeSkin( szVariable, TFALSE );
}
#endif

//-----------------------------------------------------------------------------
// ASkins
//-----------------------------------------------------------------------------

const TCHAR* ASkins::GetBreedName( TINT a_iBreed )
{
	return IsBreed( a_iBreed ) ? s_aBreedNames[ a_iBreed ] : "?";
}

const TCHAR* ASkins::GetGenderName( TINT a_iGender )
{
	return IsGender( a_iGender ) ? s_aGenderNames[ a_iGender ] : "?";
}

const TCHAR* ASkins::GetSavedSkin()
{
	static TCHAR s_szSaved[ MAX_SKIN_NAME ] = "cow";
	static TBOOL s_bRead                    = TFALSE;

	if ( s_bRead )
		return s_szSaved;

	s_bRead = TTRUE;

	HKEY  hKey;
	TBOOL bSaved = TFALSE;

	if ( ERROR_SUCCESS == RegOpenKeyExA( HKEY_CURRENT_USER, s_szSettingsKey, 0, KEY_READ, &hKey ) )
	{
		TCHAR szValue[ MAX_SKIN_NAME * 2 ] = {};
		DWORD dwType                       = REG_NONE;
		DWORD dwSize                       = sizeof( szValue ) - 1;

		if ( ERROR_SUCCESS == RegQueryValueExA( hKey, s_szSkinValue, NULL, &dwType, (LPBYTE)szValue, &dwSize ) && dwType == REG_SZ )
		{
			CleanSkinName( s_szSaved, szValue );
			bSaved = TTRUE;
		}

		RegCloseKey( hKey );
	}

	TINFO( "[skins] skin %s (%s)\n", s_szSaved, bSaved ? "the saved one" : "nothing saved" );
	return s_szSaved;
}

TINT ASkins::GetNumSkins()
{
	return NUM_SKINS;
}

const TCHAR* ASkins::GetSkinName( TINT a_iIndex )
{
	return ( a_iIndex >= 0 && a_iIndex < NUM_SKINS ) ? s_aSkins[ a_iIndex ].szName : COW_SKIN;
}

const TCHAR* ASkins::GetSkinLabel( TINT a_iIndex )
{
	return ( a_iIndex >= 0 && a_iIndex < NUM_SKINS ) ? s_aSkins[ a_iIndex ].szLabel : "?";
}

TBOOL ASkins::ChooseSkin( const TCHAR* a_szSkin )
{
	if ( !a_szSkin )
		return TFALSE;

	return TakeSkin( a_szSkin, TTRUE );
}

void ASkins::Update( TFLOAT a_fDeltaTime )
{
	static TBOOL s_bConnected = TFALSE;

	if ( !s_bConnected )
	{
		s_bConnected = TTRUE;
		ACoopNet::SetDataHandler( ACoopNet::CHANNEL_APPEARANCE, OnAppearanceData );
		ACoopNet::AddUpdateHandler( OnCoopNetUpdate );

		s_oLocalCow.szWho  = "your";
		s_oRemoteCow.szWho = "the other player's";
		s_oLocalCow.iGender = s_oLocalCow.iBreed = s_oRemoteCow.iGender = s_oRemoteCow.iBreed = -1;

		// Created once and never freed: they stay connected until the program exits, their destructors would unlink
		// them from lists that may be gone by then
		TGlobalListener<CowLook, AUnitDestroyEvent>* pDestroyListener = new TGlobalListener<CowLook, AUnitDestroyEvent>();
		pDestroyListener->Connect( &s_oRemoteCow, OnUnitDestroyed );

#ifndef PRIMEDEBUG
		// player.skin starts with the saved skin, here it's that one
		TStringManager::String8Copy( s_szSkin, GetSavedSkin() );
#endif
	}

	// The puppets follow the cows right before the models are updated and drawn
	static TBOOL s_bAnimationListener = TFALSE;

	if ( !s_bAnimationListener && ARenderer::IsSingletonCreated() )
	{
		s_bAnimationListener = TTRUE;

		TListener<ARenderer, TBOOL*, CowLook>* pListener = new TListener<ARenderer, TBOOL*, CowLook>();
		pListener->Connect( ARenderer::GetSingleton()->GetAnimationUpdateStartEmitter(), &s_oLocalCow, OnAnimationUpdateStart, 0 );
	}

#ifdef PRIMEDEBUG
	ReadSkinVariable();
#endif

	APlayerProfileManager* pProfileManager = APlayerProfileManager::IsSingletonCreated() ? APlayerProfileManager::GetSingleton() : TNULL;

	if ( AAnimatedUnit* pCow = GetCowOnFarm() )
	{
		const TINT iGender = pProfileManager->GetMainGender();
		const TINT iBreed  = pProfileManager->GetMainCostume();

		if ( s_bFarmReloading )
		{
			s_bFarmReloading = TFALSE;

			const TVector4& rcPosition = *pCow->GetSteer()->GetPosition();
			TINFO( "[skins] back on the farm as a %s %s cow at %.2f %.2f %.2f\n", GetGenderName( iGender ), GetBreedName( iBreed ), rcPosition.x, rcPosition.y, rcPosition.z );
		}

		// What it is already
		if ( s_iWantedGender == iGender )
			s_iWantedGender = -1;

		if ( s_iWantedGender == -1 && s_iWantedBreed == iBreed )
			s_iWantedBreed = -1;

		if ( s_iWantedGender != -1 )
		{
			// Another model: the farm is loaded again once nothing else goes on (the Mods window is closed too)
			if ( CanReloadFarm() )
			{
				const TINT iNewBreed = ( s_iWantedBreed != -1 ) ? s_iWantedBreed : iBreed;

				StartFarmReload( pCow, s_iWantedGender, IsBreed( iNewBreed ) ? iNewBreed : 0 );
				s_iWantedGender = -1;
				s_iWantedBreed  = -1;
			}
		}
		else if ( s_iWantedBreed != -1 && !IsOnBike() )
		{
			PutBreedOn( pCow, s_iWantedBreed );
			s_iWantedBreed = -1;
		}
	}

	UpdateLocalPuppet();

	// The console variables show the look (or what was asked for)
	TINT iShownGender = -1;
	TINT iShownBreed  = -1;

	if ( pProfileManager && IsGender( pProfileManager->GetMainGender() ) )
	{
		iShownGender = pProfileManager->GetMainGender();
		iShownBreed  = IsBreed( pProfileManager->GetMainCostume() ) ? pProfileManager->GetMainCostume() : 0;
	}

	ms_iGenderVar = ( s_iWantedGender != -1 ) ? s_iWantedGender : iShownGender;
	ms_iBreedVar  = ( s_iWantedBreed != -1 ) ? s_iWantedBreed : iShownBreed;
}

void ASkins::RequestGender( TINT a_iGender )
{
	if ( a_iGender == -1 )
	{
		if ( s_iWantedGender != -1 )
			TINFO( "[skins] gender change cancelled\n" );

		s_iWantedGender = -1;
		return;
	}

	if ( !IsGender( a_iGender ) )
		return;

	if ( s_iWantedGender != a_iGender )
		TINFO( "[skins] asked for a %s cow (done on the farm)\n", s_aGenderNames[ a_iGender ] );

	s_iWantedGender = a_iGender;
}

void ASkins::RequestBreed( TINT a_iBreed )
{
	if ( a_iBreed == -1 )
	{
		s_iWantedBreed = -1;
		return;
	}

	if ( !IsBreed( a_iBreed ) )
		return;

	if ( s_iWantedBreed != a_iBreed )
		TINFO( "[skins] asked for the %s hide (done on the farm)\n", s_aBreedNames[ a_iBreed ] );

	s_iWantedBreed = a_iBreed;
}

void ASkins::OnMainGameAssetsLoaded( const PBPropertyValue* a_pPlayerCharacter, TTRB* a_pTRB )
{
	s_bFarmAssets = TTRUE;

	// Every hide of both genders: a breed goes on a cow right away, the remote cow has its own
	TINT iNumHides = 0;

	for ( TINT iGender = 0; iGender < NUM_GENDERS; iGender++ )
	{
		for ( TINT iBreed = 0; iBreed < NUM_BREEDS; iBreed++ )
		{
			TCHAR szName[ 32 ];
			GetHideLibraryName( szName, sizeof( szName ), iGender, iBreed );

			AMaterialLibraryManager::GetSingleton()->LoadLibrary( TPString8( szName ), a_pTRB, TFALSE );
			s_abHideLoaded[ iGender ][ iBreed ] = TTRUE;
			iNumHides++;
		}
	}

	// The models of the genders, for the remote cow
	const PBProperties* pProperties = ( a_pPlayerCharacter && a_pPlayerCharacter->GetType() == PBPropertyValue::Type::Properties ) ? a_pPlayerCharacter->GetProperties() : TNULL;

	if ( pProperties )
	{
		const PBPropertyValue* pDisableBreakpoints = pProperties->GetProperty( "disablebreakpoints" );
		s_bDisableBreakpoints                      = pDisableBreakpoints && pDisableBreakpoints->GetRaw() != TNULL;

		ReadGenderAssets( s_aGenderAssets[ APlayerProfile::GENDER_MALE ], pProperties->FindProperties( "male" ) );
		ReadGenderAssets( s_aGenderAssets[ APlayerProfile::GENDER_FEMALE ], pProperties->FindProperties( "female" ) );
	}

	TINFO( "[skins] %d hides loaded with the farm\n", iNumHides );

	for ( TINT iGender = 0; iGender < NUM_GENDERS; iGender++ )
	{
		const GenderAssets& rAssets = s_aGenderAssets[ iGender ];
		TINFO( "[skins] %s cow: %d keyframe libraries (%s), %d types (%s)\n", s_aGenderNames[ iGender ], rAssets.iNumKeylibs, rAssets.iNumKeylibs > 0 ? rAssets.aszKeylibs[ 0 ] : "-", rAssets.iNumTypes, rAssets.iNumTypes > 0 ? rAssets.aszTypes[ 0 ] : "-" );
	}

	if ( APlayerProfileManager::IsSingletonCreated() )
	{
		APlayerProfileManager* pProfileManager = APlayerProfileManager::GetSingleton();
		TINFO( "[skins] the farm is loaded for a %s %s cow (the profile's), skin %s\n", GetGenderName( pProfileManager->GetMainGender() ), GetBreedName( pProfileManager->GetMainCostume() ), s_szSkin );

		// In a network session the other player can have the other gender: loaded behind the loading screen
		const TINT iGender = pProfileManager->GetMainGender();

		if ( ACoopNet::GetMode() != ACoopNet::MODE_OFF && IsGender( iGender ) )
			LoadGenderModels( 1 - iGender );
	}
}

void ASkins::OnMainGameAssetsUnloaded()
{
	if ( !s_bFarmAssets )
		return;

	// The units went with the level before the assets: their looks were released when they were destroyed. If one
	// wasn't, its model is gone, only what ASkins made goes now
	if ( s_oLocalCow.pUnit || s_oLocalCow.pPuppet || s_oRemoteCow.pUnit || s_oRemoteCow.pPuppet || s_oRemoteCow.pSwapMaterial )
		TWARN( "[skins] the look of a cow outlived it\n" );

	ForgetCow( s_oLocalCow );
	ForgetCow( s_oRemoteCow );

	UnloadSkinAssets();

	for ( TINT iGender = 0; iGender < NUM_GENDERS; iGender++ )
		UnloadGenderModels( iGender );

	for ( TINT iGender = 0; iGender < NUM_GENDERS; iGender++ )
	{
		for ( TINT iBreed = 0; iBreed < NUM_BREEDS; iBreed++ )
		{
			if ( !s_abHideLoaded[ iGender ][ iBreed ] )
				continue;

			TCHAR szName[ 32 ];
			GetHideLibraryName( szName, sizeof( szName ), iGender, iBreed );

			AMaterialLibraryManager::GetSingleton()->UnrefLibrary( TPString8( szName ), TFALSE );
			s_abHideLoaded[ iGender ][ iBreed ] = TFALSE;
		}
	}

	s_bFarmAssets = TFALSE;
	TINFO( "[skins] the hides of the farm unloaded\n" );
}

TINT ASkins::GetRemoteModelIndex()
{
	TINT iLocalGender = APlayerProfileManager::GetSingleton()->GetMainGender();

	if ( !IsGender( iLocalGender ) || !FindPlayerModelType( iLocalGender ) )
		iLocalGender = FindPlayerModelType( APlayerProfile::GENDER_FEMALE ) ? APlayerProfile::GENDER_FEMALE : APlayerProfile::GENDER_MALE;

	TINT iGender = iLocalGender;

	if ( s_oRemoteLook.bKnown && s_oRemoteLook.iGender != iLocalGender && LoadGenderModels( s_oRemoteLook.iGender ) )
		iGender = s_oRemoteLook.iGender;

	s_iSpawnGender = iGender;

	// The models of the "player" unit: 0 male, 1 female (2 and 3 are the ones of the barn)
	return iGender;
}

void ASkins::OnRemoteUnitSpawned( AUnit* a_pUnit )
{
	// The one before is only destroyed next frame, its model gets its render callback back now
	if ( s_oRemoteCow.pUnit && s_oRemoteCow.pUnit != a_pUnit )
		ReleaseCow( s_oRemoteCow );

	s_oRemoteCow.pUnit   = a_pUnit;
	s_oRemoteCow.iGender = s_iSpawnGender;
	s_oRemoteCow.iBreed  = -1;

	TINFO( "[skins] the other player's cow is spawned with the %s model\n", s_aGenderNames[ s_iSpawnGender ] );

	// Until the look of the other player is known it shows the hide of this game's player
	if ( s_oRemoteLook.bKnown )
		ShowHide( s_oRemoteLook.iBreed );
}

void ASkins::UpdateRemotePuppet( AUnit* a_pRemoteUnit, TFLOAT a_fDeltaTime )
{
	CowLook& rLook = s_oRemoteCow;

	// Only the cow OnRemoteUnitSpawned was told about
	if ( !a_pRemoteUnit || a_pRemoteUnit != rLook.pUnit )
		return;

	const TCHAR* szSkin = ( s_oRemoteLook.bKnown && !IsCowSkin( s_oRemoteLook.szSkin ) ) ? s_oRemoteLook.szSkin : TNULL;

	if ( !szSkin )
	{
		DropPuppet( rLook );
		return;
	}

	if ( rLook.pPuppet && TStringManager::String8Compare( rLook.szPuppetSkin, szSkin ) == 0 )
		return;

	if ( TStringManager::String8Compare( rLook.szFailedSkin, szSkin ) == 0 )
		return;

	DropPuppet( rLook );
	CreatePuppet( rLook, szSkin );
}

//-----------------------------------------------------------------------------
// The Skins tab of the Mods window
//-----------------------------------------------------------------------------

void ASkins::RenderImGui()
{
#ifdef ASKINS_IMGUI
	const TBOOL      bOnFarm   = GetCowOnFarm() != TNULL;
	constexpr TFLOAT CHOICES_X = 100.0f;

	ImGui::SeparatorText( "Your character" );

	// A setting: it can be chosen anywhere, it's shown on the farm
	const TINT   iSkin     = FindSkin( s_szSkin );
	const TCHAR* szPreview = ( iSkin >= 0 ) ? s_aSkins[ iSkin ].szLabel : s_szSkin;

	ImGui::TextUnformatted( "Character" );
	ImGui::SameLine( CHOICES_X );
	ImGui::SetNextItemWidth( 280.0f );

	if ( ImGui::BeginCombo( "##character", szPreview ) )
	{
		for ( TINT i = 0; i < NUM_SKINS; i++ )
		{
			TCHAR szItem[ 96 ];
			TStringManager::String8Format( szItem, sizeof( szItem ), "%s  (%s)", s_aSkins[ i ].szLabel, s_aSkins[ i ].szMoves );

			if ( ImGui::Selectable( szItem, i == iSkin ) )
				ChooseSkin( s_aSkins[ i ].szName );
		}

		ImGui::EndCombo();
	}

	if ( !IsCowSkin( s_szSkin ) )
		ImGui::TextWrapped( "On the farm your cow is shown as this character, it copies the cow's moves. The bike, the barn, the minigames and the cutscenes show the cow." );

	ImGui::SeparatorText( "Your cow" );

	if ( !bOnFarm )
		ImGui::TextWrapped( "Your cow can be changed on the farm. Load a game first." );

	ImGui::BeginDisabled( !bOnFarm );

	ImGui::TextUnformatted( "Gender" );
	ImGui::SameLine( CHOICES_X );
	ImGui::BeginGroup();

	for ( TINT i = 0; i < NUM_GENDERS; i++ )
	{
		if ( i > 0 )
			ImGui::SameLine();

		if ( ImGui::RadioButton( s_aGenderNames[ i ], ms_iGenderVar == i ) )
			RequestGender( i );
	}

	ImGui::EndGroup();

	ImGui::TextUnformatted( "Breed" );
	ImGui::SameLine( CHOICES_X );
	ImGui::BeginGroup();

	for ( TINT i = 0; i < NUM_BREEDS; i++ )
	{
		// Rows of four
		if ( i % 4 != 0 )
			ImGui::SameLine();

		if ( ImGui::RadioButton( s_aBreedNames[ i ], ms_iBreedVar == i ) )
			RequestBreed( i );
	}

	ImGui::EndGroup();
	ImGui::EndDisabled();

	if ( bOnFarm && s_iWantedGender != -1 )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.85f, 0.35f, 1.0f ), "Close this window to load the farm again with a %s cow.", s_aGenderNames[ s_iWantedGender ] );

		if ( ImGui::SmallButton( "Keep the gender" ) )
			RequestGender( -1 );
	}
	else
	{
		ImGui::TextDisabled( "A breed changes right away, a gender loads the farm again." );
	}

	ImGui::SeparatorText( "The other player" );

	if ( ACoopNet::GetMode() == ACoopNet::MODE_OFF )
	{
		ImGui::TextDisabled( "No network game (the Co-op menu of the main menu)." );
	}
	else if ( !ACoopNet::IsConnected() )
	{
		ImGui::TextDisabled( "Waiting for the other player..." );
	}
	else if ( !s_oRemoteLook.bKnown )
	{
		ImGui::TextUnformatted( "Their cow: not known yet." );
	}
	else
	{
		const TINT   iRemoteSkin = FindSkin( s_oRemoteLook.szSkin );
		const TCHAR* szRemote    = ( iRemoteSkin >= 0 ) ? s_aSkins[ iRemoteSkin ].szLabel : s_oRemoteLook.szSkin;

		ImGui::Text( "Their cow: %s %s%s%s%s", s_aGenderNames[ s_oRemoteLook.iGender ], s_aBreedNames[ s_oRemoteLook.iBreed ], IsCowSkin( s_oRemoteLook.szSkin ) ? "" : ", as ", IsCowSkin( s_oRemoteLook.szSkin ) ? "" : szRemote, ACoopNet::GetRemotePlayerUnit() ? "" : " (shown when you're both on the farm)" );
	}

	ImGui::SeparatorText( "Saving" );
	ImGui::TextWrapped( "Your cow belongs to your save, like the one picked for a new game: the game writes it with its next save. The character is kept by this computer." );
#endif
}
