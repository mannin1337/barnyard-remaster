#include "pch.h"
#include "Player/ACoopMinigame.h"
#include "Player/ACoopMinigameTiming.h"
#include "Terrain/ATerrainInterface.h"
#include <Render/TCameraObject.h>
#include "ADartMiniGame.h"
#include "AMiniGameManager.h"
#include "ARandom.h"
#include "Animation/ADartThrowAnimController.h"
#include "Animation/AAnimatableObject.h"
#include "Cameras/ACameraManager.h"
#include "Cameras/ACamera.h"
#include "FX/AGameTimeFX.h"
#include "FX/AGameTimeFXManager.h"
#include "GameInterface/AGameStateController.h"
#include "GameInterface/AAnticGameOverState.h"
#include "GameInterface/ASSeqState.h"
#include "GUI/AFadeManager.h"
#include "Helpers/AGameLoader.h"
#include "Helpers/AHighScoreManager.h"
#include "Helpers/AWeatherManager.h"
#include "HUD/AHudManager.h"
#include "Input/AInputMapManager.h"
#include "Locale/ALocale.h"
#include "Locale/ALocaleManager.h"
#include "Locale/AModStrings.h"
#include "Player/APlayerManager.h"
#include "Player/APlayerProfileManager.h"
#include "Render/ARenderer.h"
#include "Render/ASunFlareEffect.h"
#include "Sound/ASoundManager.h"
#include "Tasks/ARootTask.h"
#include "ALoadScreen.h"
#include "Units/AAnimatedUnit.h"
#include "Units/AInteractionComponent.h"
#include "Units/AUnitManager.h"
#include "Units/AUnitTransform.h"
#include "UnitMovement/Steering/AStationarySteer.h"

#include <GUI/T2GUIFontManager.h>
#include <Plugins/PPropertyParser/PPropertyValue.h>
#include <Toshi/TStringManager.h>
#include <Math/TMathInline.h>
#include "SoundBank/darts.h"
#include "SoundBank/music.h"
#include "SoundBank/ui.h"
#include <chrono>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

// Opcode defines PI
#undef PI

TOSHI_NAMESPACE_USING

// NOTE: GetClass 00522020, CreateObject 005258a0, CreateObjectInPlace 00525900 (the constructor is inlined in both)
TDEFINE_CLASS( ADartMiniGame );

TPSTRING8_DECLARE( ADartMiniGame );
TPSTRING8_DECLARE( dart );
TPSTRING8_DECLARE( dart_loc );
TPSTRING8_DECLARE( player_darts );

// Colours of the players (yellow and blue)
static constexpr TFLOAT YELLOW_R = 1.0f;
static constexpr TFLOAT YELLOW_G = 0.3019608f;
static constexpr TFLOAT YELLOW_B = 0.027450981f;
static constexpr TFLOAT BLUE_R   = 0.39215687f;
static constexpr TFLOAT BLUE_G   = 0.3019608f;
static constexpr TFLOAT BLUE_B   = 0.94901961f;

// Clock fade of the inactive player and how fast the time bonus fades (00773e44..00773e50)
static constexpr TFLOAT INACTIVE_ALPHA   = 0.5f;
static constexpr TFLOAT FADE_DURATION    = 0.5f;
static constexpr TFLOAT BONUS_FADE_SPEED = 0.3f;

// The AI considers a target hit if it is within this of the aim (00773e5c)
static constexpr TFLOAT AI_TOLERANCE = 0.1f;

// The camera (00773e54, 00773e58); the position is 0xc1a00000 = -20.0f (00783844, 00783858, 00783868)
static constexpr TFLOAT CAMERA_FOV      = 45.0f;
static TBOOL            s_bSetupCamera  = TTRUE;
static const TVector4   CAMERA_POSITION = TVector4( -20.0f, 0.0f, 0.0f, 1.0f );

static TUINT32 MakeColour( const TFLOAT* a_pColour )
{
	return ( ( ( TUINT32( a_pColour[ 0 ] * 255.0f ) | 0xFFFFFF00 ) << 8 | TUINT32( a_pColour[ 1 ] * 255.0f ) ) << 8 ) | TUINT32( a_pColour[ 2 ] * 255.0f );
}

// Creates the portrait of the main player with the hide of their profile
// $Barnyard: FUNCTION 00648080
static void CreatePortraitFromProfile( AGUI2Portrait* a_pPortrait, AGUI2Portrait::FLAGS a_eFlags, T2GUIElement* a_pParent )
{
	a_pPortrait->Create( APlayerProfileManager::GetSingleton()->GetMainProfile()->GetHideTextureName( -1, APlayerProfile::GENDER_NONE ), a_eFlags, a_pParent );
}

// Shows the name of the main player on the portrait
// $Barnyard: FUNCTION 00647300
static void SetPortraitNameFromProfile( AGUI2Portrait* a_pPortrait )
{
	a_pPortrait->SetName( APlayerProfileManager::GetSingleton()->GetMainProfile()->GetName().GetString( 0 ) );
}

TPSTRING8_DECLARE( darts_idle );
TPSTRING8_DECLARE( darts_raise );
TPSTRING8_DECLARE( darts_throw );

namespace
{
const TPString8* const s_dartsViewAnims[] = { &TPS8( darts_idle ), &TPS8( darts_raise ), &TPS8( darts_throw ) };
static constexpr TUINT32 DARTS_VIEW_MAGIC = 0x32564443;
static constexpr TUINT32 DARTS_VIEW_MASK = ( 1u << 20 ) - 1;

#include "darts_spectator.inc"

TUINT32 DartsNowMs()
{
	using namespace std::chrono;
	return TUINT32(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void HideCoopDartsPrompt(AInteractionHudElement& hud)
{
	hud.SetForceHide(TTRUE);
	// SetText() can show the line directly while the HUD manager still considers
	// this element hidden. Clear the actual line as native STATE_AIM_Y does.
	if (hud.m_abShowLine[0])
		hud.SetText(TNULL,2,0,-1,0,0xFFFFFFFF,-1.0f);
}

TBOOL DartsViewRange( TFLOAT value, TFLOAT low, TFLOAT high )
{
	return value >= low && value <= high; // Rejects NaN/infinity, too.
}
TBOOL ValidDartsView( const DartsViewFrame& frame )
{
	if ( frame.magic != DARTS_VIEW_MAGIC || (frame.visibleDarts & ~DARTS_VIEW_MASK) ||
	     (frame.presentTargets & ~DARTS_VIEW_MASK) || frame.currentPlayer >= 2 || frame.round >= 3 ||
	     frame.state > ADart::STATE_RESULT || !DartsViewRange(frame.aimX,-2,2) || !DartsViewRange(frame.aimY,-2,2) ) return TFALSE;
	if ( frame.throwSerial > 60 || frame.aimStage > 2 || frame.lastHitSerial > 60 || frame.lastHitOwner >= 2 ||
	     frame.hitScore < 0 || frame.hitScore > 10000000 || frame.hitTarget < 0 || frame.hitTarget > ADartThrow::TARGET_NONE ||
	     frame.hitTeam < ADartThrow::TEAM_NONE || frame.hitTeam > ADartThrow::TEAM_BLUE || frame.finished > 1 ||
	     !DartsViewRange(frame.timeLeft,0,3600) || !DartsViewRange(frame.timeLimit,0,3600) ) return TFALSE;
	if ( frame.aimStage && (frame.finished || !frame.throwSerial || frame.state != frame.aimStage) ) return TFALSE;
	for ( TINT score : frame.totalScore ) if ( score < 0 || score > 10000000 ) return TFALSE;
	for ( TFLOAT angle : frame.ringAngles )
		if ( !DartsViewRange(angle,-TMath::TWO_PI,TMath::TWO_PI) ) return TFALSE;
	for ( const auto& player : frame.players )
	{
		if ( player.dart > 10 || player.animation >= 3 || player.score < 0 || player.score > 10000000 ||
		     !DartsViewRange(player.animationTime,0,3600) ) return TFALSE;
		TFLOAT lengthSq = 0;
		for ( TFLOAT axis : player.direction )
		{
			if ( !DartsViewRange(axis,-1.1f,1.1f) ) return TFALSE;
			lengthSq += axis * axis;
		}
		if ( !DartsViewRange(lengthSq,0.5f,1.5f) ) return TFALSE;
	}
	const TUINT32 dart = frame.players[frame.currentPlayer].dart;
	const TUINT32 expectedSerial = dart < 10 ? 1u + frame.round * 20u + dart * 2u + frame.currentPlayer : 0u;
	if ( frame.throwSerial != expectedSerial ||
	     (frame.lastHitSerial && (frame.lastHitSerial - 1u) % 2u != frame.lastHitOwner) ) return TFALSE;
	for ( TINT i = 0; i < 20; ++i )
	{
		if ( !(frame.visibleDarts & (1u << i)) ) continue;
		const auto& pose = frame.darts[i];
		if ( !DartsViewRange(pose.x,-10000,10000) || !DartsViewRange(pose.y,-10000,10000) ||
		     !DartsViewRange(pose.z,-10000,10000) || !DartsViewRange(pose.scale,0.000001f,64) ) return TFALSE;
		TFLOAT b[9];
		for ( TINT axis = 0; axis < 9; ++axis ) b[axis] = pose.basis[axis] / 32767.0f;
		const TFLOAT determinant = b[0]*(b[4]*b[8]-b[5]*b[7]) - b[1]*(b[3]*b[8]-b[5]*b[6]) + b[2]*(b[3]*b[7]-b[4]*b[6]);
		if ( TMath::Abs(determinant) < 0.0001f ) return TFALSE; // No singular matrices in the model transform decomposition.
	}
	return TTRUE;
}

TBOOL PackDartsViewPose( AModelInstance* model, DartsViewPose& pose )
{
	TMatrix44 matrix;
	model->GetLocalTransform(matrix);
	pose.x = matrix.m_f41; pose.y = matrix.m_f42; pose.z = matrix.m_f43;
	if ( !DartsViewRange(pose.x,-10000,10000) || !DartsViewRange(pose.y,-10000,10000) ||
	     !DartsViewRange(pose.z,-10000,10000) ) return TFALSE; // Far falling darts are outside the view.
	const TFLOAT values[] = { matrix.m_f11,matrix.m_f12,matrix.m_f13,matrix.m_f21,matrix.m_f22,matrix.m_f23,matrix.m_f31,matrix.m_f32,matrix.m_f33 };
	pose.scale = 0;
	for ( TFLOAT value : values )
	{
		if ( !DartsViewRange(value,-64,64) ) return TFALSE;
		pose.scale = TMath::Max(pose.scale,TMath::Abs(value));
	}
	if ( pose.scale < 0.000001f ) return TFALSE;
	for ( TINT i = 0; i < 9; ++i ) pose.basis[i] = TINT16(values[i] / pose.scale * 32767.0f);
	return TTRUE;
}
void ApplyDartsViewPose( AModelInstance* model, const DartsViewPose& pose )
{
	const TFLOAT factor = pose.scale / 32767.0f;
	TMatrix44 matrix(
		pose.basis[0]*factor,pose.basis[1]*factor,pose.basis[2]*factor,0,
		pose.basis[3]*factor,pose.basis[4]*factor,pose.basis[5]*factor,0,
		pose.basis[6]*factor,pose.basis[7]*factor,pose.basis[8]*factor,0,
		pose.x,pose.y,pose.z,1);
	model->SetTransform(matrix);
}
TUINT32 DartsViewHash( const DartsViewFrame& frame )
{
	TUINT32 hash = 2166136261u;
	const auto* bytes = reinterpret_cast<const unsigned char*>(&frame);
	for ( TUINT i = 0; i < sizeof(frame); ++i ) hash = (hash ^ bytes[i]) * 16777619u;
	return hash;
}
}

// A complete local visual backup, never sent over the wire. Gameplay objects
// and their pointers/flags/scores are not copied or changed by the viewer.
struct ADartMiniGame::CoopView
{
	TBOOL saved = TFALSE;
	TFLOAT publishWait = 0;
	TMatrix44 models[43]; // 3 rings, 20 targets, 20 darts
	AModelInstance* viewModels[43];
	TMatrix44 targetBases[20]; // Static terrain locators for this board.
	TBOOL visible[43], updating[43];
	TVector4 playerDirection[2], playerPosition[2];
	TMatrix44 playerModels[2];
	TBOOL playerVisible[2], controllerDisabled[2];
	TINT playerAnimation[2], visualAnimation[2] = {-1,-1};
	TUINT32 visualDart[2] = {99,99};
	TFLOAT playerAnimationTime[2];
	TUINT lastHitSerial = 0, lastHitOwner = 0, seenHitSerial = 0;
	TINT hitScore = 0, hitTarget = ADartThrow::TARGET_NONE, hitTeam = ADartThrow::TEAM_NONE;
	TUINT displayedRound = 99;
	TINT displayedScore[2] = {-1,-1};
	DartsViewMotion motion;
	DartsAimHistory aimHistory;
	TUINT hostMatch = 0, lockMatch = 0, lockSerial = 0, lockStage = 0, lockTick = 0, promptStage = 0;
	TFLOAT lockAxis = 0;
	TBOOL usedFallback = TFALSE;
	AAnimatedUnit* heldDart = TNULL;
	TVector4 heldScale;
	TUINT loggedHand = 0;
	TBOOL loggedMotion = TFALSE;

	void ReleaseHandDart()
	{
		if (!heldDart) return;
		heldDart->RemoveFromParentAttachedUnit();
		heldDart->GetModelInstance()->SetScale(heldScale);
		heldDart = TNULL;
	}
};
ADartMiniGame::CoopView* ADartMiniGame::m_pCoopView = TNULL;

TUINT ADartMiniGame::GetCoopThrowSerial() const
{
	if ( !m_bStarted || m_iRound < 0 || m_iRound >= NUM_ROUNDS || m_iCurrentPlayer < 0 || m_iCurrentPlayer >= NUM_PLAYERS ) return 0;
	const Player& player = m_aPlayers[m_iCurrentPlayer];
	if ( player.iCurrentDart < 0 || player.iCurrentDart >= NUM_DARTS ) return 0;
	return 1u + TUINT(m_iRound * NUM_DARTS * NUM_PLAYERS + player.iCurrentDart * NUM_PLAYERS + m_iCurrentPlayer);
}

TUINT ADartMiniGame::GetCoopAimStage() const
{
	if ( !GetCoopThrowSerial() || m_bFading || m_bGameOver ) return 0;
	const Player& player = m_aPlayers[m_iCurrentPlayer];
	const ADart::STATE state = player.aDarts[player.iCurrentDart].m_eState;
	if ( state == ADart::STATE_AIM_X && m_fAimTimerX > 0.1f && !m_bFireX ) return 1;
	if ( state == ADart::STATE_AIM_Y && m_fAimTimerY > 0.1f && !m_bFireY ) return 2;
	return 0;
}

TINT ADartMiniGame::GetCoopTotal( TINT player ) const
{
	TINT total = m_aPlayers[player].iScore;
	for ( TINT round = 0; round < m_iRound; ++round ) total += m_aPlayers[player].aRoundScores[round];
	return total;
}

void ADartMiniGame::PublishCoopState()
{
	if ( ACoopMinigame::IsRealtimeDartsHost() && m_bStarted )
		ACoopMinigame::SetDartsState(m_iCurrentPlayer,m_iRound,GetCoopThrowSerial(),GetCoopAimStage(),GetCoopTotal(0),GetCoopTotal(1));
}

// Stable ordering shared by both instances, without access to board internals.
void ADartMiniGame::GetCoopViewModels( AModelInstance** models )
{
	TINT target = 3;
	for ( TINT ring = 0; ring < ADartBoard::NUM_RINGS; ++ring )
	{
		auto* item = m_oBoard.GetRing(ring);
		models[ring] = item->pModel;
		for ( TINT i = 0; i < item->iNumTargets; ++i ) models[target++] = item->apTargets[i]->pModel;
	}
	TASSERT(target == 23);
	for ( TINT p = 0; p < NUM_PLAYERS; ++p )
		for ( TINT d = 0; d < NUM_DARTS; ++d )
			models[23 + p*NUM_DARTS+d] = TSTATICCAST(AVisibleUnit,m_aPlayers[p].aDarts[d].m_pUnit)->GetModelInstance();
}

void ADartMiniGame::RestoreCoopView()
{
	if ( !m_pCoopView || !m_pCoopView->saved ) return;
	CoopView& view = *m_pCoopView;
	view.ReleaseHandDart();
	AModelInstance* models[43];
	GetCoopViewModels(models);
	for ( TINT i = 0; i < 43; ++i )
	{
		models[i]->SetTransform(view.models[i]);
		models[i]->SetVisible(view.visible[i]);
		models[i]->SetSkeletonUpdating(view.updating[i]);
	}
	for ( TINT p = 0; p < NUM_PLAYERS; ++p )
	{
		AAnimatedUnit* unit = TSTATICCAST(AAnimatedUnit,m_aPlayers[p].pUnit);
		unit->GetSteer()->SetPosition(view.playerPosition[p]);
		unit->GetSteer()->SetDirection(view.playerDirection[p]);
		unit->GetModelInstance()->SetTransform(view.playerModels[p]);
		unit->SetVisible(view.playerVisible[p]);
		AAnimatableObject* object = unit->GetAnimatableObject();
		object->KillAllAnimations();
		if ( view.playerAnimation[p] >= 0 )
		{
			object->PlayAnimation(*s_dartsViewAnims[view.playerAnimation[p]]);
			object->SetAnimationTime(*s_dartsViewAnims[view.playerAnimation[p]],view.playerAnimationTime[p]);
		}
		GetThrowController(m_aPlayers[p])->SetDisabled(view.controllerDisabled[p]);
		m_aPlayers[p].pPortrait->SetScore(m_bStarted ? m_aPlayers[p].iScore : 0);
		m_aPlayers[p].pCountHud->SetCount(m_bStarted ? NUM_DARTS-m_aPlayers[p].iCurrentDart : NUM_DARTS);
	}
	view.saved = TFALSE;
	view.motion = DartsViewMotion{};
	view.aimHistory = DartsAimHistory{};
	view.lockStage = view.promptStage = view.loggedHand = 0;
	view.loggedMotion = TFALSE;
	view.publishWait = 0;
	m_oFireHud.SetBarXVisible(TFALSE); m_oFireHud.SetBarYVisible(TFALSE);
	m_oFireHud.SetBullseyeVisible(TFALSE); m_oFireHud.SetTargetLockVisible(TFALSE);
	m_oRoundHud.SetForceHide(TTRUE);
	m_GUIElement.Hide();
	TINFO("[coopmini] Darts spectator restored; native started=%d round=%d score=%d\n",m_bStarted,m_iRound,m_bStarted ? m_aPlayers[0].iScore : 0);
}

TBOOL ADartMiniGame::OnUpdate( TFLOAT dt )
{
	const TBOOL realtime = ACoopMinigame::IsRealtimeDarts(this);
	if ( realtime && !ACoopMinigame::IsRealtimeDartsHost() && !ACoopMinigame::IsSpectating(this) )
	{
		// CanUpdate acknowledges loading, but must never advance a guest native board.
		ACoopMinigame::CanUpdate(this);
		return AGameState::OnUpdate(dt);
	}
	if ( ACoopMinigame::IsSpectating(this) )
	{
		DartsViewFrame received{};
		TUINT sequence = 0, match = 0;
		const bool available = ACoopMinigame::ReadSpectatorFrame(this,&received,sizeof(received),&sequence,&match);
		const bool fresh = available && (!m_pCoopView || m_pCoopView->motion.sequence != sequence || m_pCoopView->motion.match != match);
		if ( available && (!fresh || ValidDartsView(received)) )
		{
			if ( !m_pCoopView ) m_pCoopView = new CoopView;
			CoopView& view = *m_pCoopView;
			const TBOOL first = !view.saved;
			const TBOOL logViews = realtime && ACoopMinigame::IsViewLogEnabled();
			const TFLOAT lastAimX = view.motion.shown.aimX, lastAimY = view.motion.shown.aimY;
			const TUINT32 now = DartsNowMs();
			view.motion.Update(received,sequence,match,dt);
			const DartsViewFrame& frame = view.motion.shown;
			if (fresh) view.aimHistory.Record(received,sequence,match,now);
			if (logViews && !fresh && !view.loggedMotion && (TMath::Abs(frame.aimX-lastAimX) > 0.0001f || TMath::Abs(frame.aimY-lastAimY) > 0.0001f))
			{
				view.loggedMotion = TTRUE;
				TINFO("[darts-pvp] between packets sequence=%u change=1 dt=%.4f\n",sequence,dt);
			}
			auto& models = view.viewModels;
			if ( first )
			{
				GetCoopViewModels(models);
				for ( TINT i = 0; i < 43; ++i )
				{
					models[i]->GetLocalTransform(view.models[i]);
					view.visible[i] = models[i]->IsVisible();
					view.updating[i] = models[i]->IsUpdatingSkeleton();
				}
				for ( TINT p = 0; p < NUM_PLAYERS; ++p )
				{
					AAnimatedUnit* unit = TSTATICCAST(AAnimatedUnit,m_aPlayers[p].pUnit);
					view.playerDirection[p] = *unit->GetSteer()->GetDirection();
					view.playerPosition[p] = *unit->GetSteer()->GetPosition();
					unit->GetModelInstance()->GetLocalTransform(view.playerModels[p]);
					view.playerVisible[p] = unit->IsVisible();
					view.controllerDisabled[p] = GetThrowController(m_aPlayers[p])->IsDisabled();
					GetThrowController(m_aPlayers[p])->SetDisabled(TTRUE);
					view.playerAnimation[p] = -1;
					view.visualAnimation[p] = -1; view.visualDart[p] = 99;
					for ( TINT a = 0; a < 3; ++a )
						if ( unit->GetAnimatableObject()->IsAnimPlaying(*s_dartsViewAnims[a],0) )
						{
							view.playerAnimation[p] = a;
							view.playerAnimationTime[p] = unit->GetAnimatableObject()->GetAnimationTime(*s_dartsViewAnims[a]);
						}
				}
				view.saved = TTRUE;
			}
			SetupCamera();
			if (first) FacePlayersToCamera(); // Guests skip the native UpdatePreStart that anchors both arms.
			m_oStartHudElement.SetForceHide(TTRUE);
			m_GUIElement.Show();
			TVector4 top, right, left, bottom;
			ADartBoard::GetCorners(top,right,left,bottom);
			m_oFireHud.SetBoardCorners(top,right,left,bottom);
			TINT target = 0;
			for ( TINT ring = 0; ring < ADartBoard::NUM_RINGS; ++ring )
			{
				auto* item = m_oBoard.GetRing(ring);
				TMatrix44 matrix = TMatrix44::IDENTITY;
				matrix.Scale(item->fScale,item->fScale,item->fScale);
				matrix.RotateX(frame.ringAngles[ring]);
				item->pModel->SetTransform(matrix);
				item->pModel->SetVisible(TTRUE);
				item->pModel->SetSkeletonUpdating(TTRUE);
				for ( TINT i = 0; i < item->iNumTargets; ++i,++target )
				{
					auto* t = item->apTargets[i];
					if (first)
					{
						ATerrainInterface::GetSingleton()->GetLocatorTransform(t->strLocator,view.targetBases[target]);
						view.targetBases[target].Scale(t->fScale,t->fScale,t->fScale);
					}
					const bool present = (frame.presentTargets & (1u << target)) != 0;
					if (present)
					{
						matrix = view.targetBases[target];
						matrix.RotateX(frame.ringAngles[ring]);
						t->pModel->SetTransform(matrix);
					}
					t->pModel->SetVisible(present);
					t->pModel->SetSkeletonUpdating(present);
				}
			}
			const TUINT32 dartNumber = frame.players[frame.currentPlayer].dart;
			const TINT dartIndex = TINT(frame.currentPlayer)*NUM_DARTS + TINT(dartNumber);
			AAnimatedUnit* arm = TSTATICCAST(AAnimatedUnit,m_aPlayers[frame.currentPlayer].pUnit);
			AAnimatedUnit* held = TNULL;
			if (dartNumber < NUM_DARTS && (frame.visibleDarts & (1u << dartIndex)) &&
			    frame.state != ADart::STATE_IDLE && frame.state != ADart::STATE_FLYING && frame.state != ADart::STATE_RESULT)
				held = TSTATICCAST(AAnimatedUnit,m_aPlayers[frame.currentPlayer].aDarts[dartNumber].m_pUnit);
			if (view.heldDart != held)
			{
				view.ReleaseHandDart();
				if (held && arm->GetModelInstance()->GetBoneID(m_aPlayers[frame.currentPlayer].strDartBone) >= 0 &&
				    !held->GetAnimatableObject()->GetParentObject())
				{
					view.heldScale = held->GetModelInstance()->GetScale();
					arm->AttachUnit(held,m_aPlayers[frame.currentPlayer].strDartBone);
					view.heldDart = held;
				}
			}
			for ( TINT i = 0; i < 20; ++i )
			{
				const TBOOL visible = (frame.visibleDarts & (1u << i)) != 0;
				models[23+i]->SetVisible(visible);
				models[23+i]->SetSkeletonUpdating(visible);
				if ( visible && !(i == dartIndex && view.heldDart) ) ApplyDartsViewPose(models[23+i],frame.darts[i]);
			}
			for ( TINT p = 0; p < NUM_PLAYERS; ++p )
			{
				const auto& visual = frame.players[p];
				AAnimatedUnit* unit = TSTATICCAST(AAnimatedUnit,m_aPlayers[p].pUnit);
				TVector4 direction(visual.direction[0],visual.direction[1],visual.direction[2],0);
				if (direction.x*direction.x+direction.y*direction.y+direction.z*direction.z < 0.25f)
					direction.Set(received.players[p].direction[0],received.players[p].direction[1],received.players[p].direction[2],0);
				direction.Normalise();
				unit->GetSteer()->SetDirection(direction);
				unit->SetVisible(p == TINT(frame.currentPlayer));
				GetThrowController(m_aPlayers[p])->UpdateTransform(dt);
				AAnimatableObject* object = unit->GetAnimatableObject();
				const TPString8& clip = *s_dartsViewAnims[visual.animation];
				const bool dartChanged = view.visualDart[p] != visual.dart;
				const TBOOL startClip = view.visualAnimation[p] != TINT(visual.animation) || dartChanged ||
				                        (fresh && !object->IsAnimPlaying(clip,0));
				if (startClip)
				{
					object->KillAllAnimations();
					object->PlayAnimation(clip);
					view.visualAnimation[p] = visual.animation; view.visualDart[p] = visual.dart;
				}
				// The engine advances the clip between packets, including the dart_loc attachment.
				if (startClip || (fresh && TMath::Abs(object->GetAnimationTime(clip)-visual.animationTime) > 0.12f))
					object->SetAnimationTime(clip,visual.animationTime);
				if (first || view.displayedScore[p] != visual.score)
				{
					m_aPlayers[p].pPortrait->SetScore(visual.score);
					view.displayedScore[p] = visual.score;
				}
				if (dartChanged) m_aPlayers[p].pCountHud->SetCount(NUM_DARTS-visual.dart);
				m_aPlayers[p].pPortrait->SetNameAlpha(p == TINT(frame.currentPlayer) ? 1.0f : INACTIVE_ALPHA);
				m_aPlayers[p].pPortrait->SetClockEnabled(TFALSE); // Display the host clock; never run a guest timer.
				m_aPlayers[p].pPortrait->SetClockVisible(p == TINT(frame.currentPlayer) && frame.aimStage != 0);
				m_aPlayers[p].pPortrait->SetClockDuration(frame.timeLimit);
				m_aPlayers[p].pPortrait->SetClockTime(frame.timeLeft);
			}
			const TBOOL aiming = frame.state == ADart::STATE_AIM_X || frame.state == ADart::STATE_AIM_Y ||
			                    (frame.state >= ADart::STATE_AI_TARGET && frame.state <= ADart::STATE_AI_AIM_Y);
			m_oFireHud.SetColour(m_aPlayers[frame.currentPlayer].afColour[0],m_aPlayers[frame.currentPlayer].afColour[1],m_aPlayers[frame.currentPlayer].afColour[2]);
			if (view.lockStage && (match != view.lockMatch || frame.throwSerial != view.lockSerial ||
			    frame.aimStage != view.lockStage || ACoopMinigame::BlockInput())) view.lockStage = 0;
			const TUINT shownLock = now-view.lockTick <= DartsAimWindowMs ? view.lockStage : 0;
			const TFLOAT aimX = shownLock == 2 ? view.lockAxis : frame.aimX;
			const TFLOAT aimY = shownLock == 1 ? view.lockAxis : frame.aimY;
			m_oFireHud.SetBarX(-aimX); m_oFireHud.SetBarY(-aimY);
			m_oFireHud.SetBarXVisible(aiming && frame.state != ADart::STATE_AIM_X); m_oFireHud.SetBarYVisible(aiming);
			m_oFireHud.SetBullseyeVisible(aiming && frame.state != ADart::STATE_AIM_X);
			m_oFireHud.SetBullseyePosition(-aimX,-aimY);
			if (shownLock) m_oFireHud.SetBarXLocked(TTRUE);
			if ( realtime )
			{
				const TBOOL localAim = frame.currentPlayer == ACoopMinigame::GetRealtimeDartsLocalPlayer() && frame.aimStage != 0 && !ACoopMinigame::BlockInput();
				const TUINT prompt = localAim && !shownLock ? frame.aimStage : 0;
				if (prompt)
				{
					m_oInteractionHud.SetForceHide(TFALSE);
					if (prompt != view.promptStage)
						m_oInteractionHud.SetText(ALocaleManager::Interface()->GetString(prompt == 1 ? 783 : 782),2,TINT(m_pInputMap),AInputCommand_Interact,0,0xFFFFFFFF,-1.0f);
				}
				else HideCoopDartsPrompt(m_oInteractionHud);
				view.promptStage = prompt;
				if (logViews && frame.aimStage && view.heldDart && view.loggedHand != frame.throwSerial)
				{
					AModelInstance* model = arm->GetModelInstance();
					const TINT bone = model->GetBoneID(m_aPlayers[frame.currentPlayer].strDartBone);
					TMatrix44 hand = TMatrix44::IDENTITY;
					model->GetBoneTransform(bone,hand);
					TVector4 pos;
					model->GetPosition(pos);
					ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
					TVector2 screen;
					const TBOOL inView = ARenderer::GetSingleton()->ProjectToGUI(camera,hand.GetTranslation(),screen);
					const TFLOAT error = TVector4::Distance(pos,camera->GetMatrix().GetTranslation());
					const TBOOL attached = view.heldDart->IsAttachedToParent(arm);
					const TBOOL drawable = model->IsVisible() && model->IsUpdatingSkeleton();
					if (drawable && inView)
					{
						view.loggedHand = frame.throwSerial;
						TINFO("[darts-pvp] hand view throw=%u owner=%u anchorError=%.4f bone=%d attached=%d drawable=%d inView=%d guiX=%.2f guiY=%.2f\n",frame.throwSerial,frame.currentPlayer,error,bone,attached,drawable,inView,screen.x,screen.y);
					}
				}
				if ( view.displayedRound != frame.round )
				{
					view.displayedRound = frame.round;
					m_oRoundHud.SetDuration(2.0f);
					m_oRoundHud.Start(TFALSE,frame.round + 1);
				}
				if ( frame.lastHitSerial && view.seenHitSerial != frame.lastHitSerial )
				{
					view.seenHitSerial = frame.lastHitSerial;
					TINFO("[darts-pvp] hit apply match=%u sequence=%u throw=%u owner=%u target=%d team=%d points=%d scores=%d,%d totals=%d,%d targets=%05x\n",match,sequence,frame.lastHitSerial,frame.lastHitOwner,frame.hitTarget,frame.hitTeam,frame.hitScore,frame.players[0].score,frame.players[1].score,frame.totalScore[0],frame.totalScore[1],frame.presentTargets);
				}
				if ( logViews && fresh )
				{
					TINFO("[darts-pvp] frame apply match=%u sequence=%u throw=%u owner=%u round=%u state=%u hit=%u targets=%05x scores=%d,%d totals=%d,%d hash=%08x\n",match,sequence,frame.throwSerial,frame.currentPlayer,frame.round + 1,frame.state,frame.lastHitSerial,frame.presentTargets,frame.players[0].score,frame.players[1].score,frame.totalScore[0],frame.totalScore[1],DartsViewHash(received));
				}
			}
			if ( first ) TINFO("[coopmini] Darts spectator applied; native started=%d round=%d score=%d\n",m_bStarted,m_iRound,m_bStarted ? m_aPlayers[0].iScore : 0);
		}
		else
		{
			HideCoopDartsPrompt(m_oInteractionHud);
			if (m_pCoopView) m_pCoopView->promptStage = 0;
		}
		return AGameState::OnUpdate(dt);
	}

	RestoreCoopView();
	TBOOL result = AMiniGameState::OnUpdate(dt);
	PublishCoopState();
	if ( realtime && ACoopMinigame::IsRealtimeDartsHost() && m_iCurrentPlayer != 0 ) HideCoopDartsPrompt(m_oInteractionHud);
	if ( ACoopMinigame::IsPlaying() && ( !ACoopMinigame::BlockInput() || (realtime && ACoopMinigame::IsRealtimeDartsHost()) ) && m_bStarted )
	{
		if ( !m_pCoopView ) m_pCoopView = new CoopView;
		if (!CoopMini::ViewSendDue(m_pCoopView->publishWait,dt)) return result;
		DartsViewFrame frame{};
		frame.magic = DARTS_VIEW_MAGIC;
		frame.currentPlayer = m_iCurrentPlayer; frame.round = m_iRound;
		frame.state = GetCurrentPlayer().iCurrentDart < NUM_DARTS ? GetCurrentDart().m_eState : ADart::STATE_IDLE;
		frame.throwSerial = GetCoopThrowSerial(); frame.aimStage = GetCoopAimStage();
		frame.lastHitSerial = m_pCoopView->lastHitSerial; frame.lastHitOwner = m_pCoopView->lastHitOwner;
		frame.hitScore = m_pCoopView->hitScore; frame.hitTarget = m_pCoopView->hitTarget; frame.hitTeam = m_pCoopView->hitTeam;
		frame.totalScore[0] = GetCoopTotal(0); frame.totalScore[1] = GetCoopTotal(1);
		frame.finished = m_bGameOver ? 1u : 0u;
		// The first native fade precedes ADartThrow::Reset; never read its POD aim until a dart starts.
		if ( frame.state != ADart::STATE_IDLE )
		{
			frame.aimX = m_oThrow.m_fAimX; frame.aimY = m_oThrow.m_fAimY;
			frame.timeLimit = m_fAimTime + m_fAimTimeBonus;
			frame.timeLeft = TMath::Max(0.0f,frame.timeLimit - m_oThrow.m_fTimeUsed);
		}
		TINT target = 0;
		for ( TINT ring = 0; ring < ADartBoard::NUM_RINGS; ++ring )
		{
			auto* item = m_oBoard.GetRing(ring);
			frame.ringAngles[ring] = item->fAngle;
			for ( TINT i = 0; i < item->iNumTargets; ++i,++target )
				if ( !item->apTargets[i]->bKnockedOff ) frame.presentTargets |= 1u << target;
		}
		for ( TINT p = 0; p < NUM_PLAYERS; ++p )
		{
			const Player& player = m_aPlayers[p];
			auto& visual = frame.players[p];
			const TVector4& direction = *player.pUnit->GetSteer()->GetDirection();
			visual.direction[0] = direction.x; visual.direction[1] = direction.y; visual.direction[2] = direction.z;
			visual.score = player.iScore; visual.dart = player.iCurrentDart;
			AAnimatableObject* object = TSTATICCAST(AAnimatedUnit,player.pUnit)->GetAnimatableObject();
			for ( TUINT32 a = 0; a < 3; ++a )
				if ( object->IsAnimPlaying(*s_dartsViewAnims[a],0) ) visual.animation = a;
			visual.animationTime = object->GetAnimationTime(*s_dartsViewAnims[visual.animation]);
			for ( TINT d = 0; d < NUM_DARTS; ++d )
			{
				AVisibleUnit* dart = TSTATICCAST(AVisibleUnit,player.aDarts[d].m_pUnit);
				if ( dart->IsVisible() && PackDartsViewPose(dart->GetModelInstance(),frame.darts[p*NUM_DARTS+d]) )
					frame.visibleDarts |= 1u << (p*NUM_DARTS+d);
			}
		}
		if ( ValidDartsView(frame) )
		{
			TUINT sequence = 0, match = 0;
			ACoopMinigame::PublishSpectatorFrame(this,&frame,sizeof(frame),&sequence,&match);
			if (sequence)
			{
				m_pCoopView->hostMatch = match;
				m_pCoopView->aimHistory.Record(frame,sequence,match,DartsNowMs());
			}
			if ( realtime && sequence && ACoopMinigame::IsViewLogEnabled() ) TINFO("[darts-pvp] frame publish match=%u sequence=%u throw=%u owner=%u round=%u state=%u hit=%u targets=%05x scores=%d,%d totals=%d,%d hash=%08x\n",match,sequence,frame.throwSerial,frame.currentPlayer,frame.round + 1,frame.state,frame.lastHitSerial,frame.presentTargets,frame.players[0].score,frame.players[1].score,frame.totalScore[0],frame.totalScore[1],DartsViewHash(frame));
		}
	}
	return result;
}


// $Barnyard: FUNCTION 00525540
ADartMiniGame::ADartMiniGame()
    : m_fFlightTime( 0.0f )
    , m_fRaiseTimer( 0.0f )
    , m_fResultTimer( 0.0f )
    , m_fAimTimerY( 0.0f )
    , m_fAimTimerX( 0.0f )
    , m_bFireX( TFALSE )
    , m_bFireY( TFALSE )
    , m_bGameOver( TFALSE )
    , m_bGameOverPushed( TFALSE )
    , m_bRoundFlag( TFALSE )
    , m_bFading( TFALSE )
    , m_bFadeIn( TFALSE )
{
#ifdef BARNYARD_COMMUNITY_PATCH
	// NOTE: not initialised by the original
	m_bCogsSpin = TFALSE;
#endif

	m_pInputMap = AInputMapManager::GetSingleton()->FindMap( TPS8( ADartMiniGame ) );
}

// $Barnyard: FUNCTION 00525940
// $Barnyard: FUNCTION 00525740
// $Barnyard: FUNCTION 00525730
ADartMiniGame::~ADartMiniGame()
{
	delete m_pCoopView;
	m_pCoopView = TNULL;
}

// $Barnyard: FUNCTION 00522670
TBOOL ADartMiniGame::ProcessCommand( AInputCommand a_eInputCommand, const TInputInterface::InputEvent* a_pInputEvent, TBOOL& a_rStopEvents )
{
	if ( ACoopMinigame::BlockInput() )
		return AMiniGameState::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );

	const TBOOL realtime = ACoopMinigame::IsRealtimeDarts(this);
	if ( realtime )
	{
		if ( a_eInputCommand != AInputCommand_Interact )
			return AMiniGameState::ProcessCommand(a_eInputCommand,a_pInputEvent,a_rStopEvents);
		if ( a_pInputEvent->GetEventType() != TInputInterface::EVENT_TYPE_GONE_DOWN ) return TTRUE;
		if ( !ACoopMinigame::IsRealtimeDartsHost() )
		{
			DartsViewFrame frame{};
			TUINT match = 0;
			if (!m_pCoopView || !m_pCoopView->saved ||
			    !ACoopMinigame::ReadSpectatorFrame(this,&frame,sizeof(frame),TNULL,&match) || !ValidDartsView(frame) ||
			    frame.currentPlayer != ACoopMinigame::GetRealtimeDartsLocalPlayer() || !frame.aimStage) return TTRUE;
			CoopView& view = *m_pCoopView;
			const auto& shown = view.motion.shown;
			const TUINT32 now = DartsNowMs();
			if (view.motion.match != match || shown.throwSerial != frame.throwSerial || shown.aimStage != frame.aimStage) return TTRUE;
			const bool sameStage = view.lockMatch == match && view.lockSerial == frame.throwSerial && view.lockStage == frame.aimStage;
			const DartsPressMode mode = DartsPhysicalPress(sameStage,view.usedFallback,now-view.lockTick);
			if (mode == DartsPressMode::Wait) return TTRUE;
			TFLOAT axis = frame.aimStage == 1 ? shown.aimY : shown.aimX;
			TUINT32 reference = 0;
			const bool fallback = mode == DartsPressMode::HostTime && ACoopMinigame::SupportsDartsViewAim();
			if (!fallback && ACoopMinigame::SupportsDartsViewAim())
			{
				reference = view.aimHistory.Closest(view.motion.sequence,match,frame.throwSerial,frame.aimStage,now,axis);
				if (!view.aimHistory.Resolve(reference,match,frame.throwSerial,frame.aimStage,now,axis)) return TTRUE;
			}
			view.lockAxis = axis;
			view.usedFallback = fallback || (sameStage && view.usedFallback);
			view.lockMatch = match;
			view.lockSerial = frame.throwSerial;
			view.lockStage = frame.aimStage;
			view.lockTick = now;
			ACoopMinigame::SubmitDartsInput(frame.throwSerial,frame.aimStage,reference);
			TINFO("[darts-pvp] prediction throw=%u stage=%u view=%u aim=%.5f fallback=%d\n",frame.throwSerial,frame.aimStage,reference,axis,fallback);
			return TTRUE;
		}
	}

	TINT iSlot = realtime ? 0 : APlayerManager::GetSingleton()->GetCurrentPlayerSet()->GetPlayerSlotIndexByDevice( a_pInputEvent->GetSource() );

	if ( iSlot != m_iCurrentPlayer || m_bGameOver || !m_oStartHudElement.IsForceHidden() || a_eInputCommand != AInputCommand_Interact ||
	     (realtime && !GetCoopAimStage()) )
		return AMiniGameState::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );

	ADart::STATE eState = GetCurrentDart().m_eState;

	if ( eState == ADart::STATE_AIM_X )
	{
		if ( m_fAimTimerX > 0.1f )
			m_bFireX = TTRUE;
	}
	else
	{
		if ( eState != ADart::STATE_AIM_Y )
			return AMiniGameState::ProcessCommand( a_eInputCommand, a_pInputEvent, a_rStopEvents );

		if ( m_fAimTimerY > 0.1f )
		{
			m_bFireY = TTRUE;
			return TTRUE;
		}
	}

	return TTRUE;
}

// $Barnyard: FUNCTION 00522030
ASoundId ADartMiniGame::GetSound()
{
	return soundbank::MUSIC_POOL;
}

// $Barnyard: FUNCTION 00522f10
void ADartMiniGame::OnPause()
{
	ARootTask::GetSingleton()->SetRenderWorld( TFALSE );
	g_oLoadScreen.StartLoading( 100, TTRUE );
	AGameLoader::Terrain_Set( AGameLoader::Terrain_EnvDartsLarge, TFALSE, TTRUE, 0, 0, 0, 0 );
	g_oLoadScreen.Reset();
	g_oLoadScreen.SetLoadingState( TFALSE, TTRUE );

	SetupCamera();
	SetVisible( TTRUE );

	AGameStateController::GetSingleton()->SetFlags( AGameStateController::FLAGS_FADING_IN );
}

// $Barnyard: FUNCTION 00524e40
void ADartMiniGame::OnInsertion()
{
	m_bShowPlayerFloats = TFALSE;

	ASkyDome* pSkyDome = ARenderer::GetSingleton()->GetSkyDome();
	pSkyDome->SetFlags( pSkyDome->GetFlags() & ~ASkyDome::FLAGS_RENDER );
	pSkyDome->SetFlags( ( pSkyDome->GetFlags() & ~( ASkyDome::FLAGS_FROZEN | ASkyDome::FLAGS_ROTATE ) ) | ASkyDome::FLAGS_FROZEN );

	ASunFlareEffect::GetSingleton()->SetEnabled( TFALSE );

	AGameLoader::Terrain_Set( AGameLoader::Terrain_EnvDartsLarge, TFALSE, TTRUE, 0, 0, 0, 0 );

	ACameraManager::GetSingleton()->DetachCameraHelpers();
	ACameraManager::GetSingleton()->GetCurrentCamera()->GetMatrix().SetTranslation( TVector4::VEC_POSZ );

	AUnitTransform oDartTransform( TVector4::VEC_ZERO, TVector4::VEC_NEGZ, 0, -1, -1, TTRUE );

	m_oBoard.Create();

	m_eSavedDayPhase = AGameTimeManager::GetSingleton()->GetCurrentDayPhase();

	AGameTimeFX oFX( *AGameTimeFXManager::GetSingleton()->FindFX( "DARTS" ) );
	AGameTimeFXManager::GetSingleton()->SetForcedFX( &oFX );

	AWeatherManager::GetSingleton()->SetCurrentWeather( AWeatherManager::WEATHERTYPE_NORMAL );

	// The darts of the players
	for ( TINT i = 0; i < NUM_PLAYERS; i++ )
	{
		Player& rPlayer = m_aPlayers[ i ];

		for ( TINT j = 0; j < NUM_DARTS; j++ )
		{
			ADart& rDart = rPlayer.aDarts[ j ];

			oDartTransform.m_iModelIndex = i;
			rDart.m_pUnit                = AUnitManager::GetSingleton()->SpawnUnit( TPS8( dart ), &oDartTransform );
			TSTATICCAST( AVisibleUnit, rDart.m_pUnit )->SetVisible( TFALSE );
			rDart.m_iScore = 0;
			rDart.m_eState = ADart::STATE_IDLE;
			rDart.SetSpinning( TFALSE );
			rDart.m_iRing     = ADartThrow::RING_NONE;
			rDart.m_bOnTarget = TFALSE;
			rDart.m_fScale    = 1.0f;
		}

		rPlayer.aRoundScores[ 0 ] = 0;
		rPlayer.aRoundScores[ 1 ] = 0;
		rPlayer.aRoundScores[ 2 ] = 0;
	}

	m_oAimDart.m_pUnit = AUnitManager::GetSingleton()->SpawnUnit( TPS8( dart ), &oDartTransform );
	TSTATICCAST( AVisibleUnit, m_oAimDart.m_pUnit )->SetVisible( TFALSE );
	m_oAimDart.SetSpinning( TFALSE );

	m_aPlayers[ 0 ].eGender = APlayerProfileManager::GetSingleton()->GetMainGender();
	m_iCurrentPlayer        = 0;

	T2GUIFont* pFont = T2GUIFontManager::FindFont( "Rekord26" );
	m_oUnusedText.SetFont( pFont );
	m_oUnusedText.SetDimensionsDirty();
	m_oUnusedText.SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPLEFT );
	m_oUnusedText.SetTransform( 10.0f, 10.0f, 0.0f );
	m_oUnusedText.SetTextAlign( T2GUIFont::TextAlign( 1 ) );
	m_GUIElement.AddChildTail( m_oUnusedText );

	// HUD
	m_aCountHuds[ 0 ].SetParent( &m_GUIElement );
	m_aCountHuds[ 1 ].SetParent( &m_GUIElement );
	m_oFireHud.SetParent( &m_GUIElement );
	m_oInteractionHud.m_pParent = &m_GUIElement;

	m_aCountHuds[ 0 ].SetOffTexture( "Dart_Off" );
	m_aCountHuds[ 1 ].SetOffTexture( "Dart_Off" );
	m_aCountHuds[ 0 ].SetOnTexture( "Dart_On" );
	m_aCountHuds[ 1 ].SetOnTexture( "Dart_On" );

	AHudManager* pHudManager = AHudManager::GetSingleton();
	pHudManager->AddElement( &m_oInteractionHud, TFALSE, TTRUE, TFALSE );
	pHudManager->AddElement( &m_aCountHuds[ 0 ], TFALSE, TTRUE, TFALSE );
	pHudManager->AddElement( &m_aCountHuds[ 1 ], TFALSE, TTRUE, TFALSE );
	pHudManager->AddElement( &m_oFireHud, TFALSE, TTRUE, TFALSE );

	m_aCountHuds[ 0 ].SetColour( YELLOW_R, YELLOW_G, YELLOW_B );
	m_aCountHuds[ 1 ].SetColour( BLUE_R, BLUE_G, BLUE_B );
	m_aCountHuds[ 1 ].FlipU();

	m_fSavedFOV = ACameraManager::GetSingleton()->GetCurrentCamera()->GetFOV();

	// The players
	for ( TINT i = 0; i < NUM_PLAYERS; i++ )
	{
		AUnitTransform oTransform( TVector4::VEC_ZERO, TVector4::VEC_POSZ, 0, i, -1, TTRUE );

		// The skin: the male or female player cow, the second player is Root
		if ( i == 0 )
			oTransform.m_iModelIndex = APlayerProfileManager::GetSingleton()->GetMainGender() != APlayerProfile::GENDER_MALE;
		else if ( i == 1 )
			oTransform.m_iModelIndex = 2;

		AUnit* pUnit = AUnitManager::GetSingleton()->SpawnUnit( TPS8( player_darts ), &oTransform );
		TSTATICCAST( AInteractionComponent, pUnit->FindComponent( &TGetClass( AInteractionComponent ) ) )->SetInteractionsDisabled( TTRUE );
		TSTATICCAST( AVisibleUnit, pUnit )->SetVisible( TFALSE );
		pUnit->ChangeSteer( &TGetClass( AStationarySteer ) );

		m_aPlayers[ i ].strDartBone = TPS8( dart_loc );
		m_aPlayers[ i ].pUnit       = pUnit;
		m_aPlayers[ i ].bComputer   = TFALSE;
	}

	m_aPlayers[ 0 ].pCountHud     = &m_aCountHuds[ 0 ];
	m_aPlayers[ 1 ].bComputer     = !ACoopMinigame::IsRealtimeDarts();
	m_aPlayers[ 0 ].iTeam         = ADartThrow::TEAM_YELLOW;
	m_aPlayers[ 0 ].pPortrait     = &m_aPortraits[ 0 ];
	m_aPlayers[ 0 ].afColour[ 0 ] = 1.0f;
	m_aPlayers[ 0 ].afColour[ 1 ] = 0.84705883f;
	m_aPlayers[ 0 ].afColour[ 2 ] = 0.0f;
	m_aPlayers[ 1 ].afColour[ 0 ] = BLUE_R;
	m_aPlayers[ 1 ].afColour[ 1 ] = BLUE_G;
	m_aPlayers[ 1 ].afColour[ 2 ] = BLUE_B;
	m_aPlayers[ 1 ].pCountHud     = &m_aCountHuds[ 1 ];
	m_aPlayers[ 1 ].pPortrait     = &m_aPortraits[ 1 ];
	m_aPlayers[ 1 ].iTeam         = ADartThrow::TEAM_BLUE;

	// Portraits
	CreatePortraitFromProfile( &m_aPortraits[ 0 ], AGUI2Portrait::FLAGS_LEFT | AGUI2Portrait::FLAGS_STOPWATCH | AGUI2Portrait::FLAGS_UNK5, &m_GUIElement );
	m_aPortraits[ 0 ].SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPLEFT );
	m_GUIElement.AddChildTail( m_aPortraits[ 0 ] );

	m_aPortraits[ 1 ].Create( "Root_Mug", AGUI2Portrait::FLAGS_RIGHT | AGUI2Portrait::FLAGS_STOPWATCH | AGUI2Portrait::FLAGS_UNK5, &m_GUIElement );
	m_aPortraits[ 1 ].SetAttachment( T2GUIATTACHMENT_TOPRIGHT, T2GUIATTACHMENT_TOPRIGHT );
	m_GUIElement.AddChildTail( m_aPortraits[ 1 ] );

	TFLOAT fCountY = m_aPortraits[ 0 ].GetHeight() * 1.7f;
	m_aCountHuds[ 0 ].SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPLEFT, fCountY );
	m_aCountHuds[ 1 ].SetAttachment( T2GUIATTACHMENT_TOPRIGHT, T2GUIATTACHMENT_TOPRIGHT, fCountY );
	m_aCountHuds[ 1 ].SetScale( 66.0f );
	m_aCountHuds[ 0 ].SetScale( 66.0f );

	// Time bonus texts
	m_aBonusTexts[ 0 ].SetAttachment( T2GUIATTACHMENT_TOPLEFT, T2GUIATTACHMENT_TOPLEFT );
	m_aBonusTexts[ 1 ].SetAttachment( T2GUIATTACHMENT_TOPRIGHT, T2GUIATTACHMENT_TOPRIGHT );
	m_aBonusTexts[ 0 ].SetTransform( m_aPortraits[ 0 ].GetWidth() * 0.2f, m_aPortraits[ 0 ].GetHeight() * 1.3f, 0.0f );
	m_aBonusTexts[ 1 ].SetTransform( m_aPortraits[ 0 ].GetWidth() * -0.2f, m_aPortraits[ 0 ].GetHeight() * 1.3f, 0.0f );
	m_aBonusTexts[ 0 ].SetFont( pFont );
	m_aBonusTexts[ 0 ].SetDimensionsDirty();
	m_aBonusTexts[ 1 ].SetFont( pFont );
	m_aBonusTexts[ 1 ].SetDimensionsDirty();
	m_aPlayers[ 1 ].pBonusText = &m_aBonusTexts[ 1 ];
	m_aPlayers[ 0 ].pBonusText = &m_aBonusTexts[ 0 ];
	m_GUIElement.AddChildTail( m_aBonusTexts[ 0 ] );
	m_GUIElement.AddChildTail( m_aBonusTexts[ 1 ] );

	m_aPortraits[ 0 ].SetNameColour( YELLOW_R, YELLOW_G, YELLOW_B );
	SetPortraitNameFromProfile( &m_aPortraits[ 0 ] );
	m_aPortraits[ 1 ].SetNameColour( BLUE_R, BLUE_G, BLUE_B );
	m_aPortraits[ 1 ].SetName( ALocaleManager::Interface()->GetString( 1071 ) );

	m_oFireHud.SetBullseyeVisible( TFALSE );

	AMiniGameState::OnInsertion();
	if ( ACoopMinigame::IsRealtimeDarts(this) )
	{
		m_bSubmitHighScore = TFALSE;
		m_aPortraits[0].SetName(AModStrings::Get(MODSTRING_COOPMINI_HOST_PLAYER));
		m_aPortraits[1].SetName(AModStrings::Get(MODSTRING_COOPMINI_GUEST_PLAYER));
		TINFO("[darts-pvp] init role=%s localSlot=%u humans=2 ai=%d rounds=%d dartsEach=%d\n",ACoopMinigame::IsRealtimeDartsHost() ? "host" : "guest",ACoopMinigame::GetRealtimeDartsLocalPlayer(),m_aPlayers[1].bComputer,NUM_ROUNDS,NUM_DARTS);
	}

	m_fSavedNear = ARenderer::GetSingleton()->GetNear();
	ARenderer::GetSingleton()->SetNear( 0.1f );

	SetupCamera();

	m_oDataTRB.Load( "Data/MiniGameData/Darts/dartsdata.trb" );
	m_pData  = m_oDataTRB.CastSymbol<ADartsRoundData>( "dartsdata" );
	m_iRound = 0;

	pHudManager->AddElement( &m_oRoundHud, TFALSE, TTRUE, TFALSE );
	m_oRoundHud.SetForceHide( TTRUE );
	m_oRoundHud.SetParent( &m_GUIElement );

	SetupRound( m_iRound );

	// No start HUD, the game starts as soon as the intro is over
	m_oStartHudElement.SetForceHide( TTRUE );

	AGameLoader::Terrain_Set( AGameLoader::Terrain_EnvSSDarts, TFALSE, TTRUE, 0, 0, 0, 0 );
}

// $Barnyard: FUNCTION 00522770
void ADartMiniGame::OnRemoval()
{
	RestoreCoopView();
	delete m_pCoopView;
	m_pCoopView = TNULL;
	AGameTimeManager::GetSingleton()->ChangePhaseOfDay( m_eSavedDayPhase, TTRUE, TTRUE );

	m_oRoundHud.Detach();
	m_oFireHud.Detach();
	m_oDataTRB.Close();
	m_oBoard.Destroy();
	m_aCountHuds[ 0 ].Detach();
	m_aCountHuds[ 1 ].Detach();
	m_oInteractionHud.Detach();
	m_aPortraits[ 0 ].Destroy();
	m_aPortraits[ 1 ].Destroy();

	for ( TINT i = 0; i < NUM_PLAYERS; i++ )
	{
		AUnitManager::GetSingleton()->DestroyUnit( m_aPlayers[ i ].pUnit );

		for ( TINT j = 0; j < NUM_DARTS; j++ )
			AUnitManager::GetSingleton()->DestroyUnit( m_aPlayers[ i ].aDarts[ j ].m_pUnit );
	}

	ARenderer::GetSingleton()->SetNear( m_fSavedNear );
	ACameraManager::GetSingleton()->GetCurrentCamera()->SetFOV( m_fSavedFOV );

	ASkyDome* pSkyDome = ARenderer::GetSingleton()->GetSkyDome();
	pSkyDome->SetFlags( pSkyDome->GetFlags() | ASkyDome::FLAGS_RENDER );
	pSkyDome->SetFlags( ( pSkyDome->GetFlags() & ~( ASkyDome::FLAGS_FROZEN | ASkyDome::FLAGS_ROTATE ) ) | ASkyDome::FLAGS_ROTATE );

	ASunFlareEffect::GetSingleton()->SetEnabled( TTRUE );

	AMiniGameState::OnRemoval();
}

// $Barnyard: FUNCTION 00522040
void ADartMiniGame::OnActivate()
{
	AMiniGameState::OnActivate();
}

// $Barnyard: FUNCTION 00522050
void ADartMiniGame::OnDeactivate()
{
	AMiniGameState::OnDeactivate();
}

// $Barnyard: FUNCTION 00522e80
void ADartMiniGame::Pause()
{
	PlayIntro();
}

// NOTE: inlined into Pause (00522e80) in the original
void ADartMiniGame::PlayIntro()
{
	AGameStateController::GetSingleton()->PushState(
	    new ASSeqState( "ssdart_intro", "ssdart_intro", "SSDART_CameraB", TVector4::VEC_ZERO, 1, TTRUE, TTRUE, TTRUE, TFALSE, TFALSE, TFALSE, TTRUE, TFALSE )
	);
}

// $Barnyard: FUNCTION 00522630
void ADartMiniGame::OnPreStart()
{
	SetVisible( TFALSE );
	AGameLoader::Terrain_Set( AGameLoader::Terrain_EnvSSDarts, TFALSE, TTRUE, 0, 0, 0, 0 );
}

// $Barnyard: FUNCTION 00522060
TBOOL ADartMiniGame::IsGameOver()
{
	return m_bGameOver;
}

// $Barnyard: FUNCTION 005231a0
void ADartMiniGame::UpdateGame( TFLOAT a_fDeltaTime )
{
	if ( ACoopMinigame::IsRealtimeDarts(this) )
	{
		if ( !ACoopMinigame::IsRealtimeDartsHost() ) return;
		PublishCoopState();
		const TUINT stage = GetCoopAimStage();
		const TUINT serial = GetCoopThrowSerial();
		TUINT reference = 0;
		if ( m_iCurrentPlayer == 1 && stage && ACoopMinigame::ConsumeDartsInput(serial,stage,&reference) )
		{
			TFLOAT& axis = stage == 1 ? m_oThrow.m_fAimY : m_oThrow.m_fAimX;
			if (reference && (!m_pCoopView || !m_pCoopView->aimHistory.Resolve(reference,m_pCoopView->hostMatch,serial,stage,DartsNowMs(),axis)))
				ACoopMinigame::DiscardDartsViewInput(serial,stage);
			else
			{
				if (reference)
				{
					GetThrowController(GetCurrentPlayer())->SetAim(m_oThrow.m_fAimX,m_oThrow.m_fAimY);
					UpdatePlayerFacing();
					m_oFireHud.SetBarX(-m_oThrow.m_fAimX);
					m_oFireHud.SetBarY(-m_oThrow.m_fAimY);
					m_oFireHud.SetBullseyePosition(-m_oThrow.m_fAimX,-m_oThrow.m_fAimY);
					TINFO("[darts-pvp] aim confirmed throw=%u stage=%u view=%u aim=%.5f\n",serial,stage,reference,axis);
				}
				if ( stage == 1 ) m_bFireX = TTRUE;
				else m_bFireY = TTRUE;
				TINFO("[darts-pvp] input native throw=%u owner=1 stage=%u\n",serial,stage);
			}
		}
	}
	// DOES_NOTHING (005acd80) is called on the board
	m_oBoard.UpdateTargets( a_fDeltaTime );

	if ( m_bCogsSpin )
		m_oBoard.UpdateCogs( a_fDeltaTime );

	for ( TINT i = 0; i < ADartBoard::NUM_RINGS; i++ )
		m_oBoard.RotateRing( i, m_oBoard.GetRingSpeed( i ) * a_fDeltaTime );

	for ( TINT i = 0; i < NUM_PLAYERS; i++ )
	{
		Player& rPlayer = m_aPlayers[ i ];

		rPlayer.fBonusAlpha -= BONUS_FADE_SPEED * a_fDeltaTime;

		if ( rPlayer.fBonusAlpha < 0.0f )
			rPlayer.fBonusAlpha = 0.0f;

		rPlayer.pBonusText->SetAlpha( rPlayer.fBonusAlpha );

		for ( TINT j = 0; j < NUM_DARTS; j++ )
		{
			ADart& rDart = rPlayer.aDarts[ j ];

			if ( !rDart.m_bOnTarget )
			{
				if ( rDart.m_iRing != ADartThrow::RING_NONE )
					rDart.UpdateSpin( m_oBoard.GetRingSpeed( rDart.m_iRing ) * a_fDeltaTime );

				rDart.Update();
			}
		}

		ADart::UpdateFalling( a_fDeltaTime, rPlayer.aDarts, NUM_DARTS );
	}

	TVector4 vTop;
	TVector4 vRight;
	TVector4 vLeft;
	TVector4 vBottom;
	ADartBoard::GetCorners( vTop, vRight, vLeft, vBottom );
	m_oFireHud.SetBoardCorners( vTop, vRight, vLeft, vBottom );

	if ( m_bFading )
	{
		if ( m_fFadeTimer > 0.9f && m_bFadeIn )
		{
			// NOTE: retail StartFade (0045cb20) has a 4th argument, TTRUE here, that our signature doesn't have yet
			AFadeManager::GetSingleton()->StartFade( AFade::Color( 255, 0, 0, 0 ), AFade::Color( 0, 0, 0, 0 ), 1.0f );
		}

		if ( m_fFadeTimer < 0.0f )
			m_bFading = TFALSE;

		m_fFadeTimer -= a_fDeltaTime;
		return;
	}

	Player& rPlayer = GetCurrentPlayer();

	if ( rPlayer.iCurrentDart >= NUM_DARTS || m_bGameOver )
	{
		// Wait until both players have thrown their darts
		TINT iNumDone = 0;

		for ( TINT i = 0; i < NUM_PLAYERS; i++ )
		{
			if ( m_aPlayers[ i ].iCurrentDart == NUM_DARTS )
				iNumDone++;
		}

		if ( iNumDone == NUM_PLAYERS )
		{
			for ( TINT i = 0; i < NUM_PLAYERS; i++ )
				m_aPlayers[ i ].aRoundScores[ m_iRound ] = m_aPlayers[ i ].iScore;

			if ( m_iRound == NUM_ROUNDS - 1 || (!ACoopMinigame::IsRealtimeDarts() && m_aPlayers[ 0 ].iScore <= m_aPlayers[ 1 ].iScore) )
			{
				m_bGameOver = TTRUE;
			}
			else
			{
				// Next round
				m_iRound += 1;
				SetupRound( m_iRound );
				m_oBoard.ResetTargets();

				// NOTE: retail StartFade (0045cb20) has a 4th argument, TTRUE here, that our signature doesn't have yet
				AFadeManager::GetSingleton()->StartFade( AFade::Color( 255, 0, 0, 0 ), AFade::Color( 0, 0, 0, 0 ), 1.0f );

				OnStartHudFinished();
			}
		}

		return;
	}

	ADart& rDart = GetCurrentDart();

	switch ( rDart.m_eState )
	{
		case ADart::STATE_IDLE:
		{
			m_oFireHud.SetColour( rPlayer.afColour[ 0 ], rPlayer.afColour[ 1 ], rPlayer.afColour[ 2 ] );
			m_oThrow.Reset();

			TSTATICCAST( AVisibleUnit, rDart.m_pUnit )->SetVisible( TFALSE );
			GetThrowController( rPlayer )->Raise();
			rPlayer.pPortrait->SetClockDuration( m_fAimTime + m_fAimTimeBonus );

			m_oThrow.m_fTimeUsed = 0.0f;
			TSTATICCAST( AAnimatedUnit, rPlayer.pUnit )->AttachUnit( rDart.m_pUnit, rPlayer.strDartBone );

			m_fRaiseTimer = 0.0f;
			m_bUnk1       = TFALSE;
			m_bUnk2       = TFALSE;

			const ADartsRoundData& rcData = GetRoundData();

			if ( rPlayer.bComputer )
			{
				m_fAimTime       = rcData.fAiAimTime;
				m_fAimTimeBonus  = rcData.fAiAimTimeBonus;
				m_oThrow.m_fAimX = 0.0f;
				m_oThrow.m_fAimY = 0.0f;
			}
			else
			{
				m_fAimTime       = rcData.fAimTime;
				m_fAimTimeBonus  = rcData.fAimTimeBonus;
				m_oThrow.m_fAimX = 1.0f;
				m_oThrow.m_fAimY = 1.0f;
			}

			rPlayer.pPortrait->ResetClock( TFALSE );
			rPlayer.pPortrait->Show();
			rPlayer.pPortrait->SetClockDuration( m_fAimTime + m_fAimTimeBonus );
			rPlayer.pPortrait->SetClockTime( m_fAimTime + m_fAimTimeBonus );
			rPlayer.pPortrait->StartClockFade( 0.0f, 0.5f, 0.5f );

			UpdatePlayerFacing();
			GetCurrentDart().m_eState = ADart::STATE_RAISE;
			return;
		}
		case ADart::STATE_AIM_X:
			UpdateThrowTime( m_oThrow, m_fAimTime + m_fAimTimeBonus, a_fDeltaTime );

			if ( GetRoundData().fAimFrequencyX + m_fAimTime + m_fAimTimeBonus < m_fAimTimerX || m_bFireX )
			{
				ASoundManager::GetSingleton()->PlayCue( soundbank::DARTS_PING );
				m_oFireHud.SetBarXVisible( TTRUE );
				m_oInteractionHud.SetText( ALocaleManager::Interface()->GetString( 782 ), 2, TINT( m_pInputMap ), AInputCommand_Interact, 0, 0xFFFFFFFF, -1.0f );
				GetCurrentDart().m_eState = ADart::STATE_AIM_Y;
				m_oFireHud.SetBullseyeVisible( TTRUE );
				m_oFireHud.SetBullseyePosition( m_oThrow.m_fAimX * -1.0f, m_oThrow.m_fAimY * -1.0f );

				if ( m_iCurrentPlayer == 0 )
					m_oFireHud.ResetColour();
			}
			else
			{
				UpdateAimX( m_oThrow, a_fDeltaTime );
			}

			return;
		case ADart::STATE_AIM_Y:
			UpdateThrowTime( m_oThrow, m_fAimTime + m_fAimTimeBonus, a_fDeltaTime );

			if ( m_fAimTime + m_fAimTimeBonus < m_fAimTimerX + m_fAimTimerY || m_bFireY )
			{
				m_oFireHud.SetBarXLocked( TTRUE );
				m_oInteractionHud.SetText( TNULL, 2, 0, -1, 0, 0xFFFFFFFF, -1.0f );
				m_oInteractionHud.SetForceHide( TTRUE );
				ASoundManager::GetSingleton()->PlayCue( soundbank::DARTS_PING );
				GetCurrentDart().m_eState = ADart::STATE_THROW;

				if ( m_iCurrentPlayer == 0 )
					m_oFireHud.ResetColour();
			}
			else
			{
				UpdateAimY( m_oThrow, a_fDeltaTime );
			}

			return;
		case ADart::STATE_THROW:
		{
			m_oFireHud.SetColour( rPlayer.afColour[ 0 ], rPlayer.afColour[ 1 ], rPlayer.afColour[ 2 ] );
			GetThrowController( rPlayer )->Throw();
			ASoundManager::GetSingleton()->PlayCueAtPosition( soundbank::DARTS_THROW01, *rPlayer.pUnit->GetSteer()->GetPosition() );

			m_oThrow.ComputeDirection();
			m_oBoard.ComputeTargetEx( m_oThrow );

			TVector4 vPosition;
			m_oAimDart.GetPosition( vPosition );
			m_oFireHud.SetTargetLockPosition( vPosition );

			m_fFlightTime             = 0.0f;
			GetCurrentDart().m_eState = ADart::STATE_THROWING;
			return;
		}
		case ADart::STATE_AI_START:
		{
			const ADartsRoundData& rcData  = GetRoundData();
			TRandom&               rRandom = ARandom::GetSingleton()->m_oRandom;

			m_fAiFireTimeY = rRandom.GetFloatMinMax( rcData.fAiFireTimeYMin, rcData.fAiFireTimeYMax );
			m_fAiFireTimeX = rRandom.GetFloatMinMax( rcData.fAiFireTimeXMin, rcData.fAiFireTimeXMax );
			m_fAiErrorX    = rRandom.GetFloatMinMax( rcData.fAiErrorX * -0.01f, rcData.fAiErrorX * 0.01f );
			m_fAiErrorY    = rRandom.GetFloatMinMax( rcData.fAiErrorY * -0.01f, rcData.fAiErrorY * 0.01f );

			GetCurrentDart().m_eState = ADart::STATE_AI_TARGET;
			m_oFireHud.SetBarYVisible( TFALSE );
			return;
		}
		case ADart::STATE_AI_TARGET:
			if ( m_oBoard.FindBestTarget( rPlayer.iTeam ) == ADartThrow::TARGET_NONE )
			{
				// Nothing to aim at, throw anywhere
				m_fAiTargetX = ARandom::GetSingleton()->m_oRandom.GetFloatMinMax( -1.0f, 1.0f );
				m_fAiTargetY = ARandom::GetSingleton()->m_oRandom.GetFloatMinMax( -1.0f, 1.0f );
			}

			GetCurrentDart().m_eState = ADart::STATE_AI_MOVE;
			m_fAiTimer                = 0.0f;
			return;
		case ADart::STATE_AI_MOVE:
		{
			TFLOAT fMoveTime = m_fAiFireTimeX + 0.08f + m_fAiFireTimeY + 0.08f;

			if ( fMoveTime > m_fAiTimer )
			{
				TINT iTarget = m_oBoard.FindBestTarget( rPlayer.iTeam );

				if ( iTarget != ADartThrow::TARGET_NONE )
				{
					// Aim where the target will be when the dart lands
					TVector4 vPosition;
					m_oBoard.GetTargetPosition( vPosition, iTarget, m_fFlightTime + 0.08f );
					ADartBoard::GetAimFromPosition( vPosition, m_fAiTargetX, m_fAiTargetY );
				}

				TFLOAT fInvMoveTime = 1.0f / fMoveTime;
				m_oThrow.m_fAimX    = TMath::LERP( 0.0f, m_fAiErrorX + m_fAiTargetX, fInvMoveTime * m_fAiTimer );
				m_oThrow.m_fAimY    = TMath::LERP( 0.0f, m_fAiErrorY + m_fAiTargetY, fInvMoveTime * m_fAiTimer );
				m_fAiTimer += a_fDeltaTime;
			}
			else
			{
				m_oThrow.m_fAimX = m_fAiTargetX;
				m_oThrow.m_fAimY = m_fAiTargetY;
				rDart.m_eState   = ADart::STATE_THROW;
			}

			UpdatePlayerFacing();
			UpdateThrowTime( m_oThrow, m_fAimTimeBonus + m_fAimTime, a_fDeltaTime );
			return;
		}
		case ADart::STATE_AI_AIM_X:
		{
			const ADartsRoundData& rcData = GetRoundData();

			TBOOL  bFire    = TFALSE;
			TFLOAT fCos     = TMath::Cos( TMath::PI * rcData.fAimFrequencyX * m_fAimTimerX );
			TFLOAT fCosNext = TMath::Cos( ( a_fDeltaTime + m_fAimTimerX ) * rcData.fAimFrequencyX * TMath::PI );

			UpdateThrowTime( m_oThrow, m_fAimTime + m_fAimTimeBonus, a_fDeltaTime );

			TINT iTarget = m_oBoard.FindClosestTarget( rPlayer.iTeam, m_oThrow.m_fAimX, ( fCosNext + fCos ) * 0.5f );

			if ( m_fAimTimerX > m_fAiFireTimeX )
			{
				if ( iTarget == ADartThrow::TARGET_NONE )
				{
					bFire = TTRUE;
				}
				else
				{
					TVector4 vPosition;
					m_oBoard.GetTargetPosition( vPosition, iTarget, 0.1f );
					ADartBoard::GetAimFromPosition( vPosition, m_fAiTargetX, m_fAiTargetY );

					TFLOAT fTolerance = TMath::Abs( fCosNext - fCos ) + TMath::Abs( m_fAiErrorY );

					if ( TMath::Abs( m_fAiTargetY - fCosNext ) < fTolerance &&
					     TMath::Abs( m_fAiTargetY - fCos ) < fTolerance &&
					     m_oBoard.WillHitTarget( iTarget, fCos + m_fAiErrorY, m_fAiTargetX, 0.1f ) )
					{
						bFire = TTRUE;
					}
				}
			}

			if ( m_fAimTime + m_fAimTimeBonus < m_fAimTimerX )
				bFire = TTRUE;

			UpdateAimX( m_oThrow, a_fDeltaTime );

			if ( bFire == TTRUE )
			{
				ASoundManager::GetSingleton()->PlayCue( soundbank::DARTS_PING );
				m_oFireHud.SetBarXVisible( TTRUE );
				m_oFireHud.SetBullseyeVisible( TTRUE );
				m_oFireHud.SetBullseyePosition( m_oThrow.m_fAimX * -1.0f, m_oThrow.m_fAimY * -1.0f );
				GetCurrentDart().m_eState = ADart::STATE_AI_AIM_Y;
			}

			return;
		}
		case ADart::STATE_AI_AIM_Y:
		{
			TBOOL bFire = TFALSE;
			UpdateThrowTime( m_oThrow, m_fAimTime + m_fAimTimeBonus, a_fDeltaTime );

			if ( m_fAimTimerY > m_fAiFireTimeY )
			{
				TINT iTarget = m_oBoard.FindClosestTarget( rPlayer.iTeam, m_oThrow.m_fAimX, m_oThrow.m_fAimY );

				if ( iTarget == ADartThrow::TARGET_NONE )
				{
					bFire = TTRUE;
				}
				else
				{
					TVector4 vBefore;
					TVector4 vNow;
					TVector4 vAfter;
					m_oBoard.GetTargetPosition( vBefore, iTarget, 0.1f - a_fDeltaTime );
					m_oBoard.GetTargetPosition( vNow, iTarget, 0.1f );
					m_oBoard.GetTargetPosition( vAfter, iTarget, a_fDeltaTime + 0.1f );

					TFLOAT fBeforeX;
					TFLOAT fBeforeY;
					TFLOAT fAfterX;
					TFLOAT fAfterY;
					ADartBoard::GetAimFromPosition( vBefore, fBeforeX, fBeforeY );
					ADartBoard::GetAimFromPosition( vNow, m_fAiTargetX, m_fAiTargetY );
					ADartBoard::GetAimFromPosition( vAfter, fAfterX, fAfterY );

					TFLOAT fAimX = m_oThrow.m_fAimX;

					// The target passes the aim this frame
					if ( ( fBeforeX + m_fAiErrorX + AI_TOLERANCE > fAimX && m_fAiErrorX + fAimX + AI_TOLERANCE > fAfterX ) ||
					     ( m_fAiErrorX + fAimX + AI_TOLERANCE > fBeforeX && fAfterX + m_fAiErrorX + AI_TOLERANCE > fAimX ) )
					{
						if ( AI_TOLERANCE + m_fAiErrorY > TMath::Abs( m_fAiTargetY - m_oThrow.m_fAimY ) )
							bFire = TTRUE;
					}
				}
			}

			if ( m_fAimTime + m_fAimTimeBonus < m_fAimTimerX + m_fAimTimerY || bFire == TTRUE )
			{
				ASoundManager::GetSingleton()->PlayCue( soundbank::DARTS_PING );
				GetCurrentDart().m_eState = ADart::STATE_THROW;
			}
			else
			{
				UpdateAimY( m_oThrow, a_fDeltaTime );
				m_oFireHud.SetBullseyePosition( m_oThrow.m_fAimX * -1.0f, m_oThrow.m_fAimY * -1.0f );
			}

			return;
		}
		case ADart::STATE_THROWING:
		{
			TVector4 vPosition;
			m_oAimDart.GetPosition( vPosition );
			m_oFireHud.SetTargetLockPosition( vPosition );
			return;
		}
		case ADart::STATE_RAISE:
			m_fRaiseTimer += a_fDeltaTime;
			rPlayer.pPortrait->SetClockVisible( TTRUE );

			if ( m_fRaiseTimer > GetRoundData().fRaiseTime )
			{
				TSTATICCAST( AVisibleUnit, GetCurrentDart().m_pUnit )->SetVisible( TTRUE );

				m_fAimTimerY = 0.0f;
				m_fAimTimerX = 0.0f;
				m_bFireY     = TFALSE;
				m_bFireX     = TFALSE;

				GetThrowController( GetCurrentPlayer() )->SetPlayIdle( TTRUE );
				GetThrowController( GetCurrentPlayer() )->SetAim( 0.0f, 0.0f );

				if ( GetCurrentPlayer().bComputer )
				{
					GetCurrentDart().m_eState = ADart::STATE_AI_START;
				}
				else
				{
					m_oFireHud.SetBarXVisible( TFALSE );
					m_oFireHud.SetBarX( -1.0f );
					m_oFireHud.SetBarY( -1.0f );
					m_oFireHud.SetBarYVisible( TTRUE );
					m_oInteractionHud.SetForceHide( TFALSE );
					m_oInteractionHud.SetText( ALocaleManager::Interface()->GetString( 783 ), 2, TINT( m_pInputMap ), AInputCommand_Interact, 0, 0xFFFFFFFF, -1.0f );
					GetCurrentDart().m_eState = ADart::STATE_AIM_X;
				}
			}

			return;
		case ADart::STATE_FLYING:
		{
			TVector4 vLockPosition;
			m_oAimDart.GetPosition( vLockPosition );
			m_oFireHud.SetTargetLockPosition( vLockPosition );

			m_fFlightTime += a_fDeltaTime;
			TFLOAT fProgress = m_fFlightTime * 10.0f;

			TVector4 vPosition;
			vPosition.Lerp4( m_vReleasePosition, m_oThrow.m_vTarget, ( fProgress >= 1.0f ) ? 1.0f : fProgress );
			GetCurrentDart().m_pUnit->GetSteer()->SetPosition( vPosition );

			TVector4 vDirection = m_oThrow.m_vTarget - m_vReleasePosition;
			TFLOAT   fDistance  = TVector4::Distance( m_oThrow.m_vTarget, m_vReleasePosition );
			vDirection.Normalise();

			// The dart gets bigger as it flies away
			TVector4 vDartPosition;
			GetCurrentDart().GetPosition( vDartPosition );
			GetCurrentDart().m_fScale = TVector4::Distance( m_vReleasePosition, vDartPosition ) / fDistance * 9.0f + 1.0f;
			GetCurrentDart().SetDirection( vDirection );

			if ( m_fFlightTime <= 0.1f )
				return;

			// The dart has landed
			m_oBoard.Score( m_oThrow );
			GetCurrentDart().m_iScore = m_oThrow.m_iScore;
			m_fResultTimer            = 0.0f;
			GetCurrentDart().m_eState = ADart::STATE_RESULT;

			rPlayer.pPortrait->StartClockFade( 0.5f, 0.0f, 0.5f );
			m_oFireHud.SetBarXVisible( TFALSE );
			m_oFireHud.SetBarYVisible( TFALSE );
			m_oFireHud.SetBullseyeVisible( TFALSE );

			ASoundManager::GetSingleton()->PlayCueAtPosition( soundbank::DARTS_IMPACT, *GetCurrentDart().m_pUnit->GetSteer()->GetPosition() );

			TFLOAT fRed   = 1.0f;
			TFLOAT fGreen = 1.0f;
			TFLOAT fBlue  = 1.0f;

			if ( m_oThrow.m_iTeam == ADartThrow::TEAM_YELLOW )
			{
				fGreen = YELLOW_G;
				fBlue  = YELLOW_B;
			}
			else if ( m_oThrow.m_iTeam == ADartThrow::TEAM_BLUE )
			{
				fRed   = BLUE_R;
				fGreen = BLUE_G;
				fBlue  = BLUE_B;
			}

			const TWCHAR* wszHitText = m_oBoard.GetHitText( m_oThrow, rPlayer.iTeam );
			m_oFireHud.ShowScore( m_oThrow.m_fAimX * -1.0f, m_oThrow.m_fAimY * -1.0f, m_oThrow.m_iScore, wszHitText, fRed, fGreen, fBlue );

			if ( m_oThrow.m_iTarget == ADartThrow::TARGET_NONE )
			{
				GetCurrentDart().m_iRing = m_oThrow.m_iRing;
			}
			else
			{
				if ( rPlayer.iTeam == m_oThrow.m_iTeam )
				{
					// Own target: points and a time bonus
					ASoundManager::GetSingleton()->PlayCue( soundbank::UI_DING );

					if ( ADartThrow::IsBullseye( m_oThrow ) )
						ASoundManager::GetSingleton()->PlayCue( soundbank::DARTS_CHEER_GOOD );

					rPlayer.aCategoryScores[ m_oThrow.m_iCategory ] += m_oThrow.m_iScore;
					rPlayer.iScore += m_oThrow.m_iScore;

					const ADartsRoundData& rcFirstRound = m_pData[ 0 ];

					TFLOAT fTimeLeft = m_fAimTimeBonus + m_fAimTime;

					if ( rcFirstRound.fUnk40 / rcFirstRound.fAimFrequencyY + rcFirstRound.fUnk40 / rcFirstRound.fAimFrequencyX <= m_oThrow.m_fTimeUsed )
						fTimeLeft -= m_oThrow.m_fTimeUsed;

					TFLOAT fBonus = fTimeLeft * ( ( m_iCurrentPlayer != 0 && !ACoopMinigame::IsRealtimeDarts() ) ? rcFirstRound.fTimeBonusPlayer2 : rcFirstRound.fTimeBonusPlayer1 );
					TINT   iBonus = ( fmod( fBonus, 1.0 ) <= 0.5 ) ? TMath::FloorToInt( fBonus ) : TMath::CeilToInt( fBonus );

					if ( iBonus < 0 )
						iBonus = 0;

					ShowTimeBonus( m_iCurrentPlayer, iBonus );
					rPlayer.iTimeBonus += iBonus;
					rPlayer.iScore += iBonus;
					rPlayer.pPortrait->SetScore( rPlayer.iScore );
				}
				else
				{
					// The other player's target: the points go to them
					ASoundManager::GetSingleton()->PlayCue( soundbank::UI_NEGATIVE );

					Player& rOther = m_aPlayers[ ( m_iCurrentPlayer + 1 ) % NUM_PLAYERS ];
					rOther.iScore += m_oThrow.m_iScore;
					rOther.pPortrait->SetScore( rOther.iScore );
				}

				m_oBoard.KnockOffTarget( m_oThrow.m_iTarget, &GetCurrentDart() );
			}

			if ( ACoopMinigame::IsRealtimeDartsHost() )
			{
				if ( !m_pCoopView ) m_pCoopView = new CoopView;
				m_pCoopView->lastHitSerial = GetCoopThrowSerial();
				m_pCoopView->lastHitOwner = m_iCurrentPlayer;
				m_pCoopView->hitScore = m_oThrow.m_iScore;
				m_pCoopView->hitTarget = m_oThrow.m_iTarget;
				m_pCoopView->hitTeam = m_oThrow.m_iTeam;
				m_pCoopView->publishWait = 0;
				PublishCoopState();
				TINFO("[darts-pvp] hit native throw=%u owner=%d round=%d dart=%d target=%d team=%d points=%d scores=%d,%d totals=%d,%d\n",GetCoopThrowSerial(),m_iCurrentPlayer,m_iRound + 1,rPlayer.iCurrentDart + 1,m_oThrow.m_iTarget,m_oThrow.m_iTeam,m_oThrow.m_iScore,m_aPlayers[0].iScore,m_aPlayers[1].iScore,GetCoopTotal(0),GetCoopTotal(1));
			}
			else if ( ACoopMinigame::IsPlaying() )
			{
				TINT score = m_aPlayers[0].iScore;
				for ( TINT round = 0; round < m_iRound; ++round ) score += m_aPlayers[0].aRoundScores[round];
				ACoopMinigame::SetScore( score );
			}

			PlayHitSound( m_iCurrentPlayer, m_oThrow );

			if ( m_oThrow.m_iScore > 0 || GetCurrentDart().m_iRing != ADartThrow::RING_NONE || m_oThrow.m_iCog != ADartThrow::COG_NONE )
			{
				GetCurrentDart().SetSpinning( TTRUE );

				// Hitting a cog drops the dart
				if ( m_oThrow.m_iCog != ADartThrow::COG_NONE )
					GetCurrentDart().Drop();
			}

			if ( m_oThrow.m_iScore == 0 )
				ASoundManager::GetSingleton()->PlayCue( soundbank::DARTS_CHEER_BAD );


			return;
		}
		case ADart::STATE_RESULT:
		{
			TVector4 vPosition;
			m_oAimDart.GetPosition( vPosition );
			m_oFireHud.SetTargetLockPosition( vPosition );

			m_fResultTimer += a_fDeltaTime;
			m_oFireHud.SetTargetLockAlpha( TMath::Max( 1.0f - ( m_fResultTimer + m_fResultTimer ), 0.0f ) );
			m_oFireHud.SetScoreAlpha( TMath::Max( 1.0f - m_fResultTimer, 0.0f ) );

			if ( m_fResultTimer > 1.0f )
			{
				m_oAimDart.SetSpinning( TFALSE );
				GetCurrentDart().m_eState = ADart::STATE_IDLE;

				GetCurrentPlayer().pPortrait->ResetClock( TFALSE );
				GetCurrentPlayer().pPortrait->SetClockVisible( TFALSE );

				TINT& riCurrentDart = GetCurrentPlayer().iCurrentDart;

				if ( riCurrentDart >= NUM_DARTS - 1 )
				{
					riCurrentDart = NUM_DARTS;
				}
				else
				{
					riCurrentDart += 1;
					GetCurrentDart().m_eState = ADart::STATE_IDLE;

					m_oFireHud.SetBarXVisible( TFALSE );
					m_oFireHud.SetBarYVisible( TFALSE );
					m_oFireHud.SetTargetLockVisible( TFALSE );
					m_oFireHud.SetTargetLockAlpha( 1.0f );
				}

				// Next player
				SetPlayerActive( m_iCurrentPlayer, TFALSE );
				m_iCurrentPlayer = ( m_iCurrentPlayer + 1 ) % NUM_PLAYERS;
				SetPlayerActive( m_iCurrentPlayer, TTRUE );
				if ( ACoopMinigame::IsRealtimeDartsHost() )
				{
					PublishCoopState();
					if ( m_pCoopView ) m_pCoopView->publishWait = 0;
					TINFO("[darts-pvp] next throw=%u owner=%d round=%d used=%d,%d\n",GetCoopThrowSerial(),m_iCurrentPlayer,m_iRound + 1,m_aPlayers[0].iCurrentDart,m_aPlayers[1].iCurrentDart);
				}
			}

			return;
		}
		default:
			return;
	}
}

// $Barnyard: FUNCTION 005228e0
void ADartMiniGame::UpdateGameOver( TFLOAT a_fDeltaTime )
{
	if ( ACoopMinigame::IsRealtimeDarts(this) )
	{
		if ( ACoopMinigame::IsRealtimeDartsHost() && !m_bGameOverPushed )
		{
			m_bGameOverPushed = TTRUE;
			if ( m_pCoopView ) m_pCoopView->publishWait = 0;
			TINFO("[darts-pvp] complete rounds=%d throws=60 totals=%d,%d\n",m_iRound + 1,GetCoopTotal(0),GetCoopTotal(1));
			ACoopMinigame::CompleteDartsMatch(GetCoopTotal(0),GetCoopTotal(1));
		}
		return;
	}
	if ( !m_bGameOverPushed )
	{
		if ( ACoopMinigame::IsPlaying() )
			TINFO("[coopmini] Darts native end round=%d human=%d ai=%d\n", m_iRound + 1, m_aPlayers[0].iScore, m_aPlayers[1].iScore);
		ShowGameOver( TTRUE ); // Native result; LAN ends this run, offline retains the original result screen.
		m_bGameOverPushed = TTRUE;
	}
}

// $Barnyard: FUNCTION 00523170
void ADartMiniGame::UpdatePreStart( TFLOAT a_fDeltaTime )
{
	SetupCamera();
	m_GUIElement.Hide();
	FacePlayersToCamera();
}

// $Barnyard: FUNCTION 00524b80
AMiniGameState::S1* ADartMiniGame::GetResults()
{
	m_oResults.ComputeTotal( 0 );

	if ( m_aPlayers[ 0 ].iScore <= m_aPlayers[ 1 ].iScore )
	{
		// NOTE: retail stores -1 and then 0
		m_oResults.iResult                = -1;
		m_oResults.iResult                = 0;
		m_oResults.bWon                   = m_iRound != 0;
		m_oResults.oPlayerList[ 1 ].bUnk1 = TFALSE;
	}
	else
	{
		m_oResults.iResult = 0;
		m_oResults.bWon    = TTRUE;
	}

	PlayerInfo& rInfo                 = m_oResults.oPlayerList[ 0 ];
	rInfo.iUnk2                       = m_aPlayers[ 0 ].iScore;
	m_oResults.oPlayerList[ 1 ].iUnk2 = m_aPlayers[ 1 ].iScore;
	rInfo.bUnk1                       = TTRUE;
	m_oResults.oPlayerList[ 1 ].bUnk1 = TFALSE;
	m_oResults.iMiniGameIndex         = AMiniGameManager::GetSingleton()->GetNextMiniGameIndex();

	while ( rInfo.vecRows.Size() != 0 )
	{
		rInfo.vecRows[ rInfo.vecRows.Size() - 1 ] = ResultRow();
		rInfo.vecRows.PopBack();
	}

	// One row per round played
	TINT iTotal = 0;

	for ( TINT i = 0; i < NUM_ROUNDS; i++ )
	{
		if ( m_iRound >= i )
		{
			ALocale::FormatTags( m_awszRoundNames[ i ], 64, 994, PPropertyValue( i + 1 ) );
			rInfo.vecRows.PushBack( ResultRow{ m_awszRoundNames[ i ], m_aPlayers[ 0 ].aRoundScores[ i ] } );
			iTotal += m_aPlayers[ 0 ].aRoundScores[ i ];
		}
	}

	rInfo.iTotal = iTotal;
	m_oResults.ComputeTotal( 0 );

	if ( m_bSubmitHighScore )
	{
		m_bSubmitHighScore = TFALSE;

		if ( AMiniGameManager::GetSingleton()->GetMiniGameVariant() == 0 )
			AHighScoreManager::AddScore( HIGHSCORE_TABLE, TFLOAT( rInfo.iTotal ), m_oResults.bWon, AHighScoreManager::PLAYER_1, TFALSE );
	}

	return &m_oResults;
}

// $Barnyard: FUNCTION 00522f80
void ADartMiniGame::OnStartHudFinished()
{
	AMiniGameState::OnStartHudFinished();

	m_GUIElement.Show();
	SetupCamera();

	for ( TINT i = 0; i < NUM_PLAYERS; i++ )
	{
		Player& rPlayer = m_aPlayers[ i ];

		rPlayer.iScore       = 0;
		rPlayer.iCurrentDart = 0;
		rPlayer.iTimeBonus   = 0;
		rPlayer.pPortrait->SetScore( 0 );

		rPlayer.aCategoryScores[ 0 ] = 0;
		rPlayer.aCategoryScores[ 1 ] = 0;
		rPlayer.aCategoryScores[ 2 ] = 0;

		rPlayer.pCountHud->SetCount( NUM_DARTS );

		for ( TINT j = 0; j < NUM_DARTS; j++ )
		{
			TSTATICCAST( AVisibleUnit, rPlayer.aDarts[ j ].m_pUnit )->SetVisible( TFALSE );
			rPlayer.aDarts[ j ].Reset();
		}

		TSTATICCAST( AVisibleUnit, rPlayer.pUnit )->SetVisible( TTRUE );

		rPlayer.pPortrait->SetNameAlpha( INACTIVE_ALPHA );
		rPlayer.pCountHud->FadeTo( INACTIVE_ALPHA, FADE_DURATION );
		rPlayer.pCountHud->SetCount( NUM_DARTS - rPlayer.iCurrentDart );
		rPlayer.pPortrait->SetClockEnabled( TFALSE );
		rPlayer.pPortrait->SetClockVisible( TFALSE );
	}

	m_iCurrentPlayer = 0;

	Player& rFirst = m_aPlayers[ 0 ];
	rFirst.pPortrait->SetNameAlpha( 1.0f );
	rFirst.pCountHud->FadeTo( 1.0f, FADE_DURATION );
	rFirst.pCountHud->SetCount( NUM_DARTS - rFirst.iCurrentDart );
	rFirst.pPortrait->SetClockEnabled( TTRUE );
	rFirst.pPortrait->SetClockVisible( TFALSE );

	Player& rCurrent = GetCurrentPlayer();
	m_oFireHud.SetColour( rCurrent.afColour[ 0 ], rCurrent.afColour[ 1 ], rCurrent.afColour[ 2 ] );

	m_fFadeTimer = 3.0f;
	m_bFading    = TTRUE;

	m_oRoundHud.SetDuration( 2.0f );
	m_oRoundHud.Start( TFALSE, m_iRound + 1 );

	m_bGameOver       = TFALSE;
	m_bGameOverPushed = TFALSE;
}

// $Barnyard: FUNCTION 005224a0
void ADartMiniGame::OnUnitAction( AAnimationBreakpointUnitAction::Event* a_pEvent )
{
	const TBOOL realtime = ACoopMinigame::IsRealtimeDarts(this);
	if ( (m_pCoopView && m_pCoopView->saved) || (realtime && !ACoopMinigame::IsRealtimeDartsHost()) ||
	     (!realtime && ACoopMinigame::BlockInput()) ) return; // Display/restore animation must not release a local dart.
	if ( a_pEvent->eAction != AAnimationBreakpointUnitAction::ACTION_HD_THROW )
		return;
	if ( GetCurrentPlayer().iCurrentDart >= NUM_DARTS ) return;

	Player& rPlayer = GetCurrentPlayer();
	ADart&  rDart   = GetCurrentDart();

	if ( rDart.m_eState != ADart::STATE_THROWING )
		return;

	// The dart leaves the hand
	TSTATICCAST( AAnimatedUnit, rPlayer.pUnit )->DetachUnit( rDart.m_pUnit, TTRUE, TFALSE );
	rPlayer.pCountHud->SetCount( NUM_DARTS - 1 - rPlayer.iCurrentDart );

	TSTATICCAST( AVisibleUnit, rDart.m_pUnit )->GetModelInstance()->GetPosition( m_vReleasePosition );
	rDart.m_pUnit->GetSteer()->SetPosition( m_vReleasePosition );
	rDart.SetDirection( m_oThrow.m_vDirection );

	GetCurrentDart().m_eState = ADart::STATE_FLYING;
}

// $Barnyard: FUNCTION 00522070
void ADartMiniGame::SetupRound( TINT a_iRound )
{
	// NOTE: the round passed isn't used, m_iRound is
	const ADartsRoundData& rcData = GetRoundData();

	m_bRoundFlag = rcData.iUnk == 1;

	m_oBoard.GetRing( ADartBoard::RING_INNER )->fAngle  = 0.0f;
	m_oBoard.GetRing( ADartBoard::RING_MIDDLE )->fAngle = 0.0f;
	m_oBoard.GetRing( ADartBoard::RING_OUTER )->fAngle  = 0.0f;

	m_oBoard.GetRing( ADartBoard::RING_INNER )->fSpeed  = GetRoundData().fInnerRingSpeed * TMath::DegToRad( 1.0f );
	m_oBoard.GetRing( ADartBoard::RING_MIDDLE )->fSpeed = GetRoundData().fMiddleRingSpeed * TMath::DegToRad( 1.0f );
	m_oBoard.GetRing( ADartBoard::RING_OUTER )->fSpeed  = GetRoundData().fOuterRingSpeed * TMath::DegToRad( 1.0f );

	m_oBoard.SetScoreMultiplier( TMath::CeilToInt( GetRoundData().fScoreMultiplier ) );

	if ( TMath::Abs( GetRoundData().fOuterRingSpeed ) > 0.1f )
		m_bCogsSpin = TTRUE;
}

// $Barnyard: FUNCTION 005221a0
void ADartMiniGame::UpdatePlayerFacing()
{
	ASteer* pSteer = GetCurrentPlayer().pUnit->GetSteer();

	m_oThrow.ComputeDirection();

	static TVector4 s_vForward = TVector4::VEC_POSX;

	TVector4 vThrow;
	vThrow.Multiply( m_oThrow.m_vDirection, 0.25f );

	TVector4 vForward;
	vForward.Multiply( s_vForward, 1.0f - 0.25f );

	TVector4 vDirection( vThrow.x + vForward.x, vThrow.y + vForward.y, vThrow.z + vForward.z, vThrow.w );
	vDirection.Normalise();

	pSteer->SetDirection( vDirection );
}

// $Barnyard: FUNCTION 00522300
void ADartMiniGame::UpdateThrowTime( ADartThrow& a_rThrow, TFLOAT a_fTime, TFLOAT a_fDeltaTime )
{
	a_rThrow.m_fTimeUsed += a_fDeltaTime;
	GetCurrentPlayer().pPortrait->SetClockTime( a_fTime - a_rThrow.m_fTimeUsed );
}

// $Barnyard: FUNCTION 00522340
void ADartMiniGame::SetPlayerActive( TINT a_iPlayer, TBOOL a_bActive )
{
	TFLOAT  fAlpha  = a_bActive ? 1.0f : INACTIVE_ALPHA;
	Player& rPlayer = m_aPlayers[ a_iPlayer ];

	rPlayer.pPortrait->SetNameAlpha( fAlpha );
	rPlayer.pCountHud->FadeTo( fAlpha, FADE_DURATION );
	rPlayer.pCountHud->SetCount( NUM_DARTS - rPlayer.iCurrentDart );
	rPlayer.pPortrait->SetClockEnabled( a_bActive );
	rPlayer.pPortrait->SetClockVisible( TFALSE );
}

// $Barnyard: FUNCTION 005223d0
void ADartMiniGame::ShowTimeBonus( TINT a_iPlayer, TINT a_iBonus )
{
	Player& rPlayer = m_aPlayers[ a_iPlayer ];

	TStringManager::String16Format( rPlayer.wszBonus, 24, L"%d", a_iBonus );
	rPlayer.pBonusText->SetTextAndFitWidth( rPlayer.wszBonus );
	rPlayer.pBonusText->SetColour( MakeColour( rPlayer.afColour ) );

	rPlayer.fBonusAlpha = 1.0f;
	rPlayer.pBonusText->SetAlpha( 1.0f );
}

// $Barnyard: FUNCTION 005225f0
void ADartMiniGame::SetVisible( TBOOL a_bVisible )
{
	for ( TINT i = 0; i < NUM_PLAYERS; i++ )
		TSTATICCAST( AVisibleUnit, m_aPlayers[ i ].pUnit )->SetVisible( a_bVisible );

	m_oBoard.SetSkeletonUpdating( a_bVisible );
}

// $Barnyard: FUNCTION 00522650
ADartThrowAnimController* ADartMiniGame::GetThrowController( Player& a_rPlayer )
{
	return TSTATICCAST( ADartThrowAnimController, TSTATICCAST( AAnimatedUnit, a_rPlayer.pUnit )->GetAnimatableObject()->FindAnimController( &TGetClass( ADartThrowAnimController ) ) );
}

// $Barnyard: FUNCTION 00522970
void ADartMiniGame::SetupCamera()
{
	ACamera* pCamera = ACameraManager::GetSingleton()->GetCurrentCamera();

	static TVector4 s_vPosition = CAMERA_POSITION;

	TVector4 vZero = TVector4::VEC_ZERO;

	TVector4 vDirection( vZero.x - s_vPosition.x, vZero.y - s_vPosition.y, vZero.z - s_vPosition.z, vZero.w );
	vDirection.Normalise();

	if ( s_bSetupCamera )
	{
		pCamera->GetMatrix().LookAtDirection( vDirection, TVector4::VEC_NEGY );
		pCamera->GetMatrix().SetTranslation( s_vPosition );
		pCamera->SetFOV( TMath::DegToRad( CAMERA_FOV ) );
	}
}

// $Barnyard: FUNCTION 00522a80
void ADartMiniGame::FacePlayersToCamera()
{
	TVector4 vZero = TVector4::VEC_ZERO;

	static TVector4 s_vPlayer1Position = CAMERA_POSITION;
	static TVector4 s_vPlayer2Position = CAMERA_POSITION;

	const TVector4& rcCameraPos = ACameraManager::GetSingleton()->GetCurrentCamera()->GetMatrix().GetTranslation();

	TVector4 vDirection( vZero.x - rcCameraPos.x, vZero.y - rcCameraPos.y, vZero.z - rcCameraPos.z, vZero.w );
	vDirection.Normalise();

	m_aPlayers[ 0 ].pUnit->GetSteer()->SetPosition( s_vPlayer1Position );
	m_aPlayers[ 1 ].pUnit->GetSteer()->SetPosition( s_vPlayer2Position );

	TVector4 vDirectionXZ( vDirection.x, 0.0f, vDirection.z, vDirection.w );
	vDirectionXZ.Normalise();

	m_aPlayers[ 0 ].pUnit->GetSteer()->SetDirection( vDirectionXZ );
	m_aPlayers[ 1 ].pUnit->SetSteerDirection( vDirectionXZ );
}

// $Barnyard: FUNCTION 00522bf0
void ADartMiniGame::UpdateAimX( ADartThrow& a_rThrow, TFLOAT a_fDeltaTime )
{
	Player& rPlayer = GetCurrentPlayer();
	m_oFireHud.SetColour( rPlayer.afColour[ 0 ], rPlayer.afColour[ 1 ], rPlayer.afColour[ 2 ] );

	a_rThrow.m_fAimY = TMath::Cos( GetRoundData().fAimFrequencyX * m_fAimTimerX * TMath::PI );
	m_oFireHud.SetBarY( m_oThrow.m_fAimY * -1.0f );

	GetThrowController( GetCurrentPlayer() )->SetAim( a_rThrow.m_fAimX, a_rThrow.m_fAimY );

	m_fAimTimerX += a_fDeltaTime;
	UpdatePlayerFacing();
}

// $Barnyard: FUNCTION 00522cc0
void ADartMiniGame::UpdateAimY( ADartThrow& a_rThrow, TFLOAT a_fDeltaTime )
{
	Player& rPlayer = GetCurrentPlayer();
	m_oFireHud.SetColour( rPlayer.afColour[ 0 ], rPlayer.afColour[ 1 ], rPlayer.afColour[ 2 ] );

	a_rThrow.m_fAimX = TMath::Cos( GetRoundData().fAimFrequencyY * m_fAimTimerY * TMath::PI );
	m_oFireHud.SetBarX( m_oThrow.m_fAimX * -1.0f );
	m_oFireHud.SetBarY( m_oThrow.m_fAimY * -1.0f );

	GetThrowController( GetCurrentPlayer() )->SetAim( m_oThrow.m_fAimX, m_oThrow.m_fAimY );

	m_fAimTimerY += a_fDeltaTime;
	m_oFireHud.SetBullseyePosition( a_rThrow.m_fAimX * -1.0f, a_rThrow.m_fAimY * -1.0f );
	UpdatePlayerFacing();
}

// $Barnyard: FUNCTION 00522dd0
void ADartMiniGame::PlayHitSound( TINT a_iPlayer, ADartThrow& a_rThrow )
{
	if ( a_iPlayer == 1 )
	{
		// Root
		if ( a_rThrow.m_iTeam != ADartThrow::TEAM_NONE )
		{
			if ( m_aPlayers[ 1 ].iTeam == a_rThrow.m_iTeam )
			{
				if ( ADartThrow::IsBullseye( a_rThrow ) )
					ASoundManager::GetSingleton()->PlayCue( soundbank::DARTS_ROOT_SCORE );
			}
			else
			{
				ASoundManager::GetSingleton()->PlayCue( soundbank::DARTS_ROOT_MISS );
			}
		}
	}
	else
	{
		Player& rPlayer = m_aPlayers[ a_iPlayer ];

		if ( rPlayer.iTeam == a_rThrow.m_iTeam && ( a_rThrow.m_iScore == 10 || a_rThrow.m_iScore == 5 ) )
			ASoundManager::GetSingleton()->PlayCue( ASoundId( ( rPlayer.eGender != APlayerProfile::GENDER_MALE ) ? 677 : 636 ) );
	}
}
