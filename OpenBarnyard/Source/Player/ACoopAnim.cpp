#include "pch.h"
#include "ACoopAnim.h"
#include "ACoopNet.h"
#include "ASkins.h"
#include "Units/AUnit.h"
#include "Units/AAnimatedUnit.h"
#include "Animation/AAnimatableObject.h"
#include "Animation/AAnimatableObjectManager.h"
#include "Animation/AAnimationBreakpointSoundParticle.h"
#include "Animation/AModel.h"
#include "UnitMovement/Steering/ASteer.h"

#include <Render/TAnimation.h>
#include <Render/TSkeleton.h>
#include <Math/TMatrix44.h>
#include <Math/TQuaternion.h>

#include <cmath>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

//-----------------------------------------------------------------------------
// The default of ASkins::UpdateRemotePuppet, used until the skins branch defines it
// (a weak symbol: the linker takes this one only while there's no other)
//-----------------------------------------------------------------------------

extern "C" void __cdecl ACoopAnim_UpdateRemotePuppetDefault( AUnit* a_pRemoteUnit, TFLOAT a_fDeltaTime )
{
}

#pragma comment( linker, "/alternatename:?UpdateRemotePuppet@ASkins@@SAXPAVAUnit@@M@Z=_ACoopAnim_UpdateRemotePuppetDefault" )

//-----------------------------------------------------------------------------
// The pose message (CHANNEL_ANIMATION), little endian like everything here:
//  u32 magic, u32 nonce (of the sender's run), u16 sequence, u8 flags (POSEFLAGS),
//  u8 number of objects, f32 x3 offset of the model from the steer, f32 x4 its rotation,
//  then per object (the first is the model of the player, the others are attached to an earlier one):
//    str type, i8 parent (-1 for the first)
//    [attached] str parent bone, str own bone (empty: the origin), f32 x3 translation, f32 x4 rotation, f32 scale, u8 shown
//    str animation set, u8 number of animations,
//    per animation: u8 overlay, str name, f32 time, f32 speed, u8 weight (0-255)
//  a str is a u8 length and that many characters
//-----------------------------------------------------------------------------

static constexpr TUINT32 POSE_MAGIC  = 0x31415942; // "BYA1"
static constexpr TINT    MAX_NAME    = 32;
static constexpr TINT    MAX_OBJECTS = 8;
static constexpr TINT    MAX_ANIMS   = 10;

enum POSEFLAGS : TUINT8
{
	POSEFLAGS_VISIBLE = 1 << 0, // the sender's model is shown (not hidden by a scene)
};

struct PoseWriter
{
	char  aData[ ACoopNet::MAX_DATA_SIZE ];
	TINT  iSize = 0;
	TBOOL bFull = TFALSE;

	TBOOL Has( TINT a_iBytes ) const { return iSize + a_iBytes <= TINT( sizeof( aData ) ); }

	void Bytes( const void* a_pData, TINT a_iBytes )
	{
		if ( !Has( a_iBytes ) )
		{
			bFull = TTRUE;
			return;
		}

		TUtil::MemCopy( aData + iSize, a_pData, a_iBytes );
		iSize += a_iBytes;
	}

	void U8( TUINT8 a_uiValue ) { Bytes( &a_uiValue, 1 ); }
	void U16( TUINT16 a_uiValue ) { Bytes( &a_uiValue, 2 ); }
	void U32( TUINT32 a_uiValue ) { Bytes( &a_uiValue, 4 ); }
	void F32( TFLOAT a_fValue ) { Bytes( &a_fValue, 4 ); }

	void Str( const TCHAR* a_szText )
	{
		TINT iLength = a_szText ? TStringManager::String8Length( a_szText ) : 0;

		if ( iLength > MAX_NAME - 1 )
			iLength = MAX_NAME - 1;

		U8( TUINT8( iLength ) );
		Bytes( a_szText, iLength );
	}
};

struct PoseReader
{
	const TUINT8* pData;
	TINT          iSize;
	TINT          iPos   = 0;
	TBOOL         bError = TFALSE;

	PoseReader( const void* a_pData, TINT a_iSize )
	    : pData( TSTATICCAST( const TUINT8, a_pData ) ), iSize( a_iSize )
	{}

	TBOOL Bytes( void* a_pOut, TINT a_iBytes )
	{
		if ( bError || iPos + a_iBytes > iSize )
		{
			bError = TTRUE;
			TUtil::MemClear( a_pOut, a_iBytes );
			return TFALSE;
		}

		TUtil::MemCopy( a_pOut, pData + iPos, a_iBytes );
		iPos += a_iBytes;
		return TTRUE;
	}

	TUINT8 U8()
	{
		TUINT8 uiValue;
		Bytes( &uiValue, 1 );
		return uiValue;
	}

	TUINT16 U16()
	{
		TUINT16 uiValue;
		Bytes( &uiValue, 2 );
		return uiValue;
	}

	TUINT32 U32()
	{
		TUINT32 uiValue;
		Bytes( &uiValue, 4 );
		return uiValue;
	}

	TFLOAT F32()
	{
		TFLOAT fValue;
		Bytes( &fValue, 4 );

		if ( !std::isfinite( fValue ) || fValue > 1.0e6f || fValue < -1.0e6f )
		{
			bError = TTRUE;
			return 0.0f;
		}

		return fValue;
	}

	// Names of types, bones and animations: printable characters only
	void Str( TCHAR* a_szOut )
	{
		const TINT iLength = U8();

		if ( bError || iLength >= MAX_NAME || iPos + iLength > iSize )
		{
			bError        = TTRUE;
			a_szOut[ 0 ] = '\0';
			return;
		}

		for ( TINT i = 0; i < iLength; i++ )
		{
			const TCHAR cChar = TCHAR( pData[ iPos + i ] );

			if ( cChar < 0x20 || cChar > 0x7e )
			{
				bError        = TTRUE;
				a_szOut[ 0 ] = '\0';
				return;
			}

			a_szOut[ i ] = cChar;
		}

		a_szOut[ iLength ] = '\0';
		iPos += iLength;
	}
};

//-----------------------------------------------------------------------------
// The other game's pose
//-----------------------------------------------------------------------------

struct PoseAnim
{
	TCHAR     szName[ MAX_NAME ];
	TPString8 strName; // pooled once, when the pose comes
	TFLOAT    fTime;
	TFLOAT    fSpeed;
	TFLOAT    fWeight;
	TBOOL     bOverlay;
};

struct PoseObject
{
	TCHAR       szType[ MAX_NAME ];
	TINT        iParent; // -1 for the model of the player
	TCHAR       szParentBone[ MAX_NAME ];
	TCHAR       szBone[ MAX_NAME ];
	TVector4    vTranslation;
	TQuaternion qRotation;
	TFLOAT      fScale;
	TBOOL       bShown;
	TCHAR       szSet[ MAX_NAME ];
	TINT        iNumAnims;
	PoseAnim    aAnims[ MAX_ANIMS ];
};

struct Pose
{
	TBOOL       bKnown;
	TUINT32     uiNonce;
	TUINT16     uiSequence;
	TUINT8      uiFlags;
	TVector4    vOffset;
	TQuaternion qRotation;
	TINT        iNumObjects;
	PoseObject  aObjects[ MAX_OBJECTS ];
	DWORD       uiTime; // GetTickCount when it came
};

// Two, so that a pose is read completely before it's used
static Pose  s_aPoses[ 2 ];
static Pose* s_pRemotePose = &s_aPoses[ 0 ];

// Whether ACoopNet lets the remote cow be shown now
static TBOOL s_bRemoteShown = TTRUE;

// The remote cow as this module knows it
static AUnit*             s_pRemoteUnit       = TNULL;
static AAnimatableObject* s_pRemoteCowObject  = TNULL; // its model (not made here)
static TBOOL              s_bCowVisible       = TTRUE; // what was set on the unit last
static TQuaternion        s_qShownRotation    = TQuaternion::IDENTITY;
static TBOOL              s_bHasShownRotation = TFALSE;

// What shows the objects of the pose here: [0] the model of the partner (the remote cow's or a puppet made here),
// the others are copies made here and attached to an earlier one
struct ShownObject
{
	AAnimatableObject* pObject;
	TBOOL              bOwned;       // made here (destroyed here)
	TCHAR              szKey[ 112 ]; // what it shows: changes make it again
	TBOOL              bTried;       // with this key (it may have failed: the type isn't loaded here)
	TBOOL              bShown;       // the skeleton updating set last (the copies)
};

static ShownObject s_aShown[ MAX_OBJECTS ];
static TINT        s_iNumShown = 0;
static TBOOL       s_bRiding   = TFALSE; // [0] is a puppet

// The local player's pose goes out SEND_RATE times a second
static TFLOAT  s_fSendWait = 0.0f;
static TUINT32 s_uiNonce   = 0;
static TUINT16 s_uiSequence = 0;

// "-netlog": what the remote cow shows, when it changes
static TCHAR s_szLoggedMain[ MAX_NAME ] = "";

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
// Reading and writing the pose
//-----------------------------------------------------------------------------

// The rotation of a_rcMatrix (its basis vectors normalised first: a model may be scaled)
static void GetRotation( const TMatrix44& a_rcMatrix, TQuaternion& a_rOutRotation )
{
	TMatrix44 matRotation = a_rcMatrix;

	for ( TINT i = 0; i < 3; i++ )
	{
		TVector4& rBasis = matRotation.AsBasisVector4( BASISVECTOR( i ) );
		rBasis.w         = 0.0f;
		const TFLOAT fMagnitude = rBasis.Magnitude();

		if ( fMagnitude > 0.0001f )
			rBasis.Multiply( 1.0f / fMagnitude );
	}

	matRotation.SetTranslation( TVector4::VEC_ZERO );
	TMatrix44::MatToQuat( a_rOutRotation, matRotation );
}

static void WriteAnimations( PoseWriter& a_rWriter, AAnimatableObject* a_pObject )
{
	AToshiAnimationInterface& rAnims = a_pObject->GetToshiAnimInterface();

	const TINT iCountAt = a_rWriter.iSize;
	a_rWriter.U8( 0 );

	TINT iCount = 0;

	for ( TINT iList = 0; iList < 2; iList++ )
	{
		T2DList<AToshiAnimationRef>& rList = ( iList == 0 ) ? rAnims.GetAnimRefs() : rAnims.GetOverlayAnimRefs();

		T2_FOREACH( rList, it )
		{
			if ( iCount >= MAX_ANIMS )
				break;

			ANamedAnimation* pNamed = it->GetNamedAnimation();
			TAnimation*      pAnim  = pNamed ? rAnims.GetAnimation( *it ) : TNULL;

			if ( !pAnim || pAnim->GetState() == TAnimation::STATE_BLENDING_OUT )
				continue;

			const TCHAR* szName = pNamed->GetName().GetString();

			if ( !szName || !a_rWriter.Has( 2 + TStringManager::String8Length( szName ) + 9 ) )
				break;

			a_rWriter.U8( TUINT8( iList ) );
			a_rWriter.Str( szName );
			a_rWriter.F32( pAnim->GetSeqTime() );
			a_rWriter.F32( pAnim->GetSpeed() );
			a_rWriter.U8( TUINT8( TMath::Min( TMath::Max( pAnim->GetWeight(), 0.0f ), 1.0f ) * 255.0f + 0.5f ) );
			iCount++;
		}
	}

	if ( iCountAt < TINT( sizeof( a_rWriter.aData ) ) )
		a_rWriter.aData[ iCountAt ] = TCHAR( iCount );
}

static const TCHAR* GetSetName( AAnimatableObject* a_pObject )
{
	ANamedAnimationSetRef pSet = a_pObject->GetToshiAnimInterface().GetAnimationSet();
	return pSet.Get() ? pSet->GetName().GetString() : "";
}

static const TCHAR* GetBoneName( AAnimatableObject* a_pObject, TINT a_iBone )
{
	AModelInstance* pModelInstance = a_pObject->GetModelInstance().Get();

	if ( a_iBone < 0 || !pModelInstance || !pModelInstance->GetSkeletonInstance() || a_iBone >= pModelInstance->GetNumBones() )
		return "";

	const TCHAR* szName = pModelInstance->GetBoneName( a_iBone );
	return szName ? szName : "";
}

// Writes a_pObject, attached to the a_iParent'th object of the pose (-1: the model of the player). TFALSE if it
// didn't fit
static TBOOL WriteObject( PoseWriter& a_rWriter, AAnimatableObject* a_pObject, TINT a_iParent )
{
	// The object with its animations has to fit (the rest of the pose is left out)
	if ( !a_rWriter.Has( 160 ) )
		return TFALSE;

	a_rWriter.Str( a_pObject->GetObjectType()->GetName().GetString() );
	a_rWriter.U8( TUINT8( TINT8( a_iParent ) ) );

	if ( a_iParent >= 0 )
	{
		AAnimatableObject::AttachmentInfo& rInfo = a_pObject->GetAttachmentInfo();

		a_rWriter.Str( rInfo.pParentObject ? GetBoneName( rInfo.pParentObject, rInfo.iParentBone ) : "" );
		a_rWriter.Str( GetBoneName( a_pObject, rInfo.iBone ) );
		a_rWriter.F32( rInfo.translation.x );
		a_rWriter.F32( rInfo.translation.y );
		a_rWriter.F32( rInfo.translation.z );
		a_rWriter.F32( rInfo.quaternion.x );
		a_rWriter.F32( rInfo.quaternion.y );
		a_rWriter.F32( rInfo.quaternion.z );
		a_rWriter.F32( rInfo.quaternion.w );
		a_rWriter.F32( a_pObject->GetModelInstance()->GetScale().x );
		a_rWriter.U8( a_pObject->GetModelInstance()->IsUpdatingSkeleton() ? 1 : 0 );
	}

	a_rWriter.Str( GetSetName( a_pObject ) );
	WriteAnimations( a_rWriter, a_pObject );

	return !a_rWriter.bFull;
}

// Writes a_pObject (the model of the player) and what's attached to it, returns the number of objects written.
// The nearest first: what the cow wears and holds (the sunglasses, a carried unit: AAnimatedUnit::AttachUnit puts its
// model on a bone of the cow), then what's attached to those (the copies that make the bunch of a carried unit,
// ACarryableUnitHelper::Carry). Up to MAX_OBJECTS, and what fits: a big bunch loses its last copies, not the cow
// its sunglasses
static TINT WriteObjects( PoseWriter& a_rWriter, AAnimatableObject* a_pObject )
{
	if ( !a_pObject->GetObjectType() || !a_pObject->GetModelInstance() )
		return 0;

	AAnimatableObject* apObjects[ MAX_OBJECTS ];
	TINT               aiParents[ MAX_OBJECTS ];
	TINT               iNumObjects = 1;

	apObjects[ 0 ] = a_pObject;
	aiParents[ 0 ] = -1;

	// (The list is its own queue: a parent comes before what's attached to it)
	for ( TINT i = 0; i < iNumObjects; i++ )
	{
		T2_FOREACH( apObjects[ i ]->GetAttachedObjects(), it )
		{
			if ( iNumObjects >= MAX_OBJECTS )
				break;

			AAnimatableObject* pAttached = it;

			if ( !pAttached->GetObjectType() || !pAttached->GetModelInstance() )
				continue;

			apObjects[ iNumObjects ] = pAttached;
			aiParents[ iNumObjects ] = i;
			iNumObjects++;
		}
	}

	TINT iNumWritten = 0;

	for ( ; iNumWritten < iNumObjects; iNumWritten++ )
	{
		const TINT iSizeBefore = a_rWriter.iSize;

		if ( !WriteObject( a_rWriter, apObjects[ iNumWritten ], aiParents[ iNumWritten ] ) )
		{
			// Left out whole, with the ones after it
			a_rWriter.iSize = iSizeBefore;
			a_rWriter.bFull = TFALSE;
			break;
		}
	}

	return iNumWritten;
}

static void SendPose( AUnit* a_pLocalPlayer )
{
	AAnimatableObject* pObject = TSTATICCAST( AAnimatedUnit, a_pLocalPlayer )->GetAnimatableObject();

	if ( !pObject || !pObject->GetModelInstance() )
		return;

	if ( s_uiNonce == 0 )
		s_uiNonce = ( GetTickCount() * 2654435761u ) ^ ( GetCurrentProcessId() << 8 ) | 1;

	TMatrix44 matModel;
	pObject->GetModelInstance()->GetLocalTransform( matModel );

	TQuaternion qRotation;
	GetRotation( matModel, qRotation );

	TVector4 vSteer;
	a_pLocalPlayer->GetSteer()->CopyPosition( vSteer );

	const TVector4& rcModel = matModel.GetTranslation();

	static PoseWriter s_oWriter;
	PoseWriter&       rWriter = s_oWriter;
	rWriter.iSize             = 0;
	rWriter.bFull             = TFALSE;

	rWriter.U32( POSE_MAGIC );
	rWriter.U32( s_uiNonce );
	rWriter.U16( ++s_uiSequence );
	rWriter.U8( TSTATICCAST( AVisibleUnit, a_pLocalPlayer )->IsVisible() ? POSEFLAGS_VISIBLE : 0 );

	const TINT iCountAt = rWriter.iSize;
	rWriter.U8( 0 );

	rWriter.F32( rcModel.x - vSteer.x );
	rWriter.F32( rcModel.y - vSteer.y );
	rWriter.F32( rcModel.z - vSteer.z );
	rWriter.F32( qRotation.x );
	rWriter.F32( qRotation.y );
	rWriter.F32( qRotation.z );
	rWriter.F32( qRotation.w );

	const TINT iNumObjects = WriteObjects( rWriter, pObject );

	if ( iNumObjects <= 0 )
		return;

	rWriter.aData[ iCountAt ] = TCHAR( iNumObjects );
	ACoopNet::SendData( ACoopNet::CHANNEL_ANIMATION, rWriter.aData, rWriter.iSize );
}

static TBOOL ReadPose( PoseReader& a_rReader, Pose& a_rPose )
{
	a_rPose.uiNonce    = a_rReader.U32();
	a_rPose.uiSequence = a_rReader.U16();
	a_rPose.uiFlags    = a_rReader.U8();

	const TINT iNumObjects = a_rReader.U8();

	a_rPose.vOffset.x = a_rReader.F32();
	a_rPose.vOffset.y = a_rReader.F32();
	a_rPose.vOffset.z = a_rReader.F32();
	a_rPose.vOffset.w = 0.0f;

	a_rPose.qRotation.x = a_rReader.F32();
	a_rPose.qRotation.y = a_rReader.F32();
	a_rPose.qRotation.z = a_rReader.F32();
	a_rPose.qRotation.w = a_rReader.F32();

	if ( a_rReader.bError || iNumObjects < 1 || iNumObjects > MAX_OBJECTS )
		return TFALSE;

	// A model far from the steer is a broken pose
	if ( a_rPose.vOffset.MagnitudeSq() > 25.0f )
		return TFALSE;

	for ( TINT i = 0; i < iNumObjects; i++ )
	{
		PoseObject& rObject = a_rPose.aObjects[ i ];

		a_rReader.Str( rObject.szType );
		rObject.iParent = TINT8( a_rReader.U8() );

		// The model of the player first, the others attached to an earlier one
		if ( ( i == 0 ) != ( rObject.iParent < 0 ) || rObject.iParent >= i )
			return TFALSE;

		if ( rObject.iParent >= 0 )
		{
			a_rReader.Str( rObject.szParentBone );
			a_rReader.Str( rObject.szBone );
			rObject.vTranslation.x = a_rReader.F32();
			rObject.vTranslation.y = a_rReader.F32();
			rObject.vTranslation.z = a_rReader.F32();
			rObject.vTranslation.w = 1.0f;
			rObject.qRotation.x    = a_rReader.F32();
			rObject.qRotation.y    = a_rReader.F32();
			rObject.qRotation.z    = a_rReader.F32();
			rObject.qRotation.w    = a_rReader.F32();
			rObject.fScale         = a_rReader.F32();
			rObject.bShown         = a_rReader.U8() != 0;

			if ( rObject.fScale <= 0.01f || rObject.fScale > 100.0f )
				rObject.fScale = 1.0f;
		}
		else
		{
			rObject.szParentBone[ 0 ] = '\0';
			rObject.szBone[ 0 ]       = '\0';
			rObject.vTranslation      = TVector4::VEC_ZERO;
			rObject.qRotation         = TQuaternion::IDENTITY;
			rObject.fScale            = 1.0f;
			rObject.bShown            = TTRUE;
		}

		a_rReader.Str( rObject.szSet );
		rObject.iNumAnims = a_rReader.U8();

		if ( a_rReader.bError || rObject.iNumAnims > MAX_ANIMS )
			return TFALSE;

		for ( TINT k = 0; k < rObject.iNumAnims; k++ )
		{
			PoseAnim& rAnim = rObject.aAnims[ k ];
			rAnim.bOverlay  = a_rReader.U8() != 0;
			a_rReader.Str( rAnim.szName );
			rAnim.fTime   = a_rReader.F32();
			rAnim.fSpeed  = a_rReader.F32();
			rAnim.fWeight = TFLOAT( a_rReader.U8() ) / 255.0f;

			if ( a_rReader.bError || rAnim.szName[ 0 ] == '\0' || TMath::Abs( rAnim.fSpeed ) > 100.0f )
				return TFALSE;
		}
	}

	if ( a_rReader.bError )
		return TFALSE;

	a_rPose.iNumObjects = iNumObjects;

	// The names are pooled once here, not every frame
	for ( TINT i = 0; i < iNumObjects; i++ )
	{
		for ( TINT k = 0; k < a_rPose.aObjects[ i ].iNumAnims; k++ )
			a_rPose.aObjects[ i ].aAnims[ k ].strName = TPString8( a_rPose.aObjects[ i ].aAnims[ k ].szName );
	}

	return TTRUE;
}

// CHANNEL_ANIMATION
static void OnPoseData( const void* a_pData, TINT a_iSize )
{
	PoseReader oReader( a_pData, a_iSize );

	if ( oReader.U32() != POSE_MAGIC )
		return;

	Pose& rNew = ( s_pRemotePose == &s_aPoses[ 0 ] ) ? s_aPoses[ 1 ] : s_aPoses[ 0 ];

	if ( !ReadPose( oReader, rNew ) )
		return;

	// Not an older one of the same run (the counter wraps around)
	if ( s_pRemotePose->bKnown && rNew.uiNonce == s_pRemotePose->uiNonce && TINT16( rNew.uiSequence - s_pRemotePose->uiSequence ) <= 0 )
		return;

	rNew.bKnown   = TTRUE;
	rNew.uiTime   = GetTickCount();
	s_pRemotePose = &rNew;
}

//-----------------------------------------------------------------------------
// Showing the pose
//-----------------------------------------------------------------------------

static TBOOL IsCowType( const TCHAR* a_szType )
{
	static const TCHAR* s_aszCowTypes[] = { "american_m", "american_f", "barn_american_m", "barn_american_f" };

	for ( TINT i = 0; i < TARRAYSIZE( s_aszCowTypes ); i++ )
	{
		if ( TStringManager::String8CompareNoCase( a_szType, s_aszCowTypes[ i ] ) == 0 )
			return TTRUE;
	}

	return TFALSE;
}

// The type called a_szType, or the one of the other gender ("american_f_bike" / "american_m_bike") if it isn't loaded
static AAnimatableObjectType* FindTypeOrOtherGender( const TCHAR* a_szType )
{
	AAnimatableObjectManager* pManager = AAnimatableObjectManager::GetSingleton();
	AAnimatableObjectType*    pType    = pManager->FindType( TPString8( a_szType ) );

	if ( pType )
		return pType;

	TCHAR szOther[ MAX_NAME ];
	TStringManager::String8CopySafe( szOther, a_szType, MAX_NAME );

	for ( TINT i = 0; szOther[ i ] != '\0' && szOther[ i + 1 ] != '\0'; i++ )
	{
		if ( szOther[ i ] == '_' && ( szOther[ i + 1 ] == 'm' || szOther[ i + 1 ] == 'f' ) && ( szOther[ i + 2 ] == '_' || szOther[ i + 2 ] == '\0' ) )
		{
			szOther[ i + 1 ] = ( szOther[ i + 1 ] == 'm' ) ? 'f' : 'm';
			return pManager->FindType( TPString8( szOther ) );
		}
	}

	return TNULL;
}

// The bikes: the one loaded here if the partner's isn't (only the best bike owned is loaded)
static AAnimatableObjectType* FindAttachmentType( const TCHAR* a_szType )
{
	AAnimatableObjectManager* pManager = AAnimatableObjectManager::GetSingleton();
	AAnimatableObjectType*    pType    = pManager->FindType( TPString8( a_szType ) );

	if ( pType )
		return pType;

	if ( TStringManager::String8CompareNoCase( a_szType, "bike", 4 ) == 0 )
	{
		static const TCHAR* s_aszBikes[] = { "bike1", "bike2", "bike3", "bike4", "bike5" };

		for ( TINT i = 0; i < TARRAYSIZE( s_aszBikes ); i++ )
		{
			if ( ( pType = pManager->FindType( TPString8( s_aszBikes[ i ] ) ) ) != TNULL )
				return pType;
		}
	}

	return TNULL;
}

static void DestroyShown( TINT a_iFrom )
{
	// The last first: an object takes what's attached to it, the copies come after their parent
	for ( TINT i = s_iNumShown - 1; i >= a_iFrom; i-- )
	{
		ShownObject& rShown = s_aShown[ i ];

		if ( rShown.bOwned && rShown.pObject && AAnimatableObjectManager::IsSingletonCreated() )
		{
			AAnimatableObject* pObject = rShown.pObject;
			rShown.pObject             = TNULL;
			AAnimatableObjectManager::GetSingleton()->DestroyAnimatableObject( pObject );
		}

		rShown.pObject   = TNULL;
		rShown.bOwned    = TFALSE;
		rShown.szKey[ 0 ] = '\0';
		rShown.bTried    = TFALSE;
		rShown.bShown    = TFALSE;
	}

	if ( a_iFrom < s_iNumShown )
		s_iNumShown = a_iFrom;

	if ( a_iFrom == 0 )
		s_bRiding = TFALSE;
}

static void SetCowVisible( TBOOL a_bVisible )
{
	if ( s_pRemoteUnit && s_bCowVisible != a_bVisible )
	{
		s_bCowVisible = a_bVisible;
		TSTATICCAST( AVisibleUnit, s_pRemoteUnit )->SetVisible( a_bVisible );

		// AAnimatedUnit::SetVisible sets the skeleton updating of what's attached to the cow too (while riding the
		// copies are on the puppet)
		for ( TINT k = 1; k < s_iNumShown && !s_bRiding; k++ )
			s_aShown[ k ].bShown = a_bVisible;
	}
}

// Makes a model of a_pType that only shows (no controllers: nothing moves it but this module)
static AAnimatableObject* CreateCopy( AAnimatableObjectType* a_pType )
{
	AAnimatableObject* pObject = AAnimatableObjectManager::GetSingleton()->CreateAnimatableObject( a_pType, TNULL, AAnimatableObject::CREATE_FLAGS_NO_ANIM_CONTROLLER );

	if ( !pObject )
		return TNULL;

	if ( !pObject->GetModelInstance() )
	{
		AAnimatableObjectManager::GetSingleton()->DestroyAnimatableObject( pObject );
		return TNULL;
	}

	// As AAnimatedUnit makes the models of units
	pObject->SetSkeletonUpdating( TTRUE, TTRUE );
	pObject->GetModelInstance()->GetSceneObject()->DisableOpacity();

	TSceneObject* pSceneObject         = pObject->GetModelInstance()->GetSceneObject();
	pSceneObject->GetFadeDistances().x = 50.0f;
	pSceneObject->GetFadeDistances().y = 50.1f;
	pSceneObject->GetFadeParams().x    = 10.000153f;

	pObject->GetModelInstance()->SetShadowMode( AModelInstance::SHADOWMODE_DYNAMIC );
	return pObject;
}

// The animation refs of every model come from one pool of 100 (g_oAnimationRefPool): the partner's models only start
// animations while plenty are left for the game's own
static constexpr TINT MIN_FREE_ANIMATION_REFS = 16;

static TINT GetFreeAnimationRefs()
{
	return TINT( ( g_oAnimationRefPool.GetCapacity() - g_oAnimationRefPool.GetUsedSize() ) / sizeof( AToshiAnimationRef ) );
}

// The circular difference of two times of a looped animation of a_fDuration
static TFLOAT GetLoopDifference( TFLOAT a_fA, TFLOAT a_fB, TFLOAT a_fDuration )
{
	TFLOAT fDifference = fmodf( a_fA - a_fB, a_fDuration );

	if ( fDifference > a_fDuration * 0.5f )
		fDifference -= a_fDuration;
	else if ( fDifference < -a_fDuration * 0.5f )
		fDifference += a_fDuration;

	return fDifference;
}

// Plays the animations of a_rcPose on a_pObject, a_fAge seconds after they were sent
static void ApplyAnimations( AAnimatableObject* a_pObject, const PoseObject& a_rcPose, TFLOAT a_fAge )
{
	AToshiAnimationInterface& rAnims = a_pObject->GetToshiAnimInterface();
	AAnimatableObjectType*    pType  = a_pObject->GetObjectType();

	if ( !pType )
		return;

	// The set first (the carried items have their own)
	if ( a_rcPose.szSet[ 0 ] != '\0' && TStringManager::String8Compare( GetSetName( a_pObject ), a_rcPose.szSet ) != 0 )
	{
		ANamedAnimationSetRef pWanted = pType->FindAnimationSetOrDefault( TPString8( a_rcPose.szSet ) );

		if ( pWanted.Get() && pWanted.Get() != rAnims.GetAnimationSet().Get() )
			rAnims.SetAnimationSet( pWanted );
	}

	ANamedAnimationSetRef pSet = rAnims.GetAnimationSet();

	if ( !pSet.Get() )
		return;

	TBOOL bHasMain = TFALSE;

	for ( TINT i = 0; i < a_rcPose.iNumAnims; i++ )
	{
		const PoseAnim&  rcAnim = a_rcPose.aAnims[ i ];
		ANamedAnimation* pNamed = pSet->GetNamedAnimation( rcAnim.strName );

		// Unknown here (a playing animation of another set): left out, PlayAnim needs an animation of the set
		if ( !pNamed || pNamed->IsOverlay() != rcAnim.bOverlay )
			continue;

		if ( !rcAnim.bOverlay )
			bHasMain = TTRUE;

		T2DList<AToshiAnimationRef>& rList = rcAnim.bOverlay ? rAnims.GetOverlayAnimRefs() : rAnims.GetAnimRefs();
		AToshiAnimationRef*          pRef  = AToshiAnimationInterface::GetAnimationRef( rList, rcAnim.strName );

		if ( !pRef )
		{
			if ( !rcAnim.bOverlay )
			{
				// A main animation starts with the transition of the set, on the next update of the model
				AToshiAnimationRef* pHead = rAnims.GetAnim();

				if ( pHead && pHead->GetTransition() && pHead->GetTransition()->GetAnimationName() == rcAnim.strName )
					continue;
			}

			if ( GetFreeAnimationRefs() < MIN_FREE_ANIMATION_REFS )
				continue;

			pRef = rAnims.PlayAnim( rcAnim.strName, rcAnim.bOverlay ? rcAnim.fWeight : -1.0f, TFALSE );

			if ( !pRef )
				continue;
		}

		TAnimation* pAnim = rAnims.GetAnimation( *pRef );

		if ( !pAnim || pAnim->GetState() == TAnimation::STATE_BLENDING_OUT )
			continue;

		pAnim->SetSpeed( rcAnim.fSpeed );

		// In time with the sender's (only when it's off: the time of a playing animation isn't touched for nothing)
		TSkeletonSequence* pSequence = pAnim->GetSequencePtr();
		const TFLOAT       fDuration = pSequence ? pSequence->GetDuration() : 0.0f;

		if ( fDuration > 0.01f )
		{
			TFLOAT       fTarget = rcAnim.fTime + rcAnim.fSpeed * a_fAge;
			const TBOOL  bLooped = !pAnim->IsManaged();
			TFLOAT       fDifference;

			if ( bLooped )
			{
				fTarget = fmodf( fTarget, fDuration );

				if ( fTarget < 0.0f )
					fTarget += fDuration;

				fDifference = GetLoopDifference( pAnim->GetSeqTime(), fTarget, fDuration );
			}
			else
			{
				fTarget     = TMath::Min( TMath::Max( fTarget, 0.0f ), fDuration );
				fDifference = pAnim->GetSeqTime() - fTarget;
			}

			if ( TMath::Abs( fDifference ) > ( bLooped ? 0.2f : 0.1f ) )
				pAnim->SetTime( fTarget );
		}

		if ( rcAnim.bOverlay && TMath::Abs( pAnim->GetDestWeight() - rcAnim.fWeight ) > 0.01f )
			rAnims.SetAnimWeight( pRef, rcAnim.fWeight );
	}

	// What the sender doesn't play any more blends out (a main one only once a main animation of the pose plays)
	for ( TINT iList = 0; iList < 2; iList++ )
	{
		const TBOOL                  bOverlay = iList == 1;
		T2DList<AToshiAnimationRef>& rList    = bOverlay ? rAnims.GetOverlayAnimRefs() : rAnims.GetAnimRefs();

		if ( !bOverlay && !bHasMain )
			continue;

		T2_FOREACH( rList, it )
		{
			ANamedAnimation* pNamed = it->GetNamedAnimation();
			TAnimation*      pAnim  = pNamed ? rAnims.GetAnimation( *it ) : TNULL;

			if ( !pAnim || pAnim->GetState() == TAnimation::STATE_BLENDING_OUT )
				continue;

			TBOOL bInPose = TFALSE;

			for ( TINT i = 0; i < a_rcPose.iNumAnims && !bInPose; i++ )
				bInPose = a_rcPose.aAnims[ i ].bOverlay == bOverlay && a_rcPose.aAnims[ i ].strName == pNamed->GetName();

			// A main one with a transition to a wanted one is on its way out already
			if ( !bInPose && !bOverlay && it->GetTransition() )
			{
				for ( TINT i = 0; i < a_rcPose.iNumAnims && !bInPose; i++ )
					bInPose = !a_rcPose.aAnims[ i ].bOverlay && a_rcPose.aAnims[ i ].strName == it->GetTransition()->GetAnimationName();

				if ( bInPose )
					continue;
			}

			if ( !bInPose )
				rAnims.StopAnim( *it );
		}
	}
}

// The key of what shows pose object a_iIndex: when it changes, it's made again
static void GetKey( const PoseObject& a_rcObject, TINT a_iIndex, TBOOL a_bRiding, TCHAR* a_szOut, TINT a_iSize )
{
	if ( a_iIndex == 0 )
		TStringManager::String8Format( a_szOut, a_iSize, a_bRiding ? "puppet:%s" : "cow", a_rcObject.szType );
	else
		TStringManager::String8Format( a_szOut, a_iSize, "%s|%d|%s|%s", a_rcObject.szType, a_rcObject.iParent, a_rcObject.szParentBone, a_rcObject.szBone );
}

// Makes the objects of the pose that changed, returns TFALSE if the model of the partner can't be shown
static void UpdateShownObjects( const Pose& a_rcPose )
{
	// The model of the partner: the remote cow, or a puppet of the model it has (the bike's rider)
	const PoseObject& rcAvatar = a_rcPose.aObjects[ 0 ];
	TBOOL             bRiding  = !IsCowType( rcAvatar.szType ) && FindTypeOrOtherGender( rcAvatar.szType ) != TNULL;

	for ( TINT i = 0; i < a_rcPose.iNumObjects; i++ )
	{
		TCHAR szKey[ sizeof( s_aShown[ 0 ].szKey ) ];
		GetKey( a_rcPose.aObjects[ i ], i, bRiding, szKey, sizeof( szKey ) );

		ShownObject& rShown = s_aShown[ i ];

		// The same as before, and still there (the engine may have destroyed it with its parent)
		if ( i < s_iNumShown && TStringManager::String8Compare( rShown.szKey, szKey ) == 0 && ( rShown.pObject || rShown.bTried ) )
			continue;

		DestroyShown( i );

		for ( TINT k = i; k < a_rcPose.iNumObjects; k++ )
		{
			ShownObject& rMake = s_aShown[ k ];
			GetKey( a_rcPose.aObjects[ k ], k, bRiding, rMake.szKey, sizeof( rMake.szKey ) );
			rMake.pObject = TNULL;
			rMake.bOwned  = TFALSE;
			rMake.bTried  = TTRUE;
			rMake.bShown  = TFALSE;

			const PoseObject& rcObject = a_rcPose.aObjects[ k ];

			if ( k == 0 )
			{
				if ( bRiding )
				{
					rMake.pObject = CreateCopy( FindTypeOrOtherGender( rcObject.szType ) );
					rMake.bOwned  = rMake.pObject != TNULL;
					rMake.bShown  = rMake.pObject != TNULL;

					if ( IsLogEnabled() )
						TINFO( "[coopanim] the partner is shown with the model %s%s\n", rcObject.szType, rMake.pObject ? "" : " (it couldn't be made)" );
				}

				if ( !rMake.pObject )
				{
					rMake.pObject = s_pRemoteCowObject;
					rMake.bOwned  = TFALSE;
				}

				s_bRiding = rMake.bOwned;
				continue;
			}

			// A copy attached to its parent, if both have the bones
			AAnimatableObject*     pParent = s_aShown[ rcObject.iParent ].pObject;
			AAnimatableObjectType* pType   = FindAttachmentType( rcObject.szType );

			if ( !pParent || !pType || !pParent->GetModelInstance() || !pParent->GetModelInstance()->GetSkeletonInstance() )
			{
				if ( IsLogEnabled() )
					TINFO( "[coopanim] the partner's %s isn't shown (%s)\n", rcObject.szType, pType ? "nothing to attach it to" : "not loaded here" );

				continue;
			}

			const TINT iParentBone = ( rcObject.szParentBone[ 0 ] != '\0' ) ? pParent->GetModelInstance()->GetBoneID( TPString8( rcObject.szParentBone ) ) : -1;

			if ( iParentBone < 0 )
			{
				if ( IsLogEnabled() )
					TINFO( "[coopanim] the partner's %s isn't shown (the model here has no bone %s)\n", rcObject.szType, rcObject.szParentBone );

				continue;
			}

			AAnimatableObject* pCopy = CreateCopy( pType );

			if ( !pCopy )
				continue;

			TINT iBone = -1;

			if ( rcObject.szBone[ 0 ] != '\0' && pCopy->GetModelInstance()->GetSkeletonInstance() )
				iBone = pCopy->GetModelInstance()->GetBoneID( TPString8( rcObject.szBone ) );

			AAnimatableObjectManager::GetSingleton()->AttachObject( pCopy, pParent, TINT8( iBone ), TINT8( iParentBone ), rcObject.vTranslation, rcObject.qRotation, rcObject.fScale );

			rMake.pObject = pCopy;
			rMake.bOwned  = TTRUE;
			rMake.bShown  = TTRUE;

			if ( IsLogEnabled() )
				TINFO( "[coopanim] the partner has %s on %s\n", rcObject.szType, rcObject.szParentBone );
		}

		s_iNumShown = a_rcPose.iNumObjects;
		break;
	}

	// Fewer objects than before
	if ( s_iNumShown > a_rcPose.iNumObjects )
		DestroyShown( a_rcPose.iNumObjects );
}

// Where the partner's model is: the remote cow's steer plus the model's offset, the shown rotation
static void GetRemoteModelTransform( TMatrix44& a_rOutMatrix )
{
	a_rOutMatrix.SetFromQuaternion( s_qShownRotation );

	TVector4 vPosition;
	s_pRemoteUnit->GetSteer()->CopyPosition( vPosition );
	vPosition.x += s_pRemotePose->vOffset.x;
	vPosition.y += s_pRemotePose->vOffset.y;
	vPosition.z += s_pRemotePose->vOffset.z;
	vPosition.w = 1.0f;

	a_rOutMatrix.SetTranslation( vPosition );
}

static void ShowRemotePose( TFLOAT a_fDeltaTime )
{
	const Pose& rcPose = *s_pRemotePose;

	if ( !rcPose.bKnown || !s_pRemoteCowObject )
	{
		SetCowVisible( s_bRemoteShown );
		return;
	}

	const TFLOAT fAge = TMath::Min( TFLOAT( GetTickCount() - rcPose.uiTime ) * 0.001f, 0.5f );

	UpdateShownObjects( rcPose );

	// The rotation follows the pose's smoothly (the states come 30 times a second)
	if ( !s_bHasShownRotation )
	{
		s_qShownRotation    = rcPose.qRotation;
		s_bHasShownRotation = TTRUE;
	}
	else
	{
		TQuaternion qTarget = rcPose.qRotation;

		// The shorter way round
		if ( s_qShownRotation.x * qTarget.x + s_qShownRotation.y * qTarget.y + s_qShownRotation.z * qTarget.z + s_qShownRotation.w * qTarget.w < 0.0f )
			qTarget.Negate();

		TQuaternion qBlended;
		TQuaternion::Nlerp( qBlended, s_qShownRotation, qTarget, TMath::Min( a_fDeltaTime * 15.0f, 1.0f ) );
		s_qShownRotation = qBlended;
	}

	const TBOOL bVisible = s_bRemoteShown && ( rcPose.uiFlags & POSEFLAGS_VISIBLE );

	SetCowVisible( bVisible && !s_bRiding );

	for ( TINT i = 0; i < s_iNumShown; i++ )
	{
		ShownObject&       rShown   = s_aShown[ i ];
		const PoseObject&  rcObject = rcPose.aObjects[ i ];
		AAnimatableObject* pObject  = rShown.pObject;

		if ( !pObject || !pObject->GetModelInstance() )
			continue;

		if ( i == 0 && rShown.bOwned )
		{
			// The puppet: placed here, shown with the partner
			const TBOOL bShown = bVisible;

			if ( rShown.bShown != bShown )
			{
				rShown.bShown = bShown;
				pObject->SetSkeletonUpdating( bShown, TTRUE );

				// The copies on it have that state now too, they get their own again below
				for ( TINT k = 1; k < s_iNumShown; k++ )
					s_aShown[ k ].bShown = bShown;
			}

			TMatrix44 matModel;
			GetRemoteModelTransform( matModel );
			pObject->GetModelInstance()->SetTransform( matModel );
		}
		else if ( i > 0 && rShown.bOwned )
		{
			// A copy is shown when the sender's is and its model is
			const TBOOL bShown = bVisible && rcObject.bShown;

			if ( rShown.bShown != bShown )
			{
				rShown.bShown = bShown;
				pObject->SetSkeletonUpdating( bShown, TFALSE );
			}

			pObject->SetAttachmentTransform( rcObject.vTranslation, rcObject.qRotation );
		}

		if ( pObject->GetModelInstance()->IsUpdatingSkeleton() )
			ApplyAnimations( pObject, rcObject, fAge );
	}

	if ( IsLogEnabled() )
	{
		const TCHAR* szMain = ACoopAnim::GetRemoteMainAnimation();

		if ( TStringManager::String8Compare( szMain, s_szLoggedMain ) != 0 )
		{
			TStringManager::String8CopySafe( s_szLoggedMain, szMain, MAX_NAME );
			TINFO( "[coopanim] the partner plays %s (%s, %d objects%s)\n", szMain[ 0 ] ? szMain : "nothing", rcPose.aObjects[ 0 ].szType, rcPose.iNumObjects, s_bRiding ? ", riding" : "" );
		}
	}
}

//-----------------------------------------------------------------------------
// ACoopAnim
//-----------------------------------------------------------------------------

// Registers the data handler before the first session
static struct ACoopAnimRegistration
{
	ACoopAnimRegistration() { ACoopNet::SetDataHandler( ACoopNet::CHANNEL_ANIMATION, OnPoseData ); }
} s_oRegistration;

void ACoopAnim::Update( TFLOAT a_fDeltaTime, AUnit* a_pLocalPlayer, AUnit* a_pRemoteUnit )
{
	// This game's player goes to the other one
	s_fSendWait -= a_fDeltaTime;

	if ( s_fSendWait <= 0.0f )
	{
		s_fSendWait = 1.0f / SEND_RATE;

		if ( a_pLocalPlayer && ACoopNet::IsConnected() )
			SendPose( a_pLocalPlayer );
	}

	// The other game's player on the remote cow
	if ( a_pRemoteUnit != s_pRemoteUnit )
	{
		OnRemoteUnitGone();

		s_pRemoteUnit = a_pRemoteUnit;
		s_bCowVisible = TTRUE;
	}

	if ( !s_pRemoteUnit )
		return;

	s_pRemoteCowObject = TSTATICCAST( AAnimatedUnit, s_pRemoteUnit )->GetAnimatableObject();

	// A new player sends its own pose
	if ( !ACoopNet::IsConnected() )
		s_pRemotePose->bKnown = TFALSE;

	ShowRemotePose( a_fDeltaTime );
}

void ACoopAnim::SetRemoteShown( TBOOL a_bShown )
{
	s_bRemoteShown = a_bShown;
}

TBOOL ACoopAnim::UpdateRemoteModel( AUnit* a_pUnit, AAnimatableObject* a_pObject, TFLOAT a_fDeltaTime )
{
	if ( a_pUnit != s_pRemoteUnit || !s_pRemotePose->bKnown || !s_bHasShownRotation || !a_pObject->GetModelInstance() )
		return TFALSE;

	TMatrix44 matModel;
	GetRemoteModelTransform( matModel );
	a_pObject->GetModelInstance()->SetTransform( matModel );
	return TTRUE;
}

TBOOL ACoopAnim::IsBreakpointAllowed( AAnimatableObject* a_pObject, AAnimationBreakpoint* a_pBreakpoint )
{
	if ( !s_pRemoteCowObject && s_iNumShown == 0 )
		return TTRUE;

	TBOOL bRemote = a_pObject == s_pRemoteCowObject;

	for ( TINT i = 0; i < s_iNumShown && !bRemote; i++ )
		bRemote = a_pObject == s_aShown[ i ].pObject;

	// The sounds and the particles of the partner's animations, the rest is done by the partner's game
	return !bRemote || ( a_pBreakpoint && a_pBreakpoint->IsA( &TGetClass( AAnimationBreakpointSoundParticle ) ) );
}

void ACoopAnim::OnObjectDeleted( AAnimatableObject* a_pObject )
{
	if ( a_pObject == s_pRemoteCowObject )
		s_pRemoteCowObject = TNULL;

	for ( TINT i = 0; i < s_iNumShown; i++ )
	{
		if ( s_aShown[ i ].pObject == a_pObject )
			s_aShown[ i ].pObject = TNULL;
	}
}

void ACoopAnim::OnTypeDestroyed( AAnimatableObjectType* a_pType )
{
	for ( TINT i = 0; i < s_iNumShown; i++ )
	{
		if ( s_aShown[ i ].bOwned && s_aShown[ i ].pObject && s_aShown[ i ].pObject->GetObjectType() == a_pType )
		{
			DestroyShown( i );
			break;
		}
	}
}

void ACoopAnim::OnRemoteUnitGone()
{
	DestroyShown( 0 );

	s_pRemoteUnit       = TNULL;
	s_pRemoteCowObject  = TNULL;
	s_bCowVisible       = TTRUE;
	s_bHasShownRotation = TFALSE;
	s_szLoggedMain[ 0 ] = '\0';
}

void ACoopAnim::OnSessionEnded()
{
	OnRemoteUnitGone();

	s_aPoses[ 0 ].bKnown = TFALSE;
	s_aPoses[ 1 ].bKnown = TFALSE;
	s_bRemoteShown       = TTRUE;
}

AAnimatableObject* ACoopAnim::GetRemoteAvatar()
{
	if ( !s_pRemoteUnit )
		return TNULL;

	AAnimatableObject* pObject = ( s_iNumShown > 0 && s_aShown[ 0 ].pObject ) ? s_aShown[ 0 ].pObject : s_pRemoteCowObject;

	if ( !pObject || !pObject->GetModelInstance() || !pObject->GetModelInstance()->IsUpdatingSkeleton() )
		return TNULL;

	return pObject;
}

TBOOL ACoopAnim::IsRemoteRiding()
{
	return s_pRemoteUnit && s_bRiding;
}

const TCHAR* ACoopAnim::GetRemoteMainAnimation()
{
	const Pose& rcPose = *s_pRemotePose;

	if ( !rcPose.bKnown || rcPose.iNumObjects < 1 )
		return "";

	for ( TINT i = 0; i < rcPose.aObjects[ 0 ].iNumAnims; i++ )
	{
		if ( !rcPose.aObjects[ 0 ].aAnims[ i ].bOverlay )
			return rcPose.aObjects[ 0 ].aAnims[ i ].szName;
	}

	return "";
}

TBOOL ACoopAnim::IsRemoteFemale()
{
	const Pose& rcPose = *s_pRemotePose;

	// "american_f", "barn_american_f", "american_f_bike"
	return rcPose.bKnown && rcPose.iNumObjects > 0 && TStringManager::String8FindString( rcPose.aObjects[ 0 ].szType, "american_f" ) != TNULL;
}

TBOOL ACoopAnim::IsLocalPlaying( AUnit* a_pLocalPlayer, const TCHAR* a_szAnimation )
{
	if ( !a_pLocalPlayer || !a_pLocalPlayer->IsA( &TGetClass( AAnimatedUnit ) ) )
		return TFALSE;

	AAnimatableObject* pObject = TSTATICCAST( AAnimatedUnit, a_pLocalPlayer )->GetAnimatableObject();

	return pObject && pObject->GetToshiAnimInterface().IsAnimPlaying( TPString8( a_szAnimation ), TFALSE );
}
