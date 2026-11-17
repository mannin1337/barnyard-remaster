#include "pch.h"
#include "Player/ACoopMinigame.h"
#include "AWhackARaccMiniGameState.h"
#include "AMiniGameManager.h"
#include "ARandom.h"
#include "Tasks/ARootTask.h"
#include "Animation/AAnimatableObject.h"
#include "Animation/AAnimatableObjectManager.h"
#include "Animation/AAnimationBreakpointCallback.h"
#include "Cameras/ACameraManager.h"
#include "Cameras/ACamera.h"
#include "FX/AGameTimeFXManager.h"
#include "FX/AParticleEffectManager.h"
#include "GameInterface/AAnticGameOverState.h"
#include "GameInterface/AGameStateController.h"
#include "GameInterface/ASSeqState.h"
#include "GUI/AGUI2.h"
#include "GUI/AGUI2AnythingPopup.h"
#include "GUI/AGUI2LifeHud.h"
#include "Helpers/AGameLoader.h"
#include "Helpers/AHighScoreManager.h"
#include "Input/AInputHandler.h"
#include "Input/AInputMapManager.h"
#include "Locale/ALocale.h"
#include "Locale/ALocaleManager.h"
#include "Player/APlayerManager.h"
#include "Player/APlayerProfileManager.h"
#include "Render/ARenderer.h"
#include "Render/ASkyDome.h"
#include "Render/ASunFlareEffect.h"
#include "Sound/ASoundManager.h"
#include "Terrain/ATerrainInterface.h"
#include "Units/AUnitManager.h"
#include "Units/ASSeqController.h"

#include <File/TTRB.h>
#include <GUI/T2GUIFontManager.h>
#include <Input/TInputDeviceMouse.h>
#include <Math/TMath.h>
#include <Math/TMathInline.h>
#include <Math/TMatrix44.h>
#include <Plugins/PPropertyParser/PPropertyValue.h>
#include <Render/TModel.h>
#include <Render/TAnimation.h>
#include <Render/TRenderInterface.h>
#include <Render/TSkeleton.h>
#include <cmath>
#include <cstring>
#include <Toshi/TString8.h>
#include <Toshi/TStringManager.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TPSTRING8_DECLARE( AWhackARaccMiniGameState );
TPSTRING8_DECLARE( down );
TPSTRING8_DECLARE( hit );
TPSTRING8_DECLARE( idle );
TPSTRING8_DECLARE( idle_1 );
TPSTRING8_DECLARE( impact );
TPSTRING8_DECLARE( impactbp );
TPSTRING8_DECLARE( reset );
TPSTRING8_DECLARE( up );

// NOTE: GetClass 004d8a30, CreateObject 004dbc70, CreateObjectInPlace 004dbcd0 (the constructor is inlined in both)
TDEFINE_CLASS( AWhackARaccMiniGameState );

// Sounds
static constexpr ASoundId SOUND_MISS         = 305;
static constexpr ASoundId SOUND_HIT          = 306;
static constexpr ASoundId SOUND_LOOP         = 313;
static constexpr ASoundId SOUND_INTRO        = 314;
static constexpr ASoundId SOUND_DIAL         = 315;
static constexpr ASoundId SOUND_HIT_COYOTE   = 316;
static constexpr ASoundId SOUND_HIT_PECK     = 317;
static constexpr ASoundId SOUND_HIT_EGG      = 318;
static constexpr ASoundId SOUND_FREDDY_UP    = 319;
static constexpr ASoundId SOUND_FREDDY_HIT_3 = 321;
static constexpr ASoundId SOUND_FREDDY_HIT_1 = 322;
static constexpr ASoundId SOUND_FREDDY_HIT_2 = 323;
static constexpr ASoundId SOUND_CROWD        = 324;

static constexpr TFLOAT STICK_DEADZONE = 0.2f;  // 00773b64
static constexpr TFLOAT BUCK_SPIN      = 10.5f; // 00773b68
static constexpr TFLOAT MAX_PICK_DIST  = 2500.0f;

static const TVector4 s_vEffectDir( 0.0f, -1.0f, 0.0f, 1.0f ); // 0077d024
static const TVector4 s_vMoleDir( -1.0f, 0.0f, 0.0f, 1.0f );   // 0077d014
static const TVector4 s_vArrowDir( 0.0f, 0.0f, 1.0f, 1.0f );   // 0077d004
static const TVector4 s_vDialDir( 0.0f, 0.0f, -1.0f, 1.0f );   // 0077d034

// The level the game starts at (never written)
static TINT s_iStartLevel = 0; // 00783268

// Whether the stick/keys hold the ring off the middle row/column
static TBOOL s_bRowHeld = TFALSE; // 0078329c
static TBOOL s_bColHeld = TFALSE; // 0078329d

// Squared distance of a_rcPoint to the line through a_rcLineStart and a_rcLineEnd
// $Barnyard: FUNCTION 006b8ba0
static TFLOAT DistancePointToLineSq( const TVector4& a_rcPoint, const TVector4& a_rcLineStart, const TVector4& a_rcLineEnd )
{
	TFLOAT fT;
	return TMath::DistancePointToLineSq( a_rcPoint, a_rcLineStart, a_rcLineEnd, fT );
}

// NOTE: same as the static helper in ABikeSteer.cpp (004083c0)
static TUINT32 PackColour( TFLOAT a_fRed, TFLOAT a_fGreen, TFLOAT a_fBlue, TFLOAT a_fAlpha )
{
	return ( ( ( TUINT32( a_fGreen * 255.0f ) | ( ( TUINT32( a_fRed * 255.0f ) | ( TUINT32( a_fAlpha * 255.0f ) << 8 ) ) << 8 ) ) << 8 ) | TUINT32( a_fBlue * 255.0f ) );
}

// Builds the transform of the flat objects (rings, dials, the arrow): a_rcPosition, rotated by a_fRotX around X
static void MakeFlatTransform( TMatrix44& a_rOutMatrix, const TVector4& a_rcPosition, TFLOAT a_fRotX )
{
	a_rOutMatrix = TMatrix44::IDENTITY;
	TMatrix44::TransformVector( a_rOutMatrix.GetTranslation(), a_rOutMatrix, a_rcPosition );
	a_rOutMatrix.RotateX( a_fRotX );
}

// Visual-only, fixed-layout payload. No native player, hole or level state is
// overwritten by the viewer. The network layer binds this to the active turn.
struct AWhackSpectatorFrame
{
	struct Actor
	{
		TFLOAT position[ 3 ];
		TINT16 rotation[ 4 ];
		TUINT8 flags, opacity, scale, reserved; // visible/updating, 0..255, /64
	};
	struct Anim { TUINT8 id; TINT8 speed; TUINT16 milliseconds; }; // speed /16
	TUINT32 schema, serial;
	Actor actors[ 33 ]; // 9 raccoons, 4 specials, 20 bucks
	Anim animations[ 15 ]; // animals, mallet main and overlay
	TFLOAT ringPulse, badTime, dialAngles[ 4 ], elapsed, timeLimit, shake[ 2 ];
	TINT32 score, levelScore;
	TUINT8 hole, malletHole, level, lives, badHole, flags, reserved[ 2 ];
};
static_assert( sizeof( AWhackSpectatorFrame ) == 916 );

namespace
{
using WhackFrame = AWhackSpectatorFrame;
constexpr TUINT32 WHACK_VIEW_SCHEMA = 0x31565742; // BWV1
constexpr TINT WHACK_VIEW_OBJECTS = 41;
// Stable animation ids, never pointers or names supplied by the peer.
const TCHAR* s_viewAnimNames[] = {
	"", "idle", "idle_1", "up", "down", "hit", "impact", "reset",
	"hole_1_imp", "hole_1_rec", "hole_1_rst", "hole_2_imp", "hole_2_rec", "hole_2_rst",
	"hole_3_imp", "hole_3_rec", "hole_3_rst", "hole_4_imp", "hole_4_rec", "hole_4_rst",
	"hole_5_imp", "hole_5_rec", "hole_5_rst", "hole_6_imp", "hole_6_rec", "hole_6_rst",
	"hole_7_imp", "hole_7_rec", "hole_7_rst", "hole_8_imp", "hole_8_rec", "hole_8_rst",
	"hole_9_imp", "hole_9_rec", "hole_9_rst"
};
struct WhackObjectBackup
{
	TMatrix44 matrix;
	TVector4 scale;
	TFLOAT opacity;
	TBOOL visible, updating, opacityEnabled;
};
struct WhackView
{
	AWhackARaccMiniGameState* owner = TNULL;
	WhackFrame own{}, current{}, start{}, target{};
	WhackObjectBackup objects[ WHACK_VIEW_OBJECTS ];
	TUINT32 serial = 0, receivedSerial = 0;
	TFLOAT age = 0, duration = 0, elapsed = 0;
	TBOOL startHidden, timeHidden, pickupHidden, guiVisible, levelVisible;
} s_whackView;

TFLOAT WhackLerp( TFLOAT a, TFLOAT b, TFLOAT t ) { return a + ( b - a ) * t; }

void BlendWhackAnim( WhackFrame::Anim& out, const WhackFrame::Anim& start, TFLOAT t )
{
	// Clip changes, direction changes and loop/restart boundaries are discrete.
	const TINT step = TINT( out.milliseconds ) - TINT( start.milliseconds );
	if ( out.id && start.id == out.id && start.speed == out.speed && step * out.speed > 0 && std::abs( step ) < 1000 )
		out.milliseconds = TUINT16( WhackLerp( start.milliseconds, out.milliseconds, t ) + 0.5f );
}

void BlendWhackFrame( WhackFrame& out, const WhackFrame& start, const WhackFrame& target, TFLOAT t,
	const AWhackARaccMiniGameState::Hole ( &holes )[ AWhackARaccMiniGameState::NUM_HOLES ] )
{
	out = target;
	if ( t >= 1.0f ) return;
	for ( TINT i = 0; i < TARRAYSIZE( out.actors ); ++i )
	{
		const auto& a = start.actors[ i ];
		auto& b = out.actors[ i ];
		if ( a.flags != 3 || b.flags != 3 ) continue;
		if ( a.position[ 0 ] != b.position[ 0 ] || a.position[ 2 ] != b.position[ 2 ] )
		{
			// Animals only move on respawn. A buck can also fly towards the HUD.
			// A pooled slot landing exactly on a new hole is a spawn even when
			// packet loss hid the invisible interval.
			if ( i < 13 ) continue;
			TBOOL spawned = TFALSE;
			for ( const auto& hole : holes )
				if ( hole.vPos.x == b.position[ 0 ] && hole.vPos.z == b.position[ 2 ] ) { spawned = TTRUE; break; }
			if ( spawned ) continue;
		}
		for ( TINT j = 0; j < 3; ++j ) b.position[ j ] = WhackLerp( a.position[ j ], b.position[ j ], t );
		b.opacity = TUINT8( WhackLerp( a.opacity, b.opacity, t ) + 0.5f );
		b.scale = TUINT8( WhackLerp( a.scale, b.scale, t ) + 0.5f );
		// The consumer normalizes the quaternion. Choose its shortest arc here.
		TFLOAT dot = 0;
		for ( TINT j = 0; j < 4; ++j ) dot += TFLOAT( a.rotation[ j ] ) * b.rotation[ j ];
		for ( TINT j = 0; j < 4; ++j )
		{
			const TFLOAT v = WhackLerp( dot < 0 ? -TFLOAT( a.rotation[ j ] ) : TFLOAT( a.rotation[ j ] ), b.rotation[ j ], t );
			b.rotation[ j ] = TINT16( TMath::Min( v, 32767.0f ) );
		}
		if ( i < 13 ) BlendWhackAnim( out.animations[ i ], start.animations[ i ], t );
	}
	if ( ( start.flags & 4 ) && ( target.flags & 4 ) && start.malletHole == target.malletHole )
		for ( TINT i = 13; i < 15; ++i ) BlendWhackAnim( out.animations[ i ], start.animations[ i ], t );
	if ( start.hole == target.hole && target.ringPulse >= start.ringPulse && target.ringPulse - start.ringPulse < 1 )
		out.ringPulse = WhackLerp( start.ringPulse, target.ringPulse, t );
	if ( ( start.flags & 8 ) && ( target.flags & 8 ) && start.badHole == target.badHole && target.badTime <= start.badTime )
		out.badTime = WhackLerp( start.badTime, target.badTime, t );
	for ( TINT i = 0; i < TARRAYSIZE( out.dialAngles ); ++i )
		if ( std::fabs( target.dialAngles[ i ] - start.dialAngles[ i ] ) < 1 )
			out.dialAngles[ i ] = WhackLerp( start.dialAngles[ i ], target.dialAngles[ i ], t );
	out.elapsed = WhackLerp( start.elapsed, target.elapsed, t );
}

TBOOL WhackFinite( TFLOAT value, TFLOAT low, TFLOAT high )
{
	return std::isfinite( value ) && value >= low && value <= high;
}
TBOOL ValidWhackFrame( const WhackFrame& f )
{
	if ( f.schema != WHACK_VIEW_SCHEMA || !f.serial || f.hole >= 9 || f.malletHole >= 9 ||
		f.badHole >= 9 || f.level >= 10 || f.lives > 8 || ( f.flags & ~15 ) ||
		f.reserved[ 0 ] || f.reserved[ 1 ] || f.score < 0 || f.score > 10000000 ||
		f.levelScore < 0 || f.levelScore > 10000000 ||
		!WhackFinite( f.ringPulse, -100000, 100000 ) || !WhackFinite( f.badTime, -10, 100 ) ||
		!WhackFinite( f.elapsed, -1, 10000 ) || !WhackFinite( f.timeLimit, 0.01f, 10000 ) ||
		!WhackFinite( f.shake[ 0 ], -100, 100 ) || !WhackFinite( f.shake[ 1 ], -100, 100 ) ) return TFALSE;
	for ( TFLOAT angle : f.dialAngles ) if ( !WhackFinite( angle, -4, 8 ) ) return TFALSE;
	for ( const auto& a : f.actors )
	{
		if ( a.reserved || ( a.flags & ~3 ) || !a.scale ) return TFALSE;
		for ( TFLOAT value : a.position ) if ( !WhackFinite( value, -10000, 10000 ) ) return TFALSE;
		TFLOAT length = 0;
		for ( TINT16 value : a.rotation ) length += ( value / 32767.0f ) * ( value / 32767.0f );
		if ( !WhackFinite( length, 0.9f, 1.1f ) ) return TFALSE;
	}
	for ( const auto& a : f.animations )
		if ( a.id >= TARRAYSIZE( s_viewAnimNames ) ) return TFALSE;
	return TTRUE;
}
WhackFrame::Anim CaptureWhackAnim( AAnimatableObject* object, TBOOL overlay )
{
	WhackFrame::Anim result{};
	auto& anims = object->GetToshiAnimInterface();
	auto& list = overlay ? anims.GetOverlayAnimRefs() : anims.GetAnimRefs();
	TFLOAT bestWeight = -1;
	for ( auto it = list.Begin(); it != list.End(); ++it )
	{
		auto* named = it->GetNamedAnimation();
		auto* anim = named ? anims.GetAnimation( *it ) : TNULL;
		if ( !anim || anim->GetState() == TAnimation::STATE_BLENDING_OUT || anim->GetWeight() < bestWeight ) continue;
		const TCHAR* name = named->GetName().GetString();
		if ( !name ) continue;
		for ( TINT i = 1; i < TARRAYSIZE( s_viewAnimNames ); ++i )
			if ( std::strcmp( name, s_viewAnimNames[ i ] ) == 0 )
			{
				result.id = TUINT8( i );
				result.speed = TINT8( TMath::Min( TMath::Max( anim->GetSpeed() * 16, -127.0f ), 127.0f ) );
				result.milliseconds = TUINT16( TMath::Min( TMath::Max( anim->GetSeqTime() * 1000.0f, 0.0f ), 65535.0f ) );
				bestWeight = anim->GetWeight();
				break;
			}
	}
	return result;
}
void ApplyWhackAnims( AAnimatableObject* object, const WhackFrame::Anim& main, const WhackFrame::Anim& overlay, TBOOL fresh, TBOOL spectating )
{
	if ( fresh && ( CaptureWhackAnim( object, TFALSE ).id != main.id || CaptureWhackAnim( object, TTRUE ).id != overlay.id ) )
		object->KillAllAnimations();
	const WhackFrame::Anim animations[] = { main, overlay };
	for ( const auto& a : animations )
	{
		if ( !a.id ) continue;
		TPString8 name( s_viewAnimNames[ a.id ] );
		auto& anims = object->GetToshiAnimInterface();
		auto* ref = anims.FindAnimationRef( name );
		if ( fresh && !ref && object->HasAnimation( name ) ) ref = anims.PlayAnim( name, -1.0f, TFALSE );
		TAnimation* clip = ref ? anims.GetAnimation( *ref ) : TNULL;
		if ( !clip ) continue;
		// Sampling owns the remote playhead. Letting the skeleton also advance
		// would present each interval twice and rewind on the following packet.
		clip->SetSpeed( spectating ? 0.0f : a.speed / 16.0f );
		const TFLOAT duration = clip->GetSequencePtr()->GetDuration();
		const TFLOAT time = a.milliseconds * 0.001f;
		if ( duration > 0.0f ) clip->SetTime( spectating && time >= duration ? std::nextafter( duration, 0.0f ) : time );
	}
	if ( spectating && ( main.id || overlay.id ) && object->GetModelInstance()->IsUpdatingSkeleton() )
		object->GetModelInstance()->GetSkeletonInstance()->UpdateState( TTRUE );
}

#if defined( BARNYARD_COMMUNITY_PATCH ) && !defined( TOSHI_FINAL )
struct WhackMotionProbe
{
	TBOOL enabled = TFALSE;
	TINT actor = -1;
	TUINT32 serial = 0;
	TUINT16 clipTime = 0;
	TVector4 prior[ TANIMATION_MAXBONES ];

	void Observe( AModelInstance* model, const WhackFrame& f, TINT index, TBOOL fresh )
	{
		auto* skeleton = model->GetSkeletonInstance();
		const TBOOL animal = index < 13;
		const TUINT16 time = animal ? f.animations[ index ].milliseconds : 0;
		const TBOOL same = !fresh && actor == index && serial == f.serial && ( !animal || clipTime != time );
		const TINT count = animal ? TMath::Min( skeleton->GetSkeleton()->GetAutoBoneCount(), TANIMATION_MAXBONES ) : 1;
		for ( TINT i = 0; i < count; ++i )
		{
			TVector4 pos;
			if ( animal ) pos = skeleton->GetBoneTransformCurrent( i ).GetTranslation();
			else model->GetPosition( pos );
			if ( same && TVector4::DistanceSq( pos, prior[ i ] ) > 0.00000001f )
			{
				TINFO( "[coopmini-view] whack motion between packets serial=%u actor=%d bone=%d distance=%.6f\n",
					f.serial, index, animal ? i : -1, TMath::Sqrt( TVector4::DistanceSq( pos, prior[ i ] ) ) );
				enabled = TFALSE;
				break;
			}
			prior[ i ] = pos;
		}
		actor = index;
		serial = f.serial;
		clipTime = time;
	}
} s_whackProbe;
#endif
}

AAnimatableObject* AWhackARaccMiniGameState::SpectatorObject( TINT index )
{
	if ( index < 9 ) return m_aRaccoons[ index ].pObject;
	if ( index < 13 ) return m_aSpecials[ index - 9 ].pObject;
	if ( index < 33 ) return m_aBucks[ index - 13 ].pObject;
	if ( index == 33 ) return m_pMallet;
	if ( index == 34 ) return m_pRing;
	if ( index == 35 ) return m_pBadRing;
	if ( index == 36 ) return m_pArrow;
	return m_aDials[ index - 37 ].pObject;
}

void AWhackARaccMiniGameState::CaptureSpectatorFrame( WhackFrame& f )
{
	f = {};
	f.schema = WHACK_VIEW_SCHEMA;
	if ( !++s_whackView.serial ) ++s_whackView.serial;
	f.serial = s_whackView.serial;
	for ( TINT i = 0; i < 33; ++i )
	{
		auto* object = SpectatorObject( i );
		AModelInstance* model = object->GetModelInstance();
		TMatrix44 matrix;
		model->GetLocalTransform( matrix );
		TQuaternion rotation;
		TMatrix44::MatToQuat( rotation, matrix );
		const TFLOAT length = rotation.Magnitude();
		if ( length > 0.001f ) rotation.AsVector4().Multiply4( 1.0f / length );
		else rotation.SetIdentity();
		auto& a = f.actors[ i ];
		const auto& position = matrix.GetTranslation();
		a.position[ 0 ] = position.x; a.position[ 1 ] = position.y; a.position[ 2 ] = position.z;
		a.rotation[ 0 ] = TINT16( rotation.x * 32767 ); a.rotation[ 1 ] = TINT16( rotation.y * 32767 );
		a.rotation[ 2 ] = TINT16( rotation.z * 32767 ); a.rotation[ 3 ] = TINT16( rotation.w * 32767 );
		a.flags = ( model->IsVisible() ? 1 : 0 ) | ( model->IsUpdatingSkeleton() ? 2 : 0 );
		a.opacity = TUINT8( TMath::Min( TMath::Max( model->GetOpacity(), 0.0f ), 1.0f ) * 255 );
		a.scale = TUINT8( TMath::Min( TMath::Max( model->GetScale().x * 64, 1.0f ), 255.0f ) );
		if ( i < 13 ) f.animations[ i ] = CaptureWhackAnim( object, TFALSE );
	}
	f.animations[ 13 ] = CaptureWhackAnim( m_pMallet, TFALSE );
	f.animations[ 14 ] = CaptureWhackAnim( m_pMallet, TTRUE );
	const auto& player = GetCurrentPlayer();
	f.ringPulse = player.fRingPulse;
	f.badTime = m_bBadHit ? m_fBadRingTime : 0.0f;
	for ( TINT i = 0; i < 4; ++i ) f.dialAngles[ i ] = m_aDials[ i ].fAngle;
	f.elapsed = m_fElapsedTime; f.timeLimit = m_fTimeLimit;
	f.shake[ 0 ] = f.shake[ 1 ] = 0; // fixed spectator camera
	f.score = player.iScore; f.levelScore = player.iLevelScore;
	f.hole = TUINT8( player.iHole ); f.malletHole = TUINT8( player.iMalletHole );
	f.level = TUINT8( m_iLevel ); f.lives = TUINT8( player.iLives );
	f.flags = ( !m_oTimeLeftHudElement.IsForceHidden() ? 1 : 0 ) |
		( m_oStartHudElement.IsForceHidden() ? 2 : 0 ) |
		( m_pMallet->GetModelInstance()->IsVisible() ? 4 : 0 ) | ( m_bBadHit ? 8 : 0 );
	TFLOAT closest = 1e20f;
	for ( TINT i = 0; m_bBadHit && i < 9; ++i )
	{
		const auto& p = m_aHoles[ i ].vPos;
		TFLOAT dx = p.x - player.vBadRingPos.x, dz = p.z - player.vBadRingPos.z;
		if ( dx * dx + dz * dz < closest ) { closest = dx * dx + dz * dz; f.badHole = TUINT8( i ); }
	}
}

void AWhackARaccMiniGameState::ApplySpectatorFrame( const WhackFrame& f, TBOOL fresh, TBOOL spectating )
{
	const WhackFrame::Anim noAnimation{};
	for ( TINT i = 0; i < 33; ++i )
	{
		const auto& a = f.actors[ i ];
		if ( !fresh && a.flags != 3 ) continue;
		auto* object = SpectatorObject( i );
		AModelInstance* model = object->GetModelInstance();
		TQuaternion rotation( a.rotation[ 0 ] / 32767.0f, a.rotation[ 1 ] / 32767.0f,
			a.rotation[ 2 ] / 32767.0f, a.rotation[ 3 ] / 32767.0f );
		rotation.AsVector4().Multiply4( 1.0f / rotation.Magnitude() );
		model->SetTransform( TVector4( a.position[ 0 ], a.position[ 1 ], a.position[ 2 ], 1 ), rotation );
		const TFLOAT scale = a.scale / 64.0f;
		model->SetScale( TVector4( scale, scale, scale, 1 ) );
		object->SetOpacity( a.opacity / 255.0f, TTRUE );
		if ( fresh )
		{
			object->SetVisible( ( a.flags & 1 ) != 0 );
			object->SetSkeletonUpdating( ( a.flags & 2 ) != 0, TTRUE );
		}
		if ( i < 13 ) ApplyWhackAnims( object, f.animations[ i ], noAnimation, fresh, spectating );
	}
#if defined( BARNYARD_COMMUNITY_PATCH ) && !defined( TOSHI_FINAL )
	if ( s_whackProbe.enabled && spectating )
		for ( TINT i = 0; i < TARRAYSIZE( f.actors ); ++i )
			if ( f.actors[ i ].flags == 3 && ( i >= 13 || f.animations[ i ].id ) )
			{
				s_whackProbe.Observe( SpectatorObject( i )->GetModelInstance(), f, i, fresh );
				break;
			}
#endif
	ApplyWhackAnims( m_pMallet, f.animations[ 13 ], f.animations[ 14 ], fresh, spectating );
	if ( fresh )
	{
		m_pMallet->SetVisible( ( f.flags & 4 ) != 0 );
		m_pMallet->SetSkeletonUpdating( TTRUE, TTRUE );
	}
	TMatrix44 matrix;
	TVector4 position = m_aHoles[ f.hole ].vPos;
	position.y -= 0.01f;
	MakeFlatTransform( matrix, position, TMath::HALF_PI );
	const TFLOAT pulse = TMath::Sin( f.ringPulse ) * 0.2f + 1;
	matrix.Scale( pulse, pulse, pulse );
	m_pRing->GetModelInstance()->SetTransform( matrix );
	m_pRing->SetVisible( TTRUE );
	m_pRing->SetSkeletonUpdating( TTRUE, TTRUE );
	position = m_aHoles[ f.badHole ].vPos;
	position.y -= 0.05f;
	MakeFlatTransform( matrix, position, TMath::HALF_PI );
	const TFLOAT opacity = TMath::Min( TMath::Max( f.badTime / m_aFloats[ FLOAT_BAD_RING_TIME ], 0.0f ), 1.0f );
	matrix.Scale( 2 - opacity, 2 - opacity, 2 - opacity );
	m_pBadRing->GetModelInstance()->SetTransform( matrix );
	m_pBadRing->SetOpacity( opacity, TTRUE );
	m_pBadRing->SetVisible( TTRUE );
	m_pBadRing->SetSkeletonUpdating( ( f.flags & 8 ) != 0, TTRUE );
	for ( TINT i = 0; i < 4; ++i )
	{
		MakeFlatTransform( matrix, m_aDials[ i ].vPos, TMath::HALF_PI );
		matrix.RotateY( -f.dialAngles[ i ] );
		m_aDials[ i ].pObject->GetModelInstance()->SetTransform( matrix );
	}
	const TFLOAT elapsed = ( f.flags & 2 ) ? TMath::Max( f.elapsed, 0.0f ) : 0;
	MakeFlatTransform( matrix, m_vArrowPos, TMath::PI * 2 * elapsed / f.timeLimit + TMath::HALF_PI );
	m_pArrow->GetModelInstance()->SetTransform( matrix );
	m_oTimeLeftHudElement.SetTime( TMath::Max( f.timeLimit - f.elapsed, 0.0f ) );
	if ( !fresh ) return;
	ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
	camera->GetMatrix().GetTranslation() = ATerrainInterface::GetSingleton()->GetLocatorPos( "camera" );
	camera->LookAtPoint( ATerrainInterface::GetSingleton()->GetLocatorPos( "camera_target" ) );
	// The native camera is fixed. The viewer needs no player/steer/camera helper.
	m_oStartHudElement.SetForceHide( TTRUE );
	m_oTimeLeftHudElement.SetForceHide( ( f.flags & 1 ) == 0 );
	m_oPortrait.SetScore( f.score );
	m_oPortrait.Show();
	m_pLifeHud->SetLives( f.lives );
	m_pLifeHud->SetVisible( TTRUE );
	m_oScoreFloats.Hide();
	m_oPickUpHudElement.SetForceHide( TTRUE );
	m_pMessage->Hide();
	m_GUIElement.Show();
}

void AWhackARaccMiniGameState::SetSpectatorMode( TBOOL spectating )
{
	if ( spectating )
	{
		CaptureSpectatorFrame( s_whackView.own );
		s_whackView.owner = this;
		s_whackView.current = s_whackView.own;
		s_whackView.receivedSerial = 0;
		s_whackView.age = s_whackView.elapsed = s_whackView.duration = 0;
#if defined( BARNYARD_COMMUNITY_PATCH ) && !defined( TOSHI_FINAL )
		s_whackProbe.enabled = TStringManager::String8FindString( AGameLoader::Dev_GetCommandLine(), "-coopminigame" ) != TNULL;
		s_whackProbe.actor = -1;
#endif
		s_whackView.startHidden = m_oStartHudElement.IsForceHidden();
		s_whackView.timeHidden = m_oTimeLeftHudElement.IsForceHidden();
		s_whackView.pickupHidden = m_oPickUpHudElement.IsForceHidden();
		s_whackView.guiVisible = m_GUIElement.IsVisible();
		s_whackView.levelVisible = m_oLevelText.IsVisible();
		for ( TINT i = 0; i < WHACK_VIEW_OBJECTS; ++i )
		{
			AModelInstance* model = SpectatorObject( i )->GetModelInstance();
			auto& backup = s_whackView.objects[ i ];
			model->GetLocalTransform( backup.matrix );
			backup.scale = model->GetScale(); backup.opacity = model->GetOpacity();
			backup.visible = model->IsVisible(); backup.updating = model->IsUpdatingSkeleton();
			backup.opacityEnabled = ( model->GetSceneObject()->GetFlags() & TSceneObject::FLAGS_OPACITY ) != 0;
		}
		m_oStartHudElement.SetForceHide( TTRUE );
		m_oLevelText.Hide();
		TINFO( "[coopmini-view] whack spectator entered (native state preserved)\n" );
		return;
	}
	if ( s_whackView.owner != this ) return;
	ApplySpectatorFrame( s_whackView.own, TTRUE, TFALSE );
	for ( TINT i = 0; i < WHACK_VIEW_OBJECTS; ++i )
	{
		auto* object = SpectatorObject( i );
		AModelInstance* model = object->GetModelInstance();
		const auto& backup = s_whackView.objects[ i ];
		model->SetTransform( backup.matrix ); model->SetScale( backup.scale );
		object->SetOpacity( backup.opacity, TTRUE );
		if ( !backup.opacityEnabled ) model->ResetOpacity();
		object->SetVisible( backup.visible ); object->SetSkeletonUpdating( backup.updating, TTRUE );
	}
	// ResetLevel has already prepared the next local level before yielding.
	// Its idle mallet must not replay the previous level's impact breakpoint.
	if ( GetCurrentPlayer().eMalletState == MALLETSTATE_IDLE )
	{
		m_pMallet->KillAllAnimations();
		m_pMallet->PlayAnimation( TPS8( idle ) );
	}
	m_oStartHudElement.SetForceHide( s_whackView.startHidden );
	m_oTimeLeftHudElement.SetForceHide( s_whackView.timeHidden );
	m_oPickUpHudElement.SetForceHide( s_whackView.pickupHidden );
	m_pLifeHud->SetVisible( !s_whackView.timeHidden );
	if ( s_whackView.timeHidden ) m_oPortrait.Hide();
	else { m_oPortrait.Show(); m_oScoreFloats.Show(); }
	if ( !s_whackView.guiVisible ) m_GUIElement.Hide();
	if ( s_whackView.levelVisible ) m_oLevelText.Show();
	else m_oLevelText.Hide();
	m_pMessage->ShowIfHasText();
	s_whackView.owner = TNULL;
	TASSERT( GetCurrentPlayer().iScore == s_whackView.own.score && GetCurrentPlayer().iLives == s_whackView.own.lives && m_fElapsedTime == s_whackView.own.elapsed );
	TINFO( "[coopmini-view] whack native visuals restored score=%d lives=%d level=%d elapsed=%.3f (unchanged)\n", GetCurrentPlayer().iScore, GetCurrentPlayer().iLives, GetCurrentPlayer().iLevel, m_fElapsedTime );
}

TBOOL AWhackARaccMiniGameState::OnUpdate( TFLOAT dt )
{
	if ( ACoopMinigame::IsSpectating( this ) )
	{
		if ( s_whackView.owner != this ) SetSpectatorMode( TTRUE );
		auto& view = s_whackView;
		view.age += dt;
		WhackFrame frame;
		const TBOOL fresh = ACoopMinigame::ReadSpectatorFrame( this, &frame, sizeof( frame ) ) &&
			( !view.receivedSerial || TINT32( frame.serial - view.receivedSerial ) > 0 ) && ValidWhackFrame( frame );
		if ( fresh )
		{
			const TBOOL first = !view.receivedSerial;
			const TBOOL cut = first || view.age >= 0.2f || frame.level != view.target.level ||
				frame.elapsed < view.target.elapsed || frame.timeLimit != view.target.timeLimit ||
				( ( frame.flags ^ view.target.flags ) & 3 ) != 0;
			view.start = view.current;
			view.target = frame;
			view.receivedSerial = frame.serial;
			view.duration = cut ? 0.0f : TMath::Max( 1.0f / 30.0f, TMath::Min( view.age, 0.1f ) );
			view.age = view.elapsed = 0;
			if ( first ) TINFO( "[coopmini-view] whack live frame applied serial=%u score=%d bytes=%u\n", frame.serial, frame.score, TUINT( sizeof( frame ) ) );
		}
		if ( fresh || view.elapsed < view.duration )
		{
			view.elapsed += dt;
			const TFLOAT t = view.elapsed < view.duration ? view.elapsed / view.duration : 1.0f;
			BlendWhackFrame( view.current, view.start, view.target, t, m_aHoles );
			ApplySpectatorFrame( view.current, fresh, TTRUE );
		}
		// Before the first packet the saved local scene may still have live clips.
		if ( !view.receivedSerial && view.age > 0.5f )
		{
			// Disabling skeleton updates also disables rendering in TSceneObject.
			// Stop animation time instead, keeping the last received pose visible.
			for ( TINT i = 0; i < 13; ++i ) SpectatorObject( i )->SetAnimSpeed( 0 );
			m_pMallet->SetAnimSpeed( 0 );
		}
		return AGameState::OnUpdate( dt );
	}
	if ( s_whackView.owner == this ) SetSpectatorMode( TFALSE );
	const TBOOL result = AMiniGameState::OnUpdate( dt );
	if ( ACoopMinigame::ShouldCaptureSpectatorFrame( this, dt ) )
	{
		WhackFrame frame;
		CaptureSpectatorFrame( frame );
		if ( ValidWhackFrame( frame ) ) ACoopMinigame::PublishSpectatorFrame( this, &frame, sizeof( frame ) );
	}
	return result;
}

// $Barnyard: FUNCTION 004da280
AWhackARaccMiniGameState::AWhackARaccMiniGameState()
{
	m_bBadHit         = TFALSE;
	m_iLoopCue        = -1;
	m_iIntroCue       = -1;
	m_iUnkCue         = -1;
	m_iFloatIndex     = 0;
	m_fMessageTimer   = -1.0f;
	m_fShakeTimer     = -1.0f;
	m_fShakeTimeLeft  = -1.0f;
	m_iCurrentPlayer  = 0;
	m_pMessage        = TNULL;
	m_pLifeHud        = TNULL;
	m_bTurnEnded      = TFALSE;
	m_bRestartPending = TFALSE;
	m_iFreddyCue      = -1;
	m_iCrowdCue       = -1;
	m_bGameEnding     = TFALSE;

	TStringManager::String16Format( m_wszLevelText, 31, L"" );

	m_pInputMap                   = AInputMapManager::GetSingleton()->FindMap( TPS8( AWhackARaccMiniGameState ) );
	m_aEffects[ EFFECT_MISS ]     = AParticleEffectManager::FindEffect( "whackmiss" );
	m_aEffects[ EFFECT_PUFF ]     = AParticleEffectManager::FindEffect( "whackpuff" );
	m_aEffects[ EFFECT_DUST ]     = AParticleEffectManager::FindEffect( "whackdust" );
	m_aEffects[ EFFECT_BAD ]      = AParticleEffectManager::FindEffect( "whackbad" );
	m_aEffects[ EFFECT_EGGSPLAT ] = AParticleEffectManager::FindEffect( "eggsplat" );
	m_aEffects[ EFFECT_EGGSHELL ] = AParticleEffectManager::FindEffect( "eggshell" );
	m_aEffects[ EFFECT_EGGYOLK ]  = AParticleEffectManager::FindEffect( "eggyolk" );
}

// $Barnyard: FUNCTION 004dbd30
// $Barnyard: FUNCTION 004dbd10
AWhackARaccMiniGameState::~AWhackARaccMiniGameState()
{
}

// $Barnyard: FUNCTION 004d8a40
void AWhackARaccMiniGameState::LoadData()
{
	m_aInts[ INT_RACCOON_SCORE ] = 1;
	m_aInts[ INT_COYOTE_SCORE ]  = 1;
	m_aInts[ INT_PECK_SCORE ]    = 1;
	m_aInts[ INT_EGG_SCORE ]     = 1;
	m_aInts[ INT_FREDDY_SCORE ]  = 1;
	m_aInts[ INT_LIVES ]         = 3;

	m_aFloats[ FLOAT_BAD_RING_TIME ]    = 1.0f;
	m_aFloats[ FLOAT_MESSAGE_TIME ]     = 1.0f;
	m_aFloats[ FLOAT_SHAKE_AMOUNT ]     = 1.0f;
	m_aFloats[ FLOAT_SHAKE_INTERVAL ]   = 0.1f;
	m_aFloats[ FLOAT_SHAKE_DURATION ]   = 0.5f;
	m_aFloats[ FLOAT_GAME_OVER_UPTIME ] = 2.0f;

	TTRB oTRB;
	oTRB.Load( "Data\\MinigameData\\WhackARac\\whackarac.trb" );

	m_iNumLevels = *oTRB.CastSymbol<TINT>( "LevelCount" );

	TCHAR szSymbol[ 1024 ];

	for ( TINT i = 1; i - 1 < m_iNumLevels; i++ )
	{
		Level& rLevel = m_aLevels[ i - 1 ];

		TStringManager::String8Format( szSymbol, sizeof( szSymbol ), "Level%i", i );
		TUINT8* pLevelData = oTRB.CastSymbol<TUINT8>( szSymbol );
		TUtil::MemCopy( &rLevel, pLevelData, 0x20 );

		TStringManager::String8Format( szSymbol, sizeof( szSymbol ), "LevelFormCount%i", i );
		TINT* pFormCount = oTRB.CastSymbol<TINT>( szSymbol );
		rLevel.iNumForms = *pFormCount;
		rLevel.pForms    = TSTATICCAST( Form, TMalloc( sizeof( Form ) * *pFormCount ) );
		TUtil::MemCopy( rLevel.pForms, pLevelData + 0x20, sizeof( Form ) * *pFormCount );
	}

	TINT*      pNumFormations = oTRB.CastSymbol<TINT>( "NumFormations" );
	Formation* pFormations    = oTRB.CastSymbol<Formation>( "Formations" );

	for ( TINT i = 0; i < *pNumFormations; i++ )
		TUtil::MemCopy( &m_aFormations[ i ], &pFormations[ i ], sizeof( Formation ) );

	TUtil::MemCopy( m_aInts, oTRB.GetSymbolAddress( "Ints" ), sizeof( m_aInts ) );
	TUtil::MemCopy( m_aFloats, oTRB.GetSymbolAddress( "Floats" ), sizeof( m_aFloats ) );

	oTRB.Close();
}

// $Barnyard: FUNCTION 004d8ca0
void AWhackARaccMiniGameState::RenderRing( TModelInstance* a_pInstance )
{
	TRenderInterface* pRender = TRenderInterface::GetSingleton();
	TMatrix44         matOldLightColour;
	const TBOOL badHit = s_whackView.owner == this ? ( s_whackView.current.flags & 8 ) != 0 : m_bBadHit;

	// The ring turns red while Freddy's hit is shown
	if ( badHit )
	{
		matOldLightColour = pRender->GetLightColour();

		TMatrix44 matRed;
		TUtil::MemSet( &matRed, 0, sizeof( matRed ) );
		matRed.AsBasisVector4( BASISVECTOR_RIGHT ) = TVector4( 1.0f, 0.0f, 0.0f, 1.0f );
		pRender->SetLightColourMatrix( matRed );
	}

	TSkeletonInstance* pSkeletonInstance = a_pInstance->GetSkeletonInstance();
	pSkeletonInstance->UpdateState( TTRUE );
	pRender->GetCurrentContext()->SetSkeletonInstance( pSkeletonInstance );

	TModelLOD& rLOD = a_pInstance->GetModel()->GetLOD( a_pInstance->GetLOD() );

	for ( TINT i = 0; i < rLOD.iNumMeshes; i++ )
		rLOD.ppMeshes[ i ]->Render();

	if ( badHit )
		pRender->SetLightColourMatrix( matOldLightColour );
}

// $Barnyard: FUNCTION 004d8dd0
void AWhackARaccMiniGameState::RenderBadRing( TModelInstance* a_pInstance )
{
	// Only visible while Freddy's hit is shown
	if ( !( s_whackView.owner == this ? ( s_whackView.current.flags & 8 ) != 0 : m_bBadHit ) )
		return;

	TRenderInterface* pRender           = TRenderInterface::GetSingleton();
	TMatrix44         matOldLightColour = pRender->GetLightColour();

	TMatrix44 matRed;
	TUtil::MemSet( &matRed, 0, sizeof( matRed ) );
	matRed.AsBasisVector4( BASISVECTOR_RIGHT ) = TVector4( 1.0f, 0.0f, 0.0f, 1.0f );
	pRender->SetLightColourMatrix( matRed );

	TSkeletonInstance* pSkeletonInstance = a_pInstance->GetSkeletonInstance();
	pSkeletonInstance->UpdateState( TTRUE );
	pRender->GetCurrentContext()->SetSkeletonInstance( pSkeletonInstance );

	TModelLOD& rLOD = a_pInstance->GetModel()->GetLOD( a_pInstance->GetLOD() );

	for ( TINT i = 0; i < rLOD.iNumMeshes; i++ )
		rLOD.ppMeshes[ i ]->Render();

	pRender->SetLightColourMatrix( matOldLightColour );
}

// $Barnyard: FUNCTION 004d8ef0
void AWhackARaccMiniGameState::RenderBadRingCallback( TModelInstance* a_pInstance, void* a_pUserData )
{
	TSTATICCAST( AWhackARaccMiniGameState, a_pUserData )->RenderBadRing( a_pInstance );
}

// $Barnyard: FUNCTION 004d8f10
void AWhackARaccMiniGameState::RenderRingCallback( TModelInstance* a_pInstance, void* a_pUserData )
{
	TSTATICCAST( AWhackARaccMiniGameState, a_pUserData )->RenderRing( a_pInstance );
}

// $Barnyard: FUNCTION 004d8f30
ASoundId AWhackARaccMiniGameState::GetSound()
{
	return 35;
}

// $Barnyard: FUNCTION 004d8f40
void AWhackARaccMiniGameState::ShowMessage( const TWCHAR* a_wszText )
{
	m_pMessage->m_eMode       = AGUI2AnythingPopup::MODE_FADE;
	m_pMessage->m_fPulseSpeed = 1.0f;
	m_pMessage->m_fStartScale = 1.0f;
	m_pMessage->m_fScale      = 1.2f;
	m_pMessage->m_fDuration   = m_aFloats[ FLOAT_MESSAGE_TIME ];
	m_pMessage->SetText( a_wszText, 0xFF77FFFF, &m_GUIElement );
}

// $Barnyard: FUNCTION 004d8fb0
void AWhackARaccMiniGameState::ResetLevel( TBOOL a_bNewGame )
{
	for ( TINT i = 0; i < NUM_BUCKS; i++ )
	{
		m_aBucks[ i ].eState = MOLESTATE_HIDDEN;
		m_aBucks[ i ].vPos   = TVector4::VEC_ZERO;
	}

	for ( TINT i = 0; i < NUM_RACCOONS; i++ )
	{
		m_aRaccoons[ i ].eState = MOLESTATE_HIDDEN;
		m_aRaccoons[ i ].vPos   = TVector4::VEC_ZERO;
	}

	for ( TINT i = 0; i < SPECIAL_NUMOF; i++ )
	{
		m_aSpecials[ i ].eState = MOLESTATE_HIDDEN;
		m_aSpecials[ i ].vPos   = TVector4::VEC_ZERO;
	}

	for ( TINT i = 0; i < NUM_HOLES; i++ )
	{
		m_aHoles[ i ].bOccupied = TFALSE;
		m_aHoles[ i ].iObject   = -1;
		m_aHoles[ i ].eKind     = KIND_RACCOON;
		m_aHoles[ i ].fCooldown = 0.0f;
	}

	m_fWhackCooldown = 0.0f;
	m_fTimeLimit     = m_aLevels[ m_iLevel ].fTime;
	m_iVoiceCue      = -1;
	m_iFreddyCue     = -1;

	for ( TINT i = 0; i < MAX_PLAYERS; i++ )
	{
		Player& rPlayer      = m_aPlayers[ i ];
		rPlayer.iRow         = 1;
		rPlayer.iCol         = 1;
		rPlayer.bPerfect     = TTRUE;
		rPlayer.iHole        = 4;
		rPlayer.fRingPulse   = 0.0f;
		rPlayer.eMalletState = MALLETSTATE_IDLE;

		if ( a_bNewGame == TTRUE )
		{
			rPlayer.iLives          = m_aInts[ INT_LIVES ];
			rPlayer.iNumFreddyHits  = 0;
			rPlayer.iLevelScore     = 0;
			rPlayer.iRaccoonScore   = 0;
			rPlayer.iEggScore       = 0;
			rPlayer.iPeckScore      = 0;
			rPlayer.iCoyoteScore    = 0;
			rPlayer.iNumMisses      = 0;
			rPlayer.iScore          = 0;
			rPlayer.iNumMisses2     = 0;
			rPlayer.iPrevLevelScore = 0;
			rPlayer.iLevel          = s_iStartLevel;
			rPlayer.bFailed         = TFALSE;
		}
	}

	if ( a_bNewGame == TTRUE )
	{
		for ( TINT i = 0; i < NUM_DIALS; i++ )
			m_aDials[ i ].fAngle = 0.0f;
	}
}

// $Barnyard: FUNCTION 004d9330
TBOOL AWhackARaccMiniGameState::ProcessCommand( AInputCommand a_eInputCommand, const TInputInterface::InputEvent* a_pInputEvent, TBOOL& a_rStopEvents )
{
	if ( ACoopMinigame::BlockInput() )
		return AMiniGameState::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );
	if ( !m_oStartHudElement.IsForceHidden() || m_fMessageTimer > 0.0f || IsGameOver() || GetCurrentPlayer().iLives == 0 )
		return AMiniGameState::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );

	if ( FindChildState( &TGetClass( AAnticGameOverState ) ) )
		return AGameState::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );

	TBOOL   bHandled = TFALSE;
	Player& rPlayer  = GetCurrentPlayer();

	switch ( a_eInputCommand )
	{
		case AInputCommand_Move:
			if ( a_pInputEvent->GetSource()->IsA( &TGetClass( TInputDeviceMouse ) ) )
			{
				// Moving the mouse switches to mouse control
				if ( 0.0f != a_pInputEvent->GetMagnitudeFloat( 0 ) && 0.0f != a_pInputEvent->GetMagnitudeFloat( 1 ) )
				{
					m_bMouseControl = TTRUE;
					m_bKeyboardMove = TFALSE;
				}
			}
			else
			{
				bHandled = TTRUE;

				TFLOAT fX = a_pInputEvent->GetMagnitudeFloat( 0 );
				TFLOAT fY = a_pInputEvent->GetMagnitudeFloat( 1 );

				if ( TMath::Abs( fX ) > 0.001f || TMath::Abs( fY ) > 0.001f || !m_bMouseControl )
				{
					if ( fX < -STICK_DEADZONE )
						rPlayer.iCol = 0;
					else if ( fX > STICK_DEADZONE )
						rPlayer.iCol = 2;
					else if ( !s_bColHeld )
						rPlayer.iCol = 1;

					if ( fY < -STICK_DEADZONE )
						rPlayer.iRow = 2;
					else if ( fY > STICK_DEADZONE )
						rPlayer.iRow = 0;
					else if ( !s_bRowHeld )
						rPlayer.iRow = 1;

					m_bMouseControl = TFALSE;
					m_bKeyboardMove = TFALSE;
				}
			}
			break;
		case AInputCommand_LeftWithUp:
			// The ring goes back to the middle when the key is released
			if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_UP )
			{
				s_bColHeld = TFALSE;
				rPlayer.iCol += 1;
			}
			else
			{
				s_bColHeld = TTRUE;
				rPlayer.iCol -= 1;
			}

			TMath::Clip( rPlayer.iCol, 0, 2 );
			bHandled        = TTRUE;
			m_bMouseControl = TFALSE;
			m_bKeyboardMove = TTRUE;
			break;
		case AInputCommand_RightWithUp:
			if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_UP )
			{
				s_bColHeld = TFALSE;
				rPlayer.iCol -= 1;
			}
			else
			{
				s_bColHeld = TTRUE;
				rPlayer.iCol += 1;
			}

			TMath::Clip( rPlayer.iCol, 0, 2 );
			bHandled        = TTRUE;
			m_bMouseControl = TFALSE;
			m_bKeyboardMove = TTRUE;
			break;
		case AInputCommand_ForwardWithUp:
			if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_UP )
			{
				s_bRowHeld = TFALSE;
				rPlayer.iRow -= 1;
			}
			else
			{
				s_bRowHeld = TTRUE;
				rPlayer.iRow += 1;
			}

			TMath::Clip( rPlayer.iRow, 0, 2 );
			bHandled        = TTRUE;
			m_bMouseControl = TFALSE;
			break;
		case AInputCommand_BackwardWithUp:
			if ( a_pInputEvent->GetEventType() == TInputInterface::EVENT_TYPE_GONE_UP )
			{
				s_bRowHeld = TFALSE;
				rPlayer.iRow += 1;
			}
			else
			{
				s_bRowHeld = TTRUE;
				rPlayer.iRow -= 1;
			}

			TMath::Clip( rPlayer.iRow, 0, 2 );
			bHandled        = TTRUE;
			m_bMouseControl = TFALSE;
			m_bKeyboardMove = TTRUE;
			break;
		case AInputCommand_Shoot:
			if ( a_pInputEvent->GetEventType() != TInputInterface::EVENT_TYPE_GONE_DOWN || m_bBadHit )
				break;

			bHandled = TTRUE;

			if ( m_aHoles[ rPlayer.iHole ].fCooldown <= 0.0f && m_fWhackCooldown <= 0.0f )
			{
				// Swing the mallet at the hole under the ring
				m_pMallet->FinishAnimation( m_aHoleRecAnims[ rPlayer.iMalletHole ] );
				m_pMallet->FinishAnimation( m_aHoleImpAnims[ rPlayer.iMalletHole ] );
				m_pMallet->FinishAnimation( m_aHoleRstAnims[ rPlayer.iMalletHole ] );
				m_pMallet->FinishAnimation( TPS8( reset ) );
				m_pMallet->FinishAnimation( TPS8( idle ) );
				m_pMallet->FinishAnimation( TPS8( impact ) );

				rPlayer.eMalletState                = MALLETSTATE_SWING;
				rPlayer.iMalletHole                 = rPlayer.iHole;
				m_aHoles[ rPlayer.iHole ].fCooldown = m_aFloats[ FLOAT_HOLE_COOLDOWN ];
				m_fWhackCooldown                    = m_aFloats[ FLOAT_WHACK_COOLDOWN ];
			}
			break;
	}

	rPlayer.iHole = rPlayer.iCol + rPlayer.iRow * NUM_ROWS;

	if ( !bHandled )
		return AMiniGameState::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );

	return bHandled;
}

// $Barnyard: FUNCTION 004d98c0
void AWhackARaccMiniGameState::OnIntroCueReset( TINT a_iType, TINT a_iCueIndex, void* a_pUserData )
{
	TSTATICCAST( AWhackARaccMiniGameState, a_pUserData )->m_iIntroCue = -1;
}

// $Barnyard: FUNCTION 004d98e0
void AWhackARaccMiniGameState::OnRemoval()
{
	if ( s_whackView.owner == this ) SetSpectatorMode( TFALSE );
	m_pLifeHud->Destroy();

	if ( m_pMessage )
		delete m_pMessage;

	if ( m_pLifeHud )
		delete m_pLifeHud;

	m_oPortrait.Destroy();
	TRenderInterface::GetSingleton()->BeginEndSceneHAL();

	for ( TINT i = 0; i < m_iNumLevels; i++ )
		TFree( m_aLevels[ i ].pForms );

	AAnimatableObjectManager* pAnimObjManager = AAnimatableObjectManager::GetSingleton();
	pAnimObjManager->DestroyAnimatableObject( m_pMallet );
	pAnimObjManager->DestroyAnimatableObject( m_pRing );
	pAnimObjManager->DestroyAnimatableObject( m_pBadRing );
	pAnimObjManager->DestroyAnimatableObject( m_pArrow );

	for ( TINT i = 0; i < NUM_BUCKS; i++ )
		pAnimObjManager->DestroyAnimatableObject( m_aBucks[ i ].pObject );

	for ( TINT i = 0; i < NUM_RACCOONS; i++ )
		pAnimObjManager->DestroyAnimatableObject( m_aRaccoons[ i ].pObject );

	for ( TINT i = 0; i < SPECIAL_NUMOF; i++ )
		pAnimObjManager->DestroyAnimatableObject( m_aSpecials[ i ].pObject );

	for ( TINT i = 0; i < NUM_DIALS; i++ )
		pAnimObjManager->DestroyAnimatableObject( m_aDials[ i ].pObject );

	m_oScoreFloats.Destroy();
	AGameTimeFXManager::GetSingleton()->ClearForcedFX();
	AGameLoader::DestroyLevel();
	AUnitManager::GetSingleton()->RemoveAllUnits();
	AUnitManager::GetSingleton()->DeleteDyingUnits();
	TRenderInterface::GetSingleton()->FlushDyingResources();

	ASoundManager::GetSingleton()->RemoveCueCallback( m_iIntroCue, 1, OnIntroCueReset );

	// Turn the sky and the sun flare back on
	ASkyDome* pSkyDome = ARenderer::GetSingleton()->GetSkyDome();
	pSkyDome->SetFlags( pSkyDome->GetFlags() | ASkyDome::FLAGS_RENDER );
	pSkyDome->SetFlags( ( pSkyDome->GetFlags() & ~( ASkyDome::FLAGS_FROZEN | ASkyDome::FLAGS_ROTATE ) ) | ASkyDome::FLAGS_ROTATE );
	ASunFlareEffect::GetSingleton()->SetEnabled( TTRUE );

	AMiniGameState::OnRemoval();
}

// NOTE: inlined in the original (00004d9ab0, 004da480, 004dd470, 004ddc00, 004ddf50)
void AWhackARaccMiniGameState::HideHud()
{
	m_pMessage->SetText( TNULL, 0x55000000, TNULL );
	m_oTimeLeftHudElement.SetForceHide( TTRUE );
	m_pLifeHud->SetVisible( TFALSE );
	m_oPortrait.Hide();
	m_oScoreFloats.Hide();
	m_oPickUpHudElement.SetForceHide( TTRUE );
}

// NOTE: inlined in the original (004dd470)
void AWhackARaccMiniGameState::ShowHud()
{
	m_oTimeLeftHudElement.SetForceHide( TFALSE );
	m_pLifeHud->SetVisible( TTRUE );
	m_oPortrait.Show();
	m_oScoreFloats.Show();
	m_oPickUpHudElement.SetForceHide( TFALSE );
}

// NOTE: inlined in the original (004da480, 004ddc00, 004ddf50)
void AWhackARaccMiniGameState::ResetDials()
{
	for ( TINT i = 0; i < NUM_DIALS; i++ )
	{
		m_aDials[ i ].fAngle = 0.0f;
		m_aDials[ i ].pObject->GetModelInstance()->SetTranslation( m_aDials[ i ].vPos.AsVector3() );
		m_aDials[ i ].pObject->GetModelInstance()->SetDirection( s_vDialDir );
	}
}

// $Barnyard: FUNCTION 004d9ab0
void AWhackARaccMiniGameState::EndGame()
{
	m_bGameEnding = TFALSE;
	m_bWon        = TFALSE;

	ASoundManager::GetSingleton()->StopCue( m_iUnkCue );
	m_iUnkCue = -1;
	ASoundManager::GetSingleton()->StopCue( m_iLoopCue );
	m_iLoopCue = -1;

	ShowGameOver();
	m_bTurnEnded      = TTRUE;
	m_bRestartPending = TTRUE;

	m_oTimeLeftHudElement.SetForceHide( TTRUE );
	m_oPickUpHudElement.SetNumber( 0 );
	HideHud();
}

// $Barnyard: FUNCTION 004d9bd0
void AWhackARaccMiniGameState::RemoveBuck( Mole* a_pBuck )
{
	a_pBuck->pObject->SetSkeletonUpdating( TFALSE, TTRUE );
	a_pBuck->eBuckState = BUCKSTATE_NONE;
	a_pBuck->eState     = MOLESTATE_HIDDEN;

	if ( a_pBuck->iHole != -1 )
	{
		m_aHoles[ a_pBuck->iHole ].bOccupied = TFALSE;
		m_aHoles[ a_pBuck->iHole ].iObject   = -1;
	}
}

// $Barnyard: FUNCTION 004d9c20
void AWhackARaccMiniGameState::OnActivate()
{
	AGUI2::GetSingleton()->SetCursorVisible( TTRUE );
	m_pMessage->ShowIfHasText();

	GetCurrentPlayer().iRow = 1;
	GetCurrentPlayer().iCol = 1;

	m_oLevelText.Show();
	AMiniGameState::OnActivate();
}

// $Barnyard: FUNCTION 004d9c70
void AWhackARaccMiniGameState::OnDeactivate()
{
	AGUI2::GetSingleton()->SetCursorVisible( TFALSE );
	m_pMessage->Hide();
	m_oLevelText.Hide();
	AMiniGameState::OnDeactivate();
}

// $Barnyard: FUNCTION 004d9ca0
TBOOL AWhackARaccMiniGameState::IsGameOver()
{
	AGUI2Timer& rTimer = m_oTimeLeftHudElement.GetTimer();

	// Wait for the last tick of the clock
	if ( !rTimer.IsTickComplete() && rTimer.IsTimerFull() )
		return TFALSE;

	return m_bGameEnding || m_bTurnEnded || ( GetCurrentPlayer().iLives == 0 && !ASoundManager::GetSingleton()->IsCueUsed( m_iVoiceCue ) );
}

// $Barnyard: FUNCTION 004d9d20
void AWhackARaccMiniGameState::OnPause()
{
	m_iIntroCue = ASoundManager::GetSingleton()->PlayCue( SOUND_INTRO );
	m_iCrowdCue = ASoundManager::GetSingleton()->PlayCue( SOUND_CROWD );
	ASoundManager::GetSingleton()->AddCueCallback( m_iIntroCue, 1, OnIntroCueReset, this );

	m_pMallet->SetSkeletonUpdating( TTRUE, TTRUE );
	m_pRing->SetSkeletonUpdating( TTRUE, TTRUE );

	ACamera* pCamera                      = ACameraManager::GetSingleton()->GetCurrentCamera();
	pCamera->GetMatrix().GetTranslation() = ATerrainInterface::GetSingleton()->GetLocatorPos( "camera" );
	pCamera->LookAtPoint( ATerrainInterface::GetSingleton()->GetLocatorPos( "camera_target" ) );

	AGUI2::GetSingleton()->SetCursorVisible( TTRUE );
	AGameStateController::GetSingleton()->SetFlags( AGameStateController::FLAGS_FADING_IN );
	AGameState::OnPause();
}

// $Barnyard: FUNCTION 004d9e10
void AWhackARaccMiniGameState::PlayLoseSequence()
{
	AGameStateController::GetSingleton()->PushState(
	    new ASSeqState( "sswar_lose", "sswar_lose", "SSwar_CameraB", TVector4::VEC_ZERO, 1, TTRUE, TTRUE, TTRUE, TFALSE, TFALSE, TFALSE, TTRUE, TFALSE )
	);
}

// $Barnyard: FUNCTION 004d9ea0
void AWhackARaccMiniGameState::PlayWinSequence()
{
	AGameStateController::GetSingleton()->PushState(
	    new ASSeqState( "sswar_win", "sswar_win", "SSwar_CameraB", TVector4::VEC_ZERO, 1, TTRUE, TTRUE, TTRUE, TFALSE, TFALSE, TFALSE, TTRUE, TFALSE )
	);
}

// $Barnyard: FUNCTION 004d9f30
void AWhackARaccMiniGameState::Pause()
{
	AGameStateController::GetSingleton()->PushState(
	    new ASSeqState( "sswar_intro", "sswar_intro", "SSwar_CameraB", TVector4::VEC_ZERO, 1, TTRUE, TTRUE, TTRUE, TFALSE, TFALSE, TFALSE, TTRUE, TFALSE )
	);
}

// $Barnyard: FUNCTION 004d9fc0
void AWhackARaccMiniGameState::OnPreStart()
{
	m_pMallet->SetSkeletonUpdating( TFALSE, TTRUE );
	m_pRing->SetSkeletonUpdating( TFALSE, TTRUE );
	m_pBadRing->SetSkeletonUpdating( TFALSE, TTRUE );

	for ( TINT i = 0; i < NUM_BUCKS; i++ )
		m_aBucks[ i ].pObject->SetSkeletonUpdating( TFALSE, TTRUE );

	for ( TINT i = 0; i < NUM_RACCOONS; i++ )
		m_aRaccoons[ i ].pObject->SetSkeletonUpdating( TFALSE, TTRUE );

	for ( TINT i = 0; i < SPECIAL_NUMOF; i++ )
		m_aSpecials[ i ].pObject->SetSkeletonUpdating( TFALSE, TTRUE );

	AGUI2::GetSingleton()->SetCursorVisible( TFALSE );
}

// $Barnyard: FUNCTION 004da070
void AWhackARaccMiniGameState::OnObjectCollected( AAnimatableObject* a_pObject )
{
	// A barn buck has flown into the pick up HUD
	Mole* pBuck = TNULL;

	for ( TINT i = 0; i < NUM_BUCKS; i++ )
	{
		if ( m_aBucks[ i ].pObject == a_pObject )
		{
			pBuck = &m_aBucks[ i ];
			break;
		}
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	// NOTE: the original doesn't check if the object is a barn buck
	if ( !pBuck )
		return;
#endif

	RemoveBuck( pBuck );
}

// $Barnyard: FUNCTION 004da0e0
AMiniGameState::S1* AWhackARaccMiniGameState::GetResults()
{
	TINT iNumPlayers = APlayerManager::GetSingleton()->GetCurrentPlayerSet()->GetHumanPlayerCount();

	for ( TINT i = 0; i < MAX_PLAYERS; i++ )
		m_oResults.oPlayerList[ i ].bUnk1 = TFALSE;

	if ( iNumPlayers <= 1 )
	{
		m_oResults.oPlayerList[ 0 ].bUnk1 = TTRUE;
	}
	else
	{
		for ( TINT i = 0; i < iNumPlayers; i++ )
			m_oResults.oPlayerList[ i ].bUnk1 = TTRUE;
	}

	m_oResults.bWon = m_bWon;

	TINT        iPlayer = APlayerManager::GetSingleton()->GetCurrentPlayerIterator();
	PlayerInfo& rInfo   = m_oResults.oPlayerList[ iPlayer ];

	while ( rInfo.vecRows.Size() != 0 )
	{
		rInfo.vecRows[ rInfo.vecRows.Size() - 1 ] = ResultRow();
		rInfo.vecRows.PopBack();
	}

	Player& rPlayer = m_aPlayers[ iPlayer ];
	rInfo.vecRows.PushBack( ResultRow{ ALocaleManager::Interface()->GetString( LOCALE_RACCOONS ), rPlayer.iRaccoonScore } );
	rInfo.vecRows.PushBack( ResultRow{ ALocaleManager::Interface()->GetString( LOCALE_COYOTES ), rPlayer.iCoyoteScore } );
	rInfo.vecRows.PushBack( ResultRow{ ALocaleManager::Interface()->GetString( LOCALE_EGGS ), rPlayer.iEggScore } );
	rInfo.vecRows.PushBack( ResultRow{ ALocaleManager::Interface()->GetString( LOCALE_PECKS ), rPlayer.iPeckScore } );
	m_oResults.ComputeTotal( iPlayer );

	if ( m_bSubmitHighScore )
	{
		m_bSubmitHighScore = TFALSE;

		if ( AMiniGameManager::GetSingleton()->GetMiniGameVariant() == 0 )
			AHighScoreManager::AddScore( HIGHSCORE_TABLE, TFLOAT( m_oResults.oPlayerList[ 0 ].iTotal ), m_oResults.bWon, AHighScoreManager::PLAYER_1, TFALSE );
	}

	return &m_oResults;
}

// $Barnyard: FUNCTION 004da480
void AWhackARaccMiniGameState::StartTurn( TBOOL a_bNewGame )
{
	if ( ACoopMinigame::IsPlaying() )
		s_bColHeld = s_bRowHeld = TFALSE;
	for ( TINT i = 0; i < NUM_BUCKS; i++ )
		m_aBucks[ i ].pObject->SetSkeletonUpdating( TFALSE, TTRUE );

	for ( TINT i = 0; i < NUM_RACCOONS; i++ )
		m_aRaccoons[ i ].pObject->SetSkeletonUpdating( TFALSE, TTRUE );

	for ( TINT i = 0; i < SPECIAL_NUMOF; i++ )
		m_aSpecials[ i ].pObject->SetSkeletonUpdating( TFALSE, TTRUE );

	if ( a_bNewGame == TTRUE )
		ResetDials();

	for ( TINT i = 0; i < m_iNumLevels; i++ )
	{
		for ( TINT k = 0; k < m_aLevels[ i ].iNumForms; k++ )
			m_aLevels[ i ].pForms[ k ].bTriggered = TFALSE;
	}

	ResetLevel( a_bNewGame );

	if ( m_fMessageTimer < 0.0f )
	{
		APlayerManager* pPlayerManager = APlayerManager::GetSingleton();
		m_oStartHudElement.Setup( pPlayerManager->GetCurrentPlayerSet()->GetHumanPlayerCount(), m_iCurrentPlayer );
		HideHud();
		m_bShowLevelText = TTRUE;
	}

	m_oTimeLeftHudElement.Setup( m_aLevels[ GetCurrentPlayer().iLevel ].fTime );
	m_pLifeHud->SetLives( GetCurrentPlayer().iLives );
	m_bSubmitHighScore = TTRUE;
}

// $Barnyard: FUNCTION 004da6f0
void AWhackARaccMiniGameState::UpdateSpecials( TFLOAT a_fDeltaTime )
{
	for ( TINT i = 0; i < SPECIAL_NUMOF; i++ )
	{
		Mole& rSpecial = m_aSpecials[ i ];
		TBOOL bFree    = TFALSE;

		switch ( rSpecial.eState )
		{
			case MOLESTATE_HIT:
				if ( !rSpecial.pObject->HasAnimation( TPS8( hit ) ) || rSpecial.pObject->IsAnimationFinished( TPS8( hit ) ) == TTRUE )
					rSpecial.eState = MOLESTATE_FREE;
				break;
			case MOLESTATE_FREE:
				bFree = TTRUE;
				break;
			case MOLESTATE_RISING:
				if ( !rSpecial.pObject->HasAnimation( TPS8( up ) ) || rSpecial.pObject->IsAnimationFinished( TPS8( up ) ) == TTRUE )
				{
					rSpecial.eState = MOLESTATE_UP;

					if ( rSpecial.pObject->HasAnimation( TPS8( idle_1 ) ) )
						rSpecial.pObject->PlayAnimation( TPS8( idle_1 ) );

					if ( i == SPECIAL_FREDDY )
						m_iFreddyCue = ASoundManager::GetSingleton()->PlayCue( SOUND_FREDDY_UP );
				}
				break;
			case MOLESTATE_UP:
				rSpecial.fTimer -= a_fDeltaTime;

				if ( rSpecial.fTimer <= 0.0f )
				{
					rSpecial.eState = MOLESTATE_HIDING;

					if ( rSpecial.pObject->HasAnimation( TPS8( down ) ) )
						rSpecial.pObject->PlayAnimation( TPS8( down ) );
				}
				break;
			case MOLESTATE_HIDING:
				if ( rSpecial.pObject->HasAnimation( TPS8( down ) ) && rSpecial.pObject->IsAnimationFinished( TPS8( down ) ) != TTRUE )
					break;

				// It got away, no perfect level if it's worth points
				switch ( i )
				{
					case SPECIAL_FREDDY:
						if ( m_aInts[ INT_FREDDY_SCORE ] > 0 ) GetCurrentPlayer().bPerfect = TFALSE;
						break;
					case SPECIAL_PECK:
						if ( m_aInts[ INT_PECK_SCORE ] > 0 ) GetCurrentPlayer().bPerfect = TFALSE;
						break;
					case SPECIAL_EGG:
						if ( m_aInts[ INT_EGG_SCORE ] > 0 ) GetCurrentPlayer().bPerfect = TFALSE;
						break;
					case SPECIAL_COYOTE:
						if ( m_aInts[ INT_COYOTE_SCORE ] > 0 ) GetCurrentPlayer().bPerfect = TFALSE;
						break;
				}

				bFree = TTRUE;
				break;
		}

		if ( bFree )
		{
			rSpecial.eState                      = MOLESTATE_HIDDEN;
			m_aHoles[ rSpecial.iHole ].bOccupied = TFALSE;
			m_aHoles[ rSpecial.iHole ].iObject   = -1;
		}

		rSpecial.pObject->GetModelInstance()->SetTranslation( rSpecial.vPos.AsVector3() );
		rSpecial.pObject->SetSkeletonUpdating( rSpecial.eState != MOLESTATE_HIDDEN, TTRUE );
	}
}

// $Barnyard: FUNCTION 004da930
void AWhackARaccMiniGameState::Whack( TINT a_iPlayer )
{
	Player& rPlayer = m_aPlayers[ a_iPlayer ];
	Hole&   rHole   = m_aHoles[ rPlayer.iMalletHole ];

	if ( !rHole.bOccupied )
	{
		// Missed
		rPlayer.iNumMisses2++;
		rPlayer.iNumMisses++;
		ASoundManager::GetSingleton()->PlayCue( SOUND_MISS );
		AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_MISS ], rHole.vPos, s_vEffectDir );
		return;
	}

	TFLOAT fRumbleStrength = 0.5f;
	TINT   iScore          = 0;

	auto IsUp = []( const Mole& a_rMole ) {
		return a_rMole.eState != MOLESTATE_HIDDEN && a_rMole.eState != MOLESTATE_HIT && a_rMole.eState != MOLESTATE_FREE;
	};

	if ( rHole.eKind == KIND_RACCOON && IsUp( m_aRaccoons[ rHole.iObject ] ) )
	{
		Mole& rRaccoon = m_aRaccoons[ rHole.iObject ];

		ASoundManager::GetSingleton()->PlayCue( SOUND_HIT );
		AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_PUFF ], rHole.vPos, s_vEffectDir );
		AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_DUST ], rHole.vPos, s_vEffectDir );

		rPlayer.iLevelScore += m_aInts[ INT_RACCOON_SCORE ];
		rPlayer.iScore += m_aInts[ INT_RACCOON_SCORE ];
		rPlayer.iRaccoonScore += m_aInts[ INT_RACCOON_SCORE ];
		rRaccoon.eState = MOLESTATE_HIT;

		if ( rRaccoon.pObject->IsAnimPlaying( TPS8( idle_1 ), 0.0f ) == TTRUE )
			rRaccoon.pObject->FinishAnimation( TPS8( idle_1 ) );

		rRaccoon.pObject->PlayAnimation( TPS8( hit ) );
	}
	else if ( rHole.eKind == KIND_BUCK && IsUp( m_aBucks[ rHole.iObject ] ) )
	{
		Mole& rBuck = m_aBucks[ rHole.iObject ];

		ASoundManager::GetSingleton()->PlayCue( SOUND_HIT );
		AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_PUFF ], rHole.vPos, s_vEffectDir );
		AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_DUST ], rHole.vPos, s_vEffectDir );

		// The buck flies into the pick up HUD (see OnObjectCollected)
		rBuck.eState                      = MOLESTATE_HIT;
		m_aHoles[ rBuck.iHole ].bOccupied = TFALSE;
		m_aHoles[ rBuck.iHole ].iObject   = -1;
		StartCollectEffect( rBuck.pObject, 1, 30.0f );
	}
	else if ( rHole.eKind == KIND_SPECIAL && IsUp( m_aSpecials[ rHole.iObject ] ) )
	{
		m_aSpecials[ rHole.iObject ].eState = MOLESTATE_HIT;
		TINT iOldLevelScore                 = rPlayer.iLevelScore;

		switch ( rHole.iObject )
		{
			case SPECIAL_FREDDY:
			{
				ASoundManager::GetSingleton()->PlayCue( SOUND_HIT );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_PUFF ], rHole.vPos, s_vEffectDir );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_BAD ], rHole.vPos, s_vEffectDir );

				ASoundId eVoice = -1;

				switch ( rPlayer.iNumFreddyHits++ )
				{
					case 0: eVoice = SOUND_FREDDY_HIT_1; break;
					case 1: eVoice = SOUND_FREDDY_HIT_2; break;
					case 2: eVoice = SOUND_FREDDY_HIT_3; break;
				}

				ASoundManager::GetSingleton()->StopCue( m_iFreddyCue );
				m_iVoiceCue = ASoundManager::GetSingleton()->PlayCue( eVoice );

				// Lose a life, shake the camera and show the red ring
				rPlayer.iLives--;
				m_pLifeHud->AddLives( -1 );
				m_fShakeTimeLeft = m_aFloats[ FLOAT_SHAKE_DURATION ];
				m_fShakeTimer    = m_aFloats[ FLOAT_SHAKE_INTERVAL ];
				m_fBadRingTime   = m_aFloats[ FLOAT_BAD_RING_TIME ];
				m_bBadHit        = TTRUE;

				rPlayer.iRow        = 1;
				rPlayer.iCol        = 1;
				rPlayer.fRingPulse  = 0.0f;
				rPlayer.vBadRingPos = rHole.vPos;
				fRumbleStrength     = 2.0f;
				rPlayer.vBadRingPos.y -= 0.05f;

				m_pBadRing->SetSkeletonUpdating( TTRUE, TTRUE );
				m_pBadRing->SetOpacity( 1.0f, TTRUE );

				TMatrix44 matTransform;
				MakeFlatTransform( matTransform, GetCurrentPlayer().vBadRingPos, TMath::HALF_PI );
				m_pBadRing->GetModelInstance()->SetTransform( matTransform );
				break;
			}
			case SPECIAL_PECK:
				ASoundManager::GetSingleton()->PlayCue( SOUND_HIT_PECK );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_PUFF ], rHole.vPos, s_vEffectDir );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_BAD ], rHole.vPos, s_vEffectDir );

				rPlayer.iLevelScore += m_aInts[ INT_PECK_SCORE ];
				rPlayer.iScore += m_aInts[ INT_PECK_SCORE ];
				rPlayer.iPeckScore += m_aInts[ INT_PECK_SCORE ];
				iScore = m_aInts[ INT_PECK_SCORE ];
				break;
			case SPECIAL_EGG:
				ASoundManager::GetSingleton()->PlayCue( SOUND_HIT_EGG );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_EGGSPLAT ], rHole.vPos, s_vEffectDir );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_EGGSHELL ], rHole.vPos, s_vEffectDir );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_EGGYOLK ], rHole.vPos, s_vEffectDir );

				rPlayer.iLevelScore += m_aInts[ INT_EGG_SCORE ];
				rPlayer.iScore += m_aInts[ INT_EGG_SCORE ];
				rPlayer.iEggScore += m_aInts[ INT_EGG_SCORE ];
				iScore = m_aInts[ INT_EGG_SCORE ];
				break;
			case SPECIAL_COYOTE:
				ASoundManager::GetSingleton()->PlayCue( SOUND_HIT_COYOTE );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_PUFF ], rHole.vPos, s_vEffectDir );
				AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_DUST ], rHole.vPos, s_vEffectDir );

				rPlayer.iLevelScore += m_aInts[ INT_COYOTE_SCORE ];
				rPlayer.iScore += m_aInts[ INT_COYOTE_SCORE ];
				rPlayer.iCoyoteScore += m_aInts[ INT_COYOTE_SCORE ];
				iScore = m_aInts[ INT_COYOTE_SCORE ];
				break;
		}

		if ( rPlayer.iLevelScore < iOldLevelScore )
			rPlayer.bPerfect = TFALSE;

		if ( iScore != 0 )
		{
			// Show the points rising from the ring
			TUINT32 uiColour = PackColour( 1.0f, 0.0f, 0.0f, 0.8f );
			TWCHAR* wszFloat = m_awszFloats[ m_iFloatIndex ];

			if ( iScore > 0 )
			{
				TStringManager::String16Format( wszFloat, 128, L"+%i", iScore );
				uiColour = PackColour( 0.13333334f, 0.33333334f, 0.86666667f, 0.8f );
			}
			else
			{
				TStringManager::String16Format( wszFloat, 128, L"%i", iScore );
			}

			TVector4 vRingPos;
			m_pRing->GetModelInstance()->GetPosition( vRingPos );

			// NOTE: the original passes a 7th argument (0) that AddTimedFloat doesn't use
			m_oScoreFloats.AddTimedFloat( wszFloat, uiColour, vRingPos, 1.0f, -0.1f, 1.0f );
			m_iFloatIndex = ( m_iFloatIndex + 1 ) % NUM_FLOATS;
		}

		if ( rPlayer.iLevelScore < 0 )
		{
			rPlayer.iScore      = 0;
			rPlayer.iLevelScore = 0;
		}
	}
	else
	{
		// The hole's object isn't up
		ASoundManager::GetSingleton()->PlayCue( SOUND_MISS );
		AParticleEffectManager::GetSingleton()->PlayEffect( m_aEffects[ EFFECT_MISS ], rHole.vPos, s_vEffectDir );
		return;
	}

	rPlayer.fRingPulse = TMath::HALF_PI;

	AInputHandler::RumbleParams oRumble = { { 1.0f, 0.0f, 0.0f, fRumbleStrength, 0.0f, 1.0f, 1.0f }, 2 };
	AInputHandler::GetSingleton()->SetRumble( AInputHandler::GetSingleton()->GetMainController(), &oRumble );
}

// $Barnyard: FUNCTION 004db0b0
void AWhackARaccMiniGameState::UpdateDials( TFLOAT a_fDeltaTime )
{
	m_oPortrait.SetScore( GetCurrentPlayer().iScore );

	TINT iDivisor = 10;

	for ( TINT i = 0; i < NUM_DIALS; i++ )
	{
		Dial& rDial = m_aDials[ i ];

		// The dials show the digits of the level score
		TINT   iDigit       = GetCurrentPlayer().iLevelScore % iDivisor / ( iDivisor / 10 );
		TFLOAT fTargetAngle = TFLOAT( iDigit ) * TMath::PI * 0.2f;

		if ( iDigit == 0 && rDial.fAngle > 0.0f )
		{
			// From 9 to 0
			rDial.fAngle = TMath::PI * -0.2f;
		}
		else if ( rDial.fAngle < fTargetAngle )
		{
			rDial.fAngle += ( 4.0f - TFLOAT( i ) ) * a_fDeltaTime;

			if ( rDial.fAngle > fTargetAngle )
			{
				rDial.fAngle = fTargetAngle;
				ASoundManager::GetSingleton()->PlayCue( SOUND_DIAL );
			}
		}
		else if ( rDial.fAngle > fTargetAngle )
		{
			rDial.fAngle -= ( 4.0f - TFLOAT( i ) ) * a_fDeltaTime;

			if ( rDial.fAngle < fTargetAngle )
			{
				rDial.fAngle = fTargetAngle;
				ASoundManager::GetSingleton()->PlayCue( SOUND_DIAL );
			}
		}

		TMatrix44 matTransform;
		MakeFlatTransform( matTransform, rDial.vPos, TMath::HALF_PI );
		matTransform.RotateY( -rDial.fAngle );
		rDial.pObject->GetModelInstance()->SetTransform( matTransform );

		iDivisor *= 10;
	}
}

// $Barnyard: FUNCTION 004db2b0
void AWhackARaccMiniGameState::UpdateMallet( TFLOAT a_fDeltaTime )
{
	Player& rPlayer = GetCurrentPlayer();

	TPString8 strImpact  = m_aHoleImpAnims[ rPlayer.iMalletHole ];
	TPString8 strRecover = m_aHoleRecAnims[ rPlayer.iMalletHole ];
	TPString8 strReset   = m_aHoleRstAnims[ rPlayer.iMalletHole ];

	switch ( rPlayer.eMalletState )
	{
		case MALLETSTATE_IDLE:
			if ( !m_pMallet->IsAnimPlaying( TPS8( idle ), 0.0f ) )
				m_pMallet->PlayAnimation( TPS8( idle ) );
			break;
		case MALLETSTATE_SWING:
			if ( m_pMallet->IsAnimPlaying( TPS8( idle ), 0.0f ) == TTRUE )
				m_pMallet->FinishAnimation( TPS8( idle ) );

			m_pMallet->PlayAnimation( TPS8( impact ) );
			m_pMallet->PlayAnimation( strImpact );
			rPlayer.eMalletState = MALLETSTATE_IMPACT;
			break;
		case MALLETSTATE_IMPACT:
			if ( m_pMallet->IsAnimationFinished( strImpact ) == TTRUE )
				rPlayer.eMalletState = MALLETSTATE_IMPACT_DONE;
			break;
		case MALLETSTATE_IMPACT_DONE:
			rPlayer.eMalletState = MALLETSTATE_RECOVER;
			break;
		case MALLETSTATE_RECOVER:
			if ( m_pMallet->IsAnimationFinished( strRecover ) == TTRUE )
				rPlayer.eMalletState = MALLETSTATE_RECOVER_DONE;
			break;
		case MALLETSTATE_RECOVER_DONE:
			rPlayer.eMalletState = MALLETSTATE_RESET;
			break;
		case MALLETSTATE_RESET:
			if ( m_pMallet->IsAnimationFinished( strReset ) == TTRUE )
				rPlayer.eMalletState = MALLETSTATE_IDLE;
			break;
	}

	// The ring over the hole under the cursor pulses after a hit
	TFLOAT   fPulse = TMath::Sin( rPlayer.fRingPulse );
	TVector4 vPos   = m_aHoles[ rPlayer.iHole ].vPos;
	vPos.y -= 0.01f;

	TMatrix44 matTransform;
	MakeFlatTransform( matTransform, vPos, TMath::HALF_PI );

	TFLOAT fScale = fPulse * 0.2f + 1.0f;
	matTransform.Scale( fScale, fScale, fScale );
	m_pRing->GetModelInstance()->SetTransform( matTransform );
}

// $Barnyard: FUNCTION 004db600
void AWhackARaccMiniGameState::UpdateRaccoons( TFLOAT a_fDeltaTime )
{
	for ( TINT i = 0; i < NUM_RACCOONS; i++ )
	{
		Mole& rRaccoon = m_aRaccoons[ i ];

		switch ( rRaccoon.eState )
		{
			case MOLESTATE_HIT:
				if ( rRaccoon.pObject->IsAnimationFinished( TPS8( hit ) ) == TTRUE )
					rRaccoon.eState = MOLESTATE_FREE;
				break;
			case MOLESTATE_FREE:
				rRaccoon.eState                      = MOLESTATE_HIDDEN;
				m_aHoles[ rRaccoon.iHole ].bOccupied = TFALSE;
				m_aHoles[ rRaccoon.iHole ].iObject   = -1;
				break;
			case MOLESTATE_RISING:
				if ( rRaccoon.pObject->GetModelInstance()->IsUpdatingSkeleton() && rRaccoon.pObject->IsAnimationFinished( TPS8( up ) ) == TTRUE )
				{
					rRaccoon.eState = MOLESTATE_UP;
					rRaccoon.pObject->PlayAnimation( TPS8( idle_1 ) );
				}
				break;
			case MOLESTATE_UP:
				rRaccoon.fTimer -= a_fDeltaTime;

				if ( rRaccoon.fTimer <= 0.0f )
				{
					rRaccoon.eState = MOLESTATE_HIDING;
					rRaccoon.pObject->PlayAnimation( TPS8( down ) );
				}
				break;
			case MOLESTATE_HIDING:
				if ( rRaccoon.pObject->IsAnimationFinished( TPS8( down ) ) == TTRUE )
				{
					// It got away
					rRaccoon.eState                      = MOLESTATE_HIDDEN;
					m_aHoles[ rRaccoon.iHole ].bOccupied = TFALSE;
					m_aHoles[ rRaccoon.iHole ].iObject   = -1;
					GetCurrentPlayer().bPerfect          = TFALSE;
				}
				break;
		}

		rRaccoon.pObject->GetModelInstance()->SetTranslation( rRaccoon.vPos.AsVector3() );
		rRaccoon.pObject->SetSkeletonUpdating( rRaccoon.eState != MOLESTATE_HIDDEN, TTRUE );
	}
}

// $Barnyard: FUNCTION 004db830
void AWhackARaccMiniGameState::LaunchBuck( Mole* a_pBuck )
{
	a_pBuck->pObject->SetSkeletonUpdating( TTRUE, TTRUE );

	// NOTE: the original gets the local transform and then overwrites it
	TMatrix44 matTransform;
	a_pBuck->pObject->GetModelInstance()->GetLocalTransform( matTransform );

	matTransform = TMatrix44::IDENTITY;
	matTransform.GetTranslation().Set( a_pBuck->vPos.x, a_pBuck->vPos.y + 0.5f, a_pBuck->vPos.z, a_pBuck->vPos.w );
	matTransform.RotateX( TMath::HALF_PI );
	a_pBuck->pObject->GetModelInstance()->SetTransform( matTransform );

	a_pBuck->vGround = a_pBuck->vPos;
	a_pBuck->vGround.y -= 0.15f;
	a_pBuck->fVelocity  = -2.8f;
	a_pBuck->fFallTime  = 0.0f;
	a_pBuck->eBuckState = BUCKSTATE_FLYING;
	a_pBuck->eState     = MOLESTATE_UP;
	a_pBuck->bLanded    = TFALSE;
	a_pBuck->pObject->GetModelInstance()->SetScale( TVector4( 0.5f, 0.5f, 0.5f, 1.0f ) );
}

// $Barnyard: FUNCTION 004db9e0
void AWhackARaccMiniGameState::UpdateBucks( TFLOAT a_fDeltaTime )
{
	for ( TINT i = 0; i < NUM_BUCKS; i++ )
	{
		Mole&           rBuck          = m_aBucks[ i ];
		AModelInstance* pModelInstance = rBuck.pObject->GetModelInstance();

		if ( !pModelInstance || !pModelInstance->IsUpdatingSkeleton() || rBuck.eBuckState == 4 || rBuck.eState == MOLESTATE_FREE )
			continue;

		TMatrix44 matTransform;
		pModelInstance->GetLocalTransform( matTransform );

		// Bounce on the ground (Y goes down)
		TVector4 vTranslation = matTransform.GetTranslation();
		TFLOAT   fY           = a_fDeltaTime * rBuck.fVelocity + vTranslation.y;
		rBuck.fFallTime += a_fDeltaTime;
		rBuck.fVelocity += rBuck.fFallTime * a_fDeltaTime * 20.0f;
		rBuck.bLanded = rBuck.bLanded || rBuck.fVelocity >= -0.001f;

		if ( fY > rBuck.vGround.y && rBuck.fVelocity > 0.0f && rBuck.eBuckState != BUCKSTATE_FALLING )
		{
			fY              = rBuck.vGround.y;
			rBuck.fVelocity = rBuck.fVelocity * ( ( rBuck.eBuckState == BUCKSTATE_HOPPING ) ? -0.5f : -0.25f );
			rBuck.fFallTime = 0.0f;
		}

		if ( rBuck.eBuckState == BUCKSTATE_FALLING && rBuck.vGround.y + 0.5f < fY )
			RemoveBuck( &rBuck );

		vTranslation.y                = fY;
		matTransform.GetTranslation() = vTranslation;
		matTransform.RotateZ( BUCK_SPIN * a_fDeltaTime );
		pModelInstance->SetTransform( matTransform );

		rBuck.fTimer -= a_fDeltaTime;

		if ( rBuck.fTimer < 0.0f && rBuck.bLanded )
		{
			if ( rBuck.fTimer >= -0.1f )
			{
				// One more hop before it falls through the ground
				if ( rBuck.eBuckState != BUCKSTATE_HOPPING )
				{
					rBuck.eBuckState = BUCKSTATE_HOPPING;
					rBuck.fVelocity  = -1.0f;
				}
			}
			else
			{
				rBuck.eBuckState = BUCKSTATE_FALLING;
			}
		}
	}
}

// $Barnyard: FUNCTION 004dbe00
void AWhackARaccMiniGameState::UpdateForms( TFLOAT a_fDeltaTime )
{
	TRandom& rRandom = ARandom::GetSingleton()->m_oRandom;
	Level&   rLevel  = m_aLevels[ m_iLevel ];

	for ( TINT i = 0; i < rLevel.iNumForms; i++ )
	{
		Form& rForm = rLevel.pForms[ i ];

		if ( m_fElapsedTime < rForm.fTime || rForm.bTriggered )
			continue;

		rForm.bTriggered = TTRUE;

		TINT iCount     = rForm.iType;
		TINT iFormation = -1;
		TINT eKind      = KIND_SPECIAL;
		TINT iSpecial   = 0;

		if ( rForm.iType < 0 && rForm.iType == Form::TYPE_FREDDY )
		{
			iSpecial = SPECIAL_FREDDY;
			iCount   = 1;
		}
		else if ( rForm.iType == Form::TYPE_EGG )
		{
			iSpecial = SPECIAL_EGG;
			iCount   = 1;
		}
		else if ( rForm.iType == Form::TYPE_PECK )
		{
			iSpecial = SPECIAL_PECK;
			iCount   = 1;
		}
		else if ( rForm.iType == Form::TYPE_COYOTE )
		{
			iSpecial = SPECIAL_COYOTE;
			iCount   = 1;
		}
		else
		{
			if ( rForm.iType < 0 )
			{
				// A formation
				iFormation = -1 - rForm.iType;
				iCount     = m_aFormations[ iFormation ].iCount;
			}

			eKind = rForm.eKind;
		}

		for ( TINT k = 0; k < iCount; k++ )
		{
			// Find what pops out
			Mole* pMole   = TNULL;
			TINT  iObject = -1;

			if ( eKind == KIND_SPECIAL )
			{
				pMole = &m_aSpecials[ iSpecial ];
			}
			else if ( eKind == KIND_BUCK )
			{
				for ( TINT j = 0; j < NUM_BUCKS; j++ )
				{
					AModelInstance* pModelInstance = m_aBucks[ j ].pObject->GetModelInstance();

					if ( !pModelInstance || !pModelInstance->IsUpdatingSkeleton() )
					{
						iObject = j;
						break;
					}
				}

				// NOTE: retail uses m_aBucks[ -1 ] if all of them are in use
				pMole = &m_aBucks[ iObject ];
			}
			else
			{
				for ( TINT j = 0; j < NUM_RACCOONS; j++ )
				{
					if ( m_aRaccoons[ j ].eState == MOLESTATE_HIDDEN )
					{
						iObject = j;
						break;
					}
				}

				// NOTE: retail uses m_aRaccoons[ -1 ] if all of them are in use
				pMole = &m_aRaccoons[ iObject ];
			}

#ifdef BARNYARD_COMMUNITY_PATCH
			if ( eKind != KIND_SPECIAL && iObject == -1 )
				continue;
#endif

			if ( 0.0f == rForm.fStayTime )
				pMole->fTimer = rRandom.GetFloat() * ( rLevel.fMaxStayTime - 0.05f ) + 0.05f;
			else
				pMole->fTimer = rForm.fStayTime;

			// Find the hole
			TINT iHole = -1;

			if ( rForm.iType >= 0 || ( eKind != KIND_RACCOON && eKind != KIND_BUCK ) )
			{
				// A random free hole
				TINT j = 0;

				for ( ; j < NUM_HOLES; j++ )
				{
					if ( !m_aHoles[ j ].bOccupied )
						break;
				}

				if ( j != NUM_HOLES )
				{
					do iHole = rRandom.GetInt( NUM_HOLES );
					while ( m_aHoles[ iHole ].bOccupied == TTRUE );
				}

#ifdef BARNYARD_COMMUNITY_PATCH
				// NOTE: retail uses m_aHoles[ -1 ] if all the holes are occupied
				if ( iHole == -1 )
					continue;
#endif
			}
			else
			{
				iHole = m_aFormations[ iFormation ].aHoles[ k ];

				if ( m_aHoles[ iHole ].bOccupied )
					continue;
			}

			Hole& rHole     = m_aHoles[ iHole ];
			rHole.bOccupied = TTRUE;

			if ( eKind == KIND_SPECIAL )
			{
				rHole.iObject = iSpecial;
				rHole.eKind   = KIND_SPECIAL;
				pMole->vPos   = rHole.vPos;
				pMole->vPos.y -= 0.05f;
			}
			else
			{
				rHole.iObject = iObject;
				rHole.eKind   = ( eKind == KIND_BUCK ) ? KIND_BUCK : KIND_RACCOON;
				pMole->vPos   = rHole.vPos;
			}

			pMole->eState = MOLESTATE_RISING;
			pMole->iHole  = iHole;

			if ( eKind == KIND_BUCK )
				LaunchBuck( pMole );
			else if ( pMole->pObject->HasAnimation( TPS8( up ) ) )
				pMole->pObject->PlayAnimation( TPS8( up ) );
		}
	}
}

// $Barnyard: FUNCTION 004dc1b0
TBOOL __stdcall AWhackARaccMiniGameState::OnMalletImpact( AWhackARaccMiniGameState* a_pThis, void* a_pOwner, void* a_pData )
{
	if ( s_whackView.owner != a_pThis && !ACoopMinigame::BlockInput() )
		a_pThis->Whack( a_pThis->m_iCurrentPlayer );
	return TTRUE;
}

// $Barnyard: FUNCTION 004dc1d0
void AWhackARaccMiniGameState::StartGame()
{
	m_iLevel       = s_iStartLevel;
	m_fElapsedTime = 0.0f;
	APlayerManager::GetSingleton()->ResetPlayerIterator();
	m_iCurrentPlayer = 0;

	m_pMallet->SetSkeletonUpdating( TTRUE, TTRUE );
	m_pMallet->SetVisible( TTRUE );

	if ( m_oImpactListener.IsLinked() )
		m_oImpactListener.Disconnect();

	m_oImpactListener.Connect( m_pMallet->GetAnimationEmitter( TPS8( impactbp ) )->GetEmitter(), this, TREINTERPRETCAST( TGenericListener::t_Callback, OnMalletImpact ), 0 );

	ResetLevel( TTRUE );

	// NOTE: the same as the new game part of ResetLevel
	for ( TINT i = 0; i < MAX_PLAYERS; i++ )
	{
		Player& rPlayer         = m_aPlayers[ i ];
		rPlayer.iRow            = 1;
		rPlayer.iCol            = 1;
		rPlayer.iHole           = 4;
		rPlayer.fRingPulse      = 0.0f;
		rPlayer.eMalletState    = MALLETSTATE_IDLE;
		rPlayer.iLevelScore     = 0;
		rPlayer.iRaccoonScore   = 0;
		rPlayer.iEggScore       = 0;
		rPlayer.iPeckScore      = 0;
		rPlayer.iCoyoteScore    = 0;
		rPlayer.iNumMisses      = 0;
		rPlayer.iScore          = 0;
		rPlayer.iNumMisses2     = 0;
		rPlayer.iPrevLevelScore = 0;
		rPlayer.bFailed         = TFALSE;
		rPlayer.iLevel          = s_iStartLevel;
		rPlayer.iLives          = m_aInts[ INT_LIVES ];
		rPlayer.bPerfect        = TTRUE;
		rPlayer.iNumFreddyHits  = 0;
	}

	StartTurn( TTRUE );

	m_oTimeLeftHudElement.SetForceHide( TTRUE );
	m_oPickUpHudElement.SetNumber( 0 );
	m_bWon = TFALSE;
}

// $Barnyard: FUNCTION 004dc310
void AWhackARaccMiniGameState::OnInsertion()
{
	m_bShowPlayerFloats = TFALSE;
	AMiniGameState::OnInsertion();
	LoadData();

	AGameLoader::Terrain_Set( AGameLoader::Terrain_EnvWacARac, TFALSE, TTRUE, 0, 0, 0, 0 );
	ACameraManager::GetSingleton()->DetachCameraHelpers();

	// Animations of the mallet for each hole
	TString8 strName;

	for ( TINT i = 0; i < NUM_HOLES; i++ )
	{
		strName.Format( "hole_%i_imp", i + 1 );
		m_aHoleImpAnims[ i ] = TPString8( strName );

		strName.Format( "hole_%i_rec", i + 1 );
		m_aHoleRecAnims[ i ] = TPString8( strName );

		strName.Format( "hole_%i_rst", i + 1 );
		m_aHoleRstAnims[ i ] = TPString8( strName );
	}

	AAnimatableObjectManager* pAnimObjManager = AAnimatableObjectManager::GetSingleton();
	ATerrainInterface*        pTerrain        = ATerrainInterface::GetSingleton();

	for ( TINT i = 0; i < NUM_HOLES; i++ )
	{
		strName.Format( "hole_%i", i + 1 );
		m_aHoles[ i ].vPos      = pTerrain->GetLocatorPos( strName );
		m_aHoles[ i ].bOccupied = TFALSE;
		m_aHoles[ i ].fCooldown = 0.0f;
	}

	m_fWhackCooldown = 0.0f;

	// Raccoons
	AAnimatableObjectType* pType = pAnimObjManager->FindType( TPS8D( "racforwaca" ) );

	for ( TINT i = 0; i < NUM_RACCOONS; i++ )
	{
		Mole& rRaccoon   = m_aRaccoons[ i ];
		rRaccoon.pObject = pAnimObjManager->CreateAnimatableObject( pType, TNULL, 0 );
		rRaccoon.pObject->SetSkeletonUpdating( TFALSE, TTRUE );
		rRaccoon.pObject->SetVisible( TTRUE );
		rRaccoon.pObject->GetModelInstance()->SetTranslation( TVector4::VEC_ZERO.AsVector3() );
		rRaccoon.pObject->GetModelInstance()->SetDirection( s_vMoleDir );
		rRaccoon.pObject->PlayAnimation( TPS8( idle_1 ) );
		rRaccoon.pObject->SetAnimationTime( TPS8( idle_1 ), 0.0f );
		rRaccoon.pObject->FinishAnimation( TPS8( idle_1 ) );
		rRaccoon.eState = MOLESTATE_HIDDEN;
		rRaccoon.vPos   = TVector4::VEC_ZERO;
	}

	// Barn bucks
	pType = pAnimObjManager->FindType( TPS8D( "barnbuck" ) );

	for ( TINT i = 0; i < NUM_BUCKS; i++ )
	{
		Mole& rBuck   = m_aBucks[ i ];
		rBuck.pObject = pAnimObjManager->CreateAnimatableObject( pType, TNULL, 0 );
		rBuck.pObject->SetSkeletonUpdating( TFALSE, TTRUE );
		rBuck.pObject->SetVisible( TTRUE );
		rBuck.pObject->GetModelInstance()->SetTranslation( TVector4::VEC_ZERO.AsVector3() );
		rBuck.pObject->GetModelInstance()->SetDirection( s_vMoleDir );
		rBuck.pObject->GetModelInstance()->SetScale( TVector4( 0.5f, 0.5f, 0.5f, 1.0f ) );
		rBuck.eState = MOLESTATE_HIDDEN;
		rBuck.vPos   = TVector4::VEC_ZERO;
	}

	// The special characters
	m_aSpecials[ SPECIAL_FREDDY ].pObject = pAnimObjManager->CreateAnimatableObject( pAnimObjManager->FindType( TPS8D( "freddyforwaca" ) ), TNULL, 0 );
	m_aSpecials[ SPECIAL_PECK ].pObject   = pAnimObjManager->CreateAnimatableObject( pAnimObjManager->FindType( TPS8D( "peckforwaca" ) ), TNULL, 0 );
	m_aSpecials[ SPECIAL_EGG ].pObject    = pAnimObjManager->CreateAnimatableObject( pAnimObjManager->FindType( TPS8D( "eggforwaca" ) ), TNULL, 0 );
	m_aSpecials[ SPECIAL_COYOTE ].pObject = pAnimObjManager->CreateAnimatableObject( pAnimObjManager->FindType( TPS8D( "coyoteforwaca" ) ), TNULL, 0 );

	for ( TINT i = 0; i < SPECIAL_NUMOF; i++ )
	{
		Mole& rSpecial  = m_aSpecials[ i ];
		rSpecial.eState = MOLESTATE_HIDDEN;
		rSpecial.vPos   = TVector4::VEC_ZERO;
		rSpecial.pObject->SetVisible( TTRUE );
		rSpecial.pObject->GetModelInstance()->SetTranslation( TVector4::VEC_ZERO.AsVector3() );
		rSpecial.pObject->GetModelInstance()->SetDirection( s_vMoleDir );
	}

	// The clock hand
	m_pArrow = pAnimObjManager->CreateAnimatableObject( pAnimObjManager->FindType( TPS8D( "wacaarrow" ) ), TNULL, 0 );
	m_pArrow->GetModelInstance()->SetSkeletonUpdating( TTRUE );
	m_pArrow->SetVisible( TTRUE );
	m_vArrowPos = pTerrain->GetLocatorPos( "dial" );
	m_vArrowPos.x -= 0.1f;
	m_pArrow->GetModelInstance()->SetTranslation( m_vArrowPos.AsVector3() );
	m_pArrow->GetModelInstance()->SetDirection( s_vArrowDir );

	for ( TINT i = 0; i < MAX_PLAYERS; i++ )
	{
		m_aPlayers[ i ].iMalletHole = 0;
		m_aPlayers[ i ].iHole       = 4;
		m_aPlayers[ i ].vUnk2       = s_vDialDir;
		m_aPlayers[ i ].vUnk1       = s_vDialDir;
	}

	// The rings, drawn with their own render callbacks so they can turn red
	AAnimatableObjectType* pRingType = pAnimObjManager->FindType( TPS8D( "wackring" ) );

	m_pRing = pAnimObjManager->CreateAnimatableObject( pRingType, TNULL, 0 );
	m_pRing->GetModelInstance()->GetSceneObject()->GetInstance()->SetCustomRenderMethod( RenderRingCallback, this );
	m_pRing->SetSkeletonUpdating( TFALSE, TTRUE );
	m_pRing->SetVisible( TTRUE );

	m_pBadRing = pAnimObjManager->CreateAnimatableObject( pRingType, TNULL, 0 );
	m_pBadRing->GetModelInstance()->GetSceneObject()->GetInstance()->SetCustomRenderMethod( RenderBadRingCallback, this );
	m_pBadRing->SetSkeletonUpdating( TFALSE, TTRUE );
	m_pBadRing->SetVisible( TTRUE );

	m_pRing->GetModelInstance()->SetUnlit();
	m_pBadRing->GetModelInstance()->SetUnlit();

	// The mallet
	m_pMallet = pAnimObjManager->CreateAnimatableObject( pAnimObjManager->FindType( TPS8D( "wacmallet" ) ), TNULL, 0 );
	m_pMallet->GetModelInstance()->SetTransform( TVector4::VEC_ZERO, s_vDialDir );
	m_pMallet->SetSkeletonUpdating( TFALSE, TTRUE );
	m_pMallet->SetVisible( TFALSE );
	m_pMallet->GetModelInstance()->DisableDrawWithDefaultMaterials();
	m_pMallet->GetModelInstance()->SetUnlit();

	// The score dials
	AAnimatableObjectType* pDialType = pAnimObjManager->FindType( TPS8D( "wacadial" ) );

	for ( TINT i = 0; i < NUM_DIALS; i++ )
	{
		Dial& rDial = m_aDials[ i ];

		strName.Format( "score_%i", i + 1 );
		rDial.vPos    = pTerrain->GetLocatorPos( strName );
		rDial.fAngle  = 0.0f;
		rDial.pObject = pAnimObjManager->CreateAnimatableObject( pDialType, TNULL, 0 );
		rDial.pObject->GetModelInstance()->SetTranslation( rDial.vPos.AsVector3() );
		rDial.pObject->GetModelInstance()->SetDirection( s_vDialDir );
		rDial.pObject->SetSkeletonUpdating( TTRUE, TTRUE );
		rDial.pObject->SetVisible( TTRUE );
	}

	// HUD
	m_oPortrait.Create( APlayerProfileManager::GetSingleton()->GetMainProfile()->GetHideTextureName( -1, APlayerProfile::GENDER_NONE ), AGUI2Portrait::FLAGS_LEFT, &m_GUIElement );
	m_GUIElement.AddChildTail( m_oPortrait );
	m_oPortrait.SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPLEFT );
	m_oScoreFloats.Create( &m_GUIElement, "rekord26" );

	// NOTE: the original sets a copy of the effect (AGameTimeFX copy constructor 00492d30)
	AGameTimeFXManager* pGameTimeFX = AGameTimeFXManager::GetSingleton();
	pGameTimeFX->SetForcedFX( pGameTimeFX->FindFX( "Whack" ) );

	m_oTimeLeftHudElement.GetTimer().SetAttachment( T2GUIATTACHMENT_TOPCENTER, T2GUIATTACHMENT_TOPCENTER );
	m_oTimeLeftHudElement.SetForceHide( TTRUE );

	m_pMessage = new AGUI2AnythingPopup();
	m_pLifeHud = new AGUI2LifeHud();
	m_pLifeHud->Create( &m_GUIElement, TNULL, 8, "Score_Wood_Thin", T2GUIATTACHMENT_BOTTOMLEFT, T2GUIATTACHMENT_BOTTOMLEFT, 1.0f, TFALSE );
	m_pLifeHud->SetVisible( TTRUE );

	StartGame();
	ASSeqController::GetSingleton()->LoadScript( "sswar_intro.tso", TVector4::VEC_ZERO );
	m_bMouseControl = TFALSE;

	// No sky and no sun flare in the barn
	ASkyDome* pSkyDome = ARenderer::GetSingleton()->GetSkyDome();
	pSkyDome->SetFlags( pSkyDome->GetFlags() & ~ASkyDome::FLAGS_RENDER );
	pSkyDome->SetFlags( ( pSkyDome->GetFlags() & ~( ASkyDome::FLAGS_FROZEN | ASkyDome::FLAGS_ROTATE ) ) | ASkyDome::FLAGS_FROZEN );
	ASunFlareEffect::GetSingleton()->SetEnabled( TFALSE );

	m_bShowLevelText = TTRUE;
	m_oLevelText.Create( T2GUIFontManager::FindFont( "Rekord26" ), &m_GUIElement, T2GUIATTACHMENT_TOPCENTER, T2GUIATTACHMENT_TOPCENTER );
	m_oPickUpHudElement.SetNumber( 0 );
}

// $Barnyard: FUNCTION 004dd470
void AWhackARaccMiniGameState::UpdateGame( TFLOAT a_fDeltaTime )
{
	ACoopMinigame::SetScore( GetCurrentPlayer().iScore );
	UpdateBucks( a_fDeltaTime );

	if ( IsGameOver() || m_fElapsedTime > m_fTimeLimit )
		return;

	if ( ARootTask::GetSingleton()->IsPaused() )
	{
		GetCurrentPlayer().iRow = 1;
		GetCurrentPlayer().iCol = 1;
		return;
	}

	if ( FindChildState( &TGetClass( AAnticGameOverState ) ) )
		return;

	for ( TINT i = 0; i < NUM_HOLES; i++ )
		m_aHoles[ i ].fCooldown -= a_fDeltaTime;

	m_fWhackCooldown -= a_fDeltaTime;

	if ( m_fMessageTimer > 0.0f )
	{
		// The game waits while the message is shown
		TMatrix44 matTransform;
		MakeFlatTransform( matTransform, m_vArrowPos, TMath::HALF_PI );
		m_pArrow->GetModelInstance()->SetTransform( matTransform );

		m_fMessageTimer -= a_fDeltaTime;
		m_pMessage->Update( a_fDeltaTime );

		if ( m_fMessageTimer >= 0.0f )
			return;

		APlayerManager* pPlayerManager = APlayerManager::GetSingleton();
		m_oStartHudElement.Setup( pPlayerManager->GetCurrentPlayerSet()->GetHumanPlayerCount(), m_iCurrentPlayer );
		m_bShowLevelText = TTRUE;
		HideHud();
	}

	if ( !m_oStartHudElement.IsForceHidden() )
	{
		m_fElapsedTime = 0.0f;
		return;
	}

	if ( m_bShowLevelText == TTRUE )
	{
		ALocale::FormatTags( m_wszLevelText, TARRAYSIZE( m_wszLevelText ), LOCALE_LEVEL, PPropertyValue( m_iLevel + 1 ) );
		m_oLevelText.SetText( m_wszLevelText, 0x55000000 );
		m_bShowLevelText = TFALSE;
		ShowHud();
	}

	AGUI2::GetSingleton()->SetCursorVisible( TTRUE );

	if ( m_bMouseControl )
	{
		// Move the ring to the hole under the mouse
		TVector4 vRayPos;
		TVector4 vRayDir;
		ARenderer::GetSingleton()->GetScreenRay( ACameraManager::GetSingleton()->GetCurrentCamera(), AGUI2::GetMouseCursor().GetCursorPos(), vRayPos, vRayDir );

		TVector4 vRayEnd;
		vRayEnd.Multiply( vRayDir, 100.0f );
		vRayEnd.Add( vRayPos, vRayEnd );

		TINT   iClosestHole = -1;
		TFLOAT fClosestDist = MAX_PICK_DIST;

		for ( TINT i = 0; i < NUM_HOLES; i++ )
		{
			TFLOAT fDist = DistancePointToLineSq( m_aHoles[ i ].vPos, vRayPos, vRayEnd );

			if ( fDist < fClosestDist )
			{
				fClosestDist = fDist;
				iClosestHole = i;
			}
		}

		if ( iClosestHole != -1 )
		{
			GetCurrentPlayer().iCol = iClosestHole % NUM_ROWS;
			GetCurrentPlayer().iRow = iClosestHole / NUM_ROWS;
		}
	}

	if ( m_iLoopCue == -1 )
		m_iLoopCue = ASoundManager::GetSingleton()->PlayCue( SOUND_LOOP );

	if ( m_fShakeTimeLeft > 0.0f )
	{
		// Shake the camera after Freddy was hit
		m_fShakeTimeLeft -= a_fDeltaTime;
		m_fShakeTimer -= a_fDeltaTime;

		if ( m_fShakeTimer >= 0.0f || m_fShakeTimeLeft <= 0.0f )
		{
			m_vShake = TVector2::VEC_ZERO;
		}
		else
		{
			TRandom& rRandom = ARandom::GetSingleton()->m_oRandom;
			m_vShake.x       = rRandom.GetFloatMinMax( -m_aFloats[ FLOAT_SHAKE_AMOUNT ], m_aFloats[ FLOAT_SHAKE_AMOUNT ] );
			m_vShake.y       = rRandom.GetFloatMinMax( -m_aFloats[ FLOAT_SHAKE_AMOUNT ], m_aFloats[ FLOAT_SHAKE_AMOUNT ] );
			m_fShakeTimer    = m_aFloats[ FLOAT_SHAKE_INTERVAL ];
		}

		ACamera*   pCamera       = ACameraManager::GetSingleton()->GetCurrentCamera();
		TMatrix44& rMatrix       = pCamera->GetMatrix();
		rMatrix.GetTranslation() = ATerrainInterface::GetSingleton()->GetLocatorPos( "camera" );
		pCamera->LookAtPoint( ATerrainInterface::GetSingleton()->GetLocatorPos( "camera_target" ) );

		// NOTE: 0045b350 inlined, the negated up vector
		const TVector4& rUp = rMatrix.AsBasisVector4( BASISVECTOR_UP );
		TVector4        vDown( -rUp.x, -rUp.y, -rUp.z, 1.0f );

		const TVector4& rRight = rMatrix.AsBasisVector4( BASISVECTOR_RIGHT );
		TVector4        vRight( rRight.x, rRight.y, rRight.z, 1.0f );

		TVector4& rTranslation = rMatrix.GetTranslation();
		rTranslation.x += vRight.x * m_vShake.x + vDown.x * m_vShake.y;
		rTranslation.y += vRight.y * m_vShake.x + vDown.y * m_vShake.y;
		rTranslation.z += vRight.z * m_vShake.x + vDown.z * m_vShake.y;
	}

	if ( m_bBadHit == TTRUE )
	{
		// The red ring grows and fades out
		TMatrix44 matTransform;
		MakeFlatTransform( matTransform, GetCurrentPlayer().vBadRingPos, TMath::HALF_PI );

		TFLOAT fScale = 2.0f - m_fBadRingTime / m_aFloats[ FLOAT_BAD_RING_TIME ];
		matTransform.Scale( fScale, fScale, fScale );
		m_pBadRing->GetModelInstance()->SetTransform( matTransform );
		m_pBadRing->SetOpacity( m_fBadRingTime / m_aFloats[ FLOAT_BAD_RING_TIME ], TTRUE );

		m_fBadRingTime -= a_fDeltaTime;

		if ( m_fBadRingTime <= 0.0f )
		{
			m_bBadHit = TFALSE;
			m_pBadRing->SetSkeletonUpdating( TFALSE, TTRUE );
		}
	}

	UpdateSpecials( a_fDeltaTime );
	UpdateRaccoons( a_fDeltaTime );
	UpdateDials( a_fDeltaTime );
	UpdateMallet( a_fDeltaTime );
	UpdateForms( a_fDeltaTime );
}

// $Barnyard: FUNCTION 004ddc00
void AWhackARaccMiniGameState::UpdateAlways( TFLOAT a_fDeltaTime )
{
	m_oLevelText.Update( a_fDeltaTime );

	if ( m_bRestartPending && !FindChildState( &TGetClass( AAnticGameOverState ) ) )
	{
		// The results are gone, start a new game
		m_bRestartPending = TFALSE;
		StartGame();
		m_bTurnEnded = TFALSE;
		m_iLevel     = GetCurrentPlayer().iLevel;

		ResetDials();

		for ( TINT i = 0; i < NUM_DIALS; i++ )
			m_aDials[ i ].fAngle = 0.0f;

		APlayerManager* pPlayerManager = APlayerManager::GetSingleton();
		m_oStartHudElement.Setup( pPlayerManager->GetCurrentPlayerSet()->GetHumanPlayerCount(), m_iCurrentPlayer );
		HideHud();
		m_bShowLevelText = TTRUE;
	}

	if ( IsGameOver() || FindChildState( &TGetClass( AAnticGameOverState ) ) )
		return;

	m_pRing->SetSkeletonUpdating( m_oStartHudElement.IsForceHidden(), TTRUE );

	if ( m_fMessageTimer > 0.0f )
		return;

	// The clock hand
	AGUI2Timer& rTimer       = m_oTimeLeftHudElement.GetTimer();
	TFLOAT      fElapsedTime = m_fElapsedTime;

	if ( !m_oStartHudElement.IsForceHidden() || m_fMessageTimer > 0.0f || ( !rTimer.IsTickComplete() && rTimer.IsTimerFull() ) )
		fElapsedTime = 0.0f;

	TFLOAT    fAngle = TMath::PI / m_fTimeLimit * fElapsedTime;
	TMatrix44 matTransform;
	MakeFlatTransform( matTransform, m_vArrowPos, fAngle + fAngle + TMath::HALF_PI );
	m_pArrow->GetModelInstance()->SetTransform( matTransform );

	if ( m_oStartHudElement.IsForceHidden() != TTRUE )
	{
		m_oTimeLeftHudElement.SetTime( m_fTimeLimit - m_fElapsedTime );
		GetCurrentPlayer().fRingPulse += a_fDeltaTime * 4.0f;
	}
}

// $Barnyard: FUNCTION 004ddf50
void AWhackARaccMiniGameState::UpdateGameOver( TFLOAT a_fDeltaTime )
{
	AGUI2Timer& rTimer = m_oTimeLeftHudElement.GetTimer();

	if ( !rTimer.IsTickComplete() && rTimer.IsTimerFull() )
	{
		TMatrix44 matTransform;
		MakeFlatTransform( matTransform, m_vArrowPos, TMath::HALF_PI );
		m_pArrow->GetModelInstance()->SetTransform( matTransform );
		return;
	}

	if ( m_bGameEnding == TTRUE )
	{
		// Wait for the raccoons and the message before showing the results
		UpdateRaccoons( a_fDeltaTime );
		m_pMessage->Update( a_fDeltaTime );

		if ( m_pMessage->m_wszPopupText || m_aRaccoons[ 0 ].eState != MOLESTATE_HIDDEN )
			return;

		EndGame();
		return;
	}

	Player& rPlayer = GetCurrentPlayer();
	rPlayer.bFailed = rPlayer.iLevelScore < m_aLevels[ m_iLevel ].iTargetScore || rPlayer.iLives == 0;

	TINT iNumPlayers = APlayerManager::GetSingleton()->GetCurrentPlayerSet()->GetHumanPlayerCount();

	if ( m_bTurnEnded == TTRUE )
	{
		if ( FindChildState( &TGetClass( AAnticGameOverState ) ) )
			return;

		if ( m_iCurrentPlayer == iNumPlayers - 1 )
		{
			StartGame();
			m_bTurnEnded = TFALSE;
			m_iLevel     = GetCurrentPlayer().iLevel;

			ResetDials();

			for ( TINT i = 0; i < NUM_DIALS; i++ )
				m_aDials[ i ].fAngle = 0.0f;

			m_oStartHudElement.Setup( iNumPlayers, m_iCurrentPlayer );
			HideHud();
			m_bShowLevelText = TTRUE;
			return;
		}
	}
	else if ( GetCurrentPlayer().bFailed == TTRUE && !FindChildState( &TGetClass( AAnticGameOverState ) ) )
	{
		if ( GetCurrentPlayer().iLives == 0 )
		{
			EndGame();
			return;
		}

		// Out of time: the game is lost, all the raccoons come out
		m_bGameEnding = TTRUE;

		for ( TINT i = 0; i < SPECIAL_NUMOF; i++ )
		{
			Mole& rSpecial = m_aSpecials[ i ];

			if ( rSpecial.eState != MOLESTATE_HIDDEN && rSpecial.eState != MOLESTATE_HIDING && !rSpecial.pObject->IsAnimPlaying( TPS8( down ), 0.0f ) )
			{
				rSpecial.pObject->PlayAnimation( TPS8( down ) );
				rSpecial.eState = MOLESTATE_HIDING;
			}
		}

		ShowMessage( ALocaleManager::Interface()->GetString( LOCALE_GAME_OVER ) );

		for ( TINT i = 0; i < NUM_RACCOONS; i++ )
		{
			Hole& rHole     = m_aHoles[ i ];
			Mole& rRaccoon  = m_aRaccoons[ i ];
			rHole.bOccupied = TTRUE;
			rHole.eKind     = KIND_RACCOON;
			rHole.iObject   = i;
			rRaccoon.vPos   = rHole.vPos;
			rRaccoon.eState = MOLESTATE_RISING;
			rRaccoon.iHole  = i;
			rRaccoon.fTimer = m_aFloats[ FLOAT_GAME_OVER_UPTIME ];

			if ( rRaccoon.pObject->HasAnimation( TPS8( up ) ) )
				rRaccoon.pObject->PlayAnimation( TPS8( up ) );
		}

		return;
	}

	Player& rCurPlayer = GetCurrentPlayer();

	if ( !rCurPlayer.bFailed && !m_bWon )
	{
		// Level passed
		m_bWon = TTRUE;
		rCurPlayer.iLevel++;
		m_oTimeLeftHudElement.SetTime( 0.0f );

		if ( rCurPlayer.iLevel != m_iNumLevels )
			return;

		// All the levels are done
		ASoundManager::GetSingleton()->StopCue( m_iUnkCue );
		m_iUnkCue = -1;
		ASoundManager::GetSingleton()->StopCue( m_iLoopCue );
		m_iLoopCue = -1;

		ShowGameOver();
		m_bTurnEnded      = TTRUE;
		m_bRestartPending = TTRUE;
		HideHud();
		return;
	}

	TBOOL bAllDone = TTRUE;

	for ( TINT i = 0; i < iNumPlayers; i++ )
	{
		if ( !m_aPlayers[ i ].bFailed && m_aPlayers[ i ].iLevel < m_iNumLevels )
			bAllDone = TFALSE;
	}

	m_fElapsedTime = 0.0f;

	if ( bAllDone == TTRUE && !rCurPlayer.bFailed )
	{
		m_bWon = TTRUE;
		ASoundManager::GetSingleton()->StopCue( m_iUnkCue );
		m_iUnkCue = -1;

		ShowGameOver();
		m_bTurnEnded      = TTRUE;
		m_bRestartPending = TTRUE;
		HideHud();
		return;
	}

	// Next level or next player
	m_bWon = TFALSE;
	ASoundManager::GetSingleton()->StopCue( m_iUnkCue );
	m_iUnkCue = -1;
	ASoundManager::GetSingleton()->StopCue( m_iLoopCue );
	m_iLoopCue = -1;
	rTimer.Reset();

	GetCurrentPlayer().iPrevLevelScore = GetCurrentPlayer().iLevelScore;
	GetCurrentPlayer().iLevelScore     = 0;

	if ( iNumPlayers > 1 )
	{
		m_pRing->SetSkeletonUpdating( TFALSE, TTRUE );
		m_pMallet->SetSkeletonUpdating( TFALSE, TTRUE );

		m_iCurrentPlayer = APlayerManager::GetSingleton()->GetNextPlayer();

		while ( m_aPlayers[ m_iCurrentPlayer ].bFailed == TTRUE )
			m_iCurrentPlayer = APlayerManager::GetSingleton()->GetNextPlayer();

		m_pMallet->SetSkeletonUpdating( TTRUE, TTRUE );

		if ( m_oImpactListener.IsLinked() )
			m_oImpactListener.Disconnect();

		m_oImpactListener.Connect( m_pMallet->GetAnimationEmitter( TPS8( impactbp ) )->GetEmitter(), this, TREINTERPRETCAST( TGenericListener::t_Callback, OnMalletImpact ), 0 );
	}

	if ( GetCurrentPlayer().bPerfect == TTRUE )
	{
		// Nothing got away
		m_fMessageTimer = m_aFloats[ FLOAT_MESSAGE_TIME ];
		ShowMessage( ALocaleManager::Interface()->GetString( LOCALE_PERFECT ) );
		m_oTimeLeftHudElement.SetForceHide( TTRUE );
	}
	else
	{
		m_fMessageTimer = -1.0f;
	}

	m_iLevel = GetCurrentPlayer().iLevel;
	StartTurn( TFALSE );
	m_bTurnEnded = TFALSE;

	// A completed native level yields the LAN turn; the next level waits locally.
	ACoopMinigame::CompleteRound( GetCurrentPlayer().iScore, TFALSE );

	ResetDials();

	for ( TINT i = 0; i < NUM_DIALS; i++ )
		m_aDials[ i ].fAngle = 0.0f;
}
