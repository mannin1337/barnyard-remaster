// Included by ABarnYardPoolMiniGameState.cpp. The native object keeps its retail layout.
#include "ABarnYardPoolMiniGameState_View.h"
#include "Player/ACoopMinigameTiming.h"

using PoolSpectator::PoolViewFrame;
namespace
{
constexpr TUINT32 POOL_VIEW_MAGIC = 0x31565043;

TBOOL PoolRange( TFLOAT value, TFLOAT low, TFLOAT high ) { return value >= low && value <= high; }
TBOOL ValidPoolView( const PoolViewFrame& frame )
{
	if ( frame.magic != POOL_VIEW_MAGIC || !frame.serial || frame.serial > 1000000 || frame.owner > 1 ||
	     frame.state > 29 || frame.stage > 3 || (frame.winner > 1 && frame.winner != 6) ||
	     (frame.visibleBalls & ~65535u) || (frame.pottedBalls & ~65535u) || frame.cueVisible > 1 || frame.aimVisible > 1 ||
	     !PoolRange(frame.fov,0.01f,3.13f) || !PoolRange(frame.power,0,1) || !PoolRange(frame.cueOpacity,0,1) ||
	     frame.message[63] || frame.title[63] || frame.info[63] ) return TFALSE;
	if ( (frame.stage == 1 && frame.state != 23) || (frame.stage == 2 && frame.state != 9) ||
	     (frame.stage == 3 && frame.state != 10) || (frame.stage && frame.winner != 6) ) return TFALSE;
	for ( TINT i = 0; i < 2; ++i )
		if ( frame.potted[i] > 8 || frame.groups[i] < 2 || frame.groups[i] > 4 || frame.shots[i] > 999999 ) return TFALSE;
	if ( frame.serial != 1u + frame.shots[0] + frame.shots[1] ) return TFALSE;
	for ( const auto& ball : frame.balls ) for ( TFLOAT axis : ball ) if ( !PoolRange(axis,-10000,10000) ) return TFALSE;
	for ( TFLOAT value : frame.cueMatrix ) if ( !PoolRange(value,-10000,10000) ) return TFALSE;
	const auto& m = frame.cueMatrix;
	const TFLOAT determinant = m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
	if ( !PoolRange(TMath::Abs(determinant),0.0001f,1000) ) return TFALSE;
	TFLOAT lengthSq = 0;
	for ( TINT i = 0; i < 3; ++i )
	{
		if ( !PoolRange(frame.aim[i],-10000,10000) || !PoolRange(frame.cameraPosition[i],-10000,10000) || !PoolRange(frame.cameraDirection[i],-1.1f,1.1f) ) return TFALSE;
		lengthSq += frame.cameraDirection[i] * frame.cameraDirection[i];
	}
	return PoolRange(lengthSq,0.5f,1.5f);
}
void PoolCopyText( TWCHAR (&target)[64], const TWCHAR* source )
{
	TINT i = 0;
	if ( source ) for ( ; i < 63 && source[i]; ++i ) target[i] = source[i];
	for ( ; i < 64; ++i ) target[i] = 0;
}
TUINT32 PoolFrameHash( const PoolViewFrame& frame )
{
	TUINT32 hash = 2166136261u;
	const auto* bytes = reinterpret_cast<const unsigned char*>(&frame);
	for ( TUINT i = 0; i < sizeof(frame); ++i ) hash = (hash ^ bytes[i]) * 16777619u;
	return hash;
}
TBOOL PoolDriverEnabled()
{
#ifndef TOSHI_FINAL
	const char* cmd = AGameLoader::Dev_GetCommandLine();
	const char* flag = "-devpooldriver";
	const size_t length = std::strlen(flag);
	for ( const char* at = cmd; (at = std::strstr(at,flag)) != TNULL; ++at )
		if ( (at == cmd || at[-1] == ' ' || at[-1] == '"') && (!at[length] || at[length] == ' ' || at[length] == '"') ) return TTRUE;
#endif
	return TFALSE;
}
}

struct ABarnYardPoolMiniGameState::CoopState
{
	PoolViewFrame view{};
	PoolSpectator::Motion motion;
	PoolSpectator::ChargePreview charge;
	TUINT loggedCharge = 0;
	TUINT localSerial = 0, nativeSerial = 0, loggedMotion = 0;
	TBOOL received = TFALSE, pressed = TFALSE, released = TFALSE, shootHeld = TFALSE;
	TFLOAT aimX = 0, aimY = 0, mouseUntil = 0, publishWait = 0;
	TBOOL driver = TFALSE, driverDown = TFALSE, driverPlaced = TFALSE;
	TUINT driverSerial = 0, driverStage = 0, driverPlan = 0;
	TFLOAT driverClock = 0, driverWait = 0, driverPower = 0;
	TVector4 driverDirection = TVector4::VEC_POSZ;

	void SubmitGuestInput(TBOOL down, TBOOL up)
	{
		charge.Edge(view.serial,view.stage,motion.sequence,down,up);
		ACoopMinigame::SubmitPoolInput(view.serial,aimX,aimY,down,up);
	}
};
ABarnYardPoolMiniGameState::CoopState* ABarnYardPoolMiniGameState::ms_pCoop = TNULL;

TUINT ABarnYardPoolMiniGameState::GetCoopShotSerial() const
{
	return 1u + TUINT(m_aTeams[0].iNumShots) + TUINT(m_aTeams[1].iNumShots);
}
TUINT ABarnYardPoolMiniGameState::GetCoopInputStage() const
{
	if ( !m_bStarted || m_fInputDelay > 0 || m_iWinner != 6 ) return 0;
	if ( m_eState == STATE_HUMAN_PLACE_BALL ) return 1;
	if ( m_eState == STATE_HUMAN_AIM ) return 2;
	if ( m_eState == STATE_HUMAN_POWER ) return 3;
	return 0;
}
void ABarnYardPoolMiniGameState::PublishCoopState()
{
	if ( ACoopMinigame::IsRealtimePoolHost() && m_bStarted )
		ACoopMinigame::SetPoolState(m_iCurrentTeam,GetCoopShotSerial(),GetCoopInputStage(),m_aTeams[0].GetNumPotted(),m_aTeams[1].GetNumPotted());
}
void ABarnYardPoolMiniGameState::ResetCoopState()
{
	delete ms_pCoop;
	ms_pCoop = new CoopState;
	ms_pCoop->driver = PoolDriverEnabled();
	m_bSubmitHighScore = TFALSE;
	m_fAimX = m_fAimY = 0;
	m_abAimInput[0] = m_abAimInput[1] = TFALSE;
	if ( !ACoopMinigame::IsRealtimePoolHost() )
	{
		m_pCue->SetAIEnabled(TFALSE);
		m_pAimTarget->SetAIEnabled(TFALSE);
		m_pCue->GetAnimatableObject()->SetAnimControllersDisabled(TTRUE);
		m_pAimTarget->GetAnimatableObject()->SetAnimControllersDisabled(TTRUE);
	}
	TINFO("[pool-pvp] init role=%s localSlot=%u humans=%d ai=%d teams=%d,%d\n",ACoopMinigame::IsRealtimePoolHost() ? "host" : "guest",ACoopMinigame::GetRealtimePoolLocalPlayer(),m_iNumPlayers,m_iNumAIPlayers,m_aTeams[0].GetCurrentPlayer(),m_aTeams[1].GetCurrentPlayer());
	if ( ms_pCoop->driver ) TINFO("[pool-driver] enabled input-only localSlot=%u\n",ACoopMinigame::GetRealtimePoolLocalPlayer());
}

// Optional QA player: the planner supplies a reference, and only ordinary LAN
// aim intent + Shoot edges are emitted. Never writes a ball, shot velocity, score or winner.
void ABarnYardPoolMiniGameState::DevCoopDriver( TFLOAT dt )
{
	CoopState& coop = *ms_pCoop;
	if ( !coop.driver ) return;
	coop.driverClock += dt;
	coop.driverWait += dt;
	const TBOOL host = ACoopMinigame::IsRealtimePoolHost();
	const TUINT serial = host ? GetCoopShotSerial() : coop.view.serial;
	const TUINT stage = host ? GetCoopInputStage() : (coop.received ? coop.view.stage : 0u);
	const TUINT owner = host ? TUINT(m_iCurrentTeam) : coop.view.owner;
	if ( !stage || owner != ACoopMinigame::GetRealtimePoolLocalPlayer() || ACoopMinigame::BlockInput() ) return;
	if ( coop.driverSerial != serial )
	{
		coop.driverSerial = serial;
		coop.driverPlan = 0;
		coop.driverDown = TFALSE;
		coop.driverPlaced = TFALSE;
		coop.driverWait = 0;
	}
	if ( coop.driverStage != stage ) { coop.driverStage = stage; coop.driverWait = 0; }
	coop.localSerial = serial;
	coop.aimX = coop.aimY = 0;
	TBOOL down = TFALSE, up = TFALSE;
	if ( stage == 1 )
	{
		coop.driverDown = TFALSE;
		coop.driverPlaced = TTRUE;
		if ( !m_pCueBall->IsPlacementValid() )
		{
			coop.aimX = TFLOAT(std::sin(coop.driverClock * 0.8f)) * 0.4f;
			coop.aimY = TFLOAT(std::cos(coop.driverClock * 0.8f)) * 0.4f;
		}
		else if ( coop.driverWait >= 0.35f ) { down = up = TTRUE; coop.driverWait = 0; }
	}
	else if ( stage == 2 )
	{
		if ( coop.driverPlan != serial && coop.driverWait >= 0.35f )
		{
			// This state is stationary. GetShot only changes planner scratch data;
			// PlaceCueBall is deliberately never called by the driver.
			const TINT target = host ? GetCurrentTeam().GetTargetBallType() :
			    (coop.view.potted[owner] >= 7 ? BALLTYPE_BLACK : TINT(coop.view.groups[owner]));
			m_oAI.SetTargetBallType(target);
			TVector4 reference = m_oAI.GetShot(TTRUE);
			coop.driverPower = reference.NormaliseMag();
			if ( coop.driverPower < 0.01f ) reference = m_vForward;
			// Ball-in-hand permits only the forward half. Keep this restriction
			// in the reference target; the native clamp still owns the actual aim.
			if ( coop.driverPlaced && TVector4::DotProduct3(reference,m_vForward) <= 0.01f )
			{
				reference = m_vForward;
				coop.driverPower = 10.0f;
			}
			coop.driverDirection = reference;
			TMath::Clip(coop.driverPower,1.0f,14.0f);
			coop.driverPlan = serial;
			TINFO("[pool-driver] plan serial=%u owner=%u target=%d power=%.5f direction=%.6f,%.6f\n",serial,owner,target,coop.driverPower,reference.x,reference.z);
		}
		if ( coop.driverPlan == serial )
		{
			const TVector4 target = host ? m_vAimTarget :
			    TVector4(coop.view.aim[0],coop.view.aim[1],coop.view.aim[2],1);
			TVector4 current = target - m_pCueBall->GetPosition();
			current.y = 0; current.Normalise();
			TVector4 right = m_pCamera->GetDirection();
			right.y = 0; right.Normalise(); right.AsVector3().RotateY(TMath::PI * 0.5f);
			const TFLOAT cross = current.x * coop.driverDirection.z - current.z * coop.driverDirection.x;
			const TFLOAT dot = current.x * coop.driverDirection.x + current.z * coop.driverDirection.z;
			const TFLOAT angle = TFLOAT(std::atan2(cross,dot));
			const TFLOAT derivative = current.x * right.z - current.z * right.x;
			if ( TMath::Abs(angle) > 0.003f && !coop.driverDown )
			{
				coop.aimX = TMath::Abs(derivative) > 0.01f ? angle / derivative * 2.0f : 0.3f;
				TMath::Clip(coop.aimX,-0.5f,0.5f);
				coop.driverWait = 0;
			}
			else if ( coop.driverWait >= (coop.driverDown ? 1.0f : 0.3f) )
			{
				down = TTRUE;
				coop.driverDown = TTRUE;
				coop.driverWait = 0;
			}
		}
	}
	else if ( stage == 3 )
	{
		const TFLOAT power = host ? m_pCue->GetPower() : coop.view.power * 15.0f;
		if ( power >= coop.driverPower || coop.driverWait > 2.5f )
		{
			up = TTRUE;
			coop.driverWait = 0;
		}
	}
	if ( down || up ) TINFO("[pool-driver] input serial=%u owner=%u stage=%u down=%d up=%d\n",serial,owner,stage,down,up);
	if ( host ) { coop.pressed |= down; coop.released |= up; }
	else coop.SubmitGuestInput(down,up);
}

TBOOL ABarnYardPoolMiniGameState::ProcessCoopCommand( AInputCommand command, const TInputInterface::InputEvent* event, TBOOL& stop )
{
	if ( !ms_pCoop || ACoopMinigame::BlockInput() ) return AMiniGameState::ProcessCommand(command,event,stop);
	CoopState& coop = *ms_pCoop;
	const TBOOL host = ACoopMinigame::IsRealtimePoolHost();
	const TUINT serial = host ? GetCoopShotSerial() : coop.view.serial;
	const TUINT stage = host ? GetCoopInputStage() : (coop.received ? coop.view.stage : 0u);
	const TUINT owner = host ? TUINT(m_iCurrentTeam) : coop.view.owner;
	if ( !stage || owner != ACoopMinigame::GetRealtimePoolLocalPlayer() ) return AMiniGameState::ProcessCommand(command,event,stop);
	if ( coop.localSerial != serial )
	{
		coop.localSerial = serial;
		coop.aimX = coop.aimY = 0;
		coop.pressed = coop.released = TFALSE;
	}
	const TBOOL down = event->GetEventType() == TInputInterface::EVENT_TYPE_GONE_DOWN;
	const TBOOL up = event->GetEventType() == TInputInterface::EVENT_TYPE_GONE_UP;
	switch ( command )
	{
		case AInputCommand_Left: coop.mouseUntil = 0; coop.aimX = up ? 0.0f : -1.0f; break;
		case AInputCommand_Right: coop.mouseUntil = 0; coop.aimX = up ? 0.0f : 1.0f; break;
		case AInputCommand_Forward: coop.mouseUntil = 0; coop.aimY = up ? 0.0f : -1.0f; break;
		case AInputCommand_Backward: coop.mouseUntil = 0; coop.aimY = up ? 0.0f : 1.0f; break;
		case AInputCommand_Move:
		{
			coop.aimX = event->GetMagnitudeFloat(0);
			coop.aimY = event->GetMagnitudeFloat(1);
			coop.mouseUntil = 0;
			if ( event->GetSource()->IsA(&TGetClass(TInputDeviceMouse)) )
			{
				const TFLOAT sensitivity = AOptions::GetSingleton()->GetSensitivity();
				coop.aimX = TMath::Pow(TMath::Abs(coop.aimX*sensitivity),1.3f) * (coop.aimX < 0 ? -1.0f : 1.0f) * 0.15f;
				coop.aimY = TMath::Pow(TMath::Abs(coop.aimY*sensitivity),1.3f) * (coop.aimY < 0 ? -1.0f : 1.0f) * 0.15f;
				coop.mouseUntil = 0.06f; // Let the 30 Hz transport see the impulse, then send neutral.
			}
			TMath::Clip(coop.aimX,-1.0f,1.0f);
			TMath::Clip(coop.aimY,-1.0f,1.0f);
			break;
		}
		case AInputCommand_Shoot:
			coop.pressed |= down;
			coop.released |= up;
			break;
		default: return AMiniGameState::ProcessCommand(command,event,stop);
	}
	if ( !host )
	{
		coop.SubmitGuestInput(coop.pressed,coop.released);
		coop.pressed = coop.released = TFALSE;
	}
	return TTRUE;
}

void ABarnYardPoolMiniGameState::ApplyCoopInput( TFLOAT x, TFLOAT y, TBOOL pressed, TBOOL released )
{
	CoopState& coop = *ms_pCoop;
	if ( !GetCoopInputStage() )
	{
		m_abAimInput[0] = m_abAimInput[1] = TFALSE;
		coop.shootHeld = TFALSE;
		coop.aimX = coop.aimY = 0;
		coop.pressed = coop.released = TFALSE;
		return;
	}
	if ( (x < 0 && m_fAimX > 0) || (x > 0 && m_fAimX < 0) ) m_afAimSpeed[1] = 0.007f;
	if ( (y < 0 && m_fAimY > 0) || (y > 0 && m_fAimY < 0) ) m_afAimSpeed[0] = 0.007f;
	m_fAimX = x * m_afAimSpeed[1]; m_fAimY = y * m_afAimSpeed[0];
	m_abAimInput[1] = TMath::Abs(x) > 0.0001f; m_abAimInput[0] = TMath::Abs(y) > 0.0001f;
	if ( pressed && !coop.shootHeld && (m_eState == STATE_HUMAN_AIM || m_eState == STATE_HUMAN_PLACE_BALL) )
	{
		coop.shootHeld = TTRUE;
		m_pCue->ResetPower();
		if ( m_eState == STATE_HUMAN_AIM ) m_eState = STATE_HUMAN_POWER;
	}
	if ( released && coop.shootHeld )
	{
		coop.shootHeld = TFALSE;
		if ( m_eState == STATE_HUMAN_POWER )
		{
			m_vShotVelocity = m_pCue->GetShotVelocity();
			m_eState = STATE_HUMAN_SHOT;
		}
		else if ( m_eState == STATE_HUMAN_PLACE_BALL && m_pCueBall->IsPlacementValid() )
		{
			m_eState = STATE_START_TURN;
			m_fInputDelay = 0.1f;
		}
	}
}

void ABarnYardPoolMiniGameState::UpdateCoopInput()
{
	if ( !ms_pCoop ) return;
	CoopState& coop = *ms_pCoop;
	const TUINT serial = GetCoopShotSerial();
	if ( coop.nativeSerial != serial )
	{
		coop.nativeSerial = serial;
		coop.shootHeld = TFALSE;
		coop.aimX = coop.aimY = 0;
		coop.pressed = coop.released = TFALSE;
	}
	if ( m_iCurrentTeam == 1 )
	{
		ACoopMinigame::PoolInput input;
		const TBOOL fresh = ACoopMinigame::ConsumePoolInput(serial,input);
		if ( !fresh && coop.shootHeld )
		{
			coop.shootHeld = TFALSE;
			if ( m_eState == STATE_HUMAN_POWER )
			{
				m_pCue->ResetPower();
				m_eState = STATE_HUMAN_AIM;
				TINFO("[pool-pvp] charge cancelled serial=%u owner=1 reason=input-expired\n",serial);
			}
		}
		ApplyCoopInput(input.aimX,input.aimY,input.shootPressed,input.shootReleased);
		if ( input.shootPressed || input.shootReleased ) TINFO("[pool-pvp] input native serial=%u owner=1 down=%d up=%d state=%d\n",serial,input.shootPressed,input.shootReleased,m_eState);
	}
	else ApplyCoopInput(coop.aimX,coop.aimY,coop.pressed,coop.released);
	coop.pressed = coop.released = TFALSE;
}

TBOOL ABarnYardPoolMiniGameState::OnUpdate( TFLOAT dt )
{
	if ( !ACoopMinigame::IsRealtimePool(this) ) return AMiniGameState::OnUpdate(dt);
	if ( !ms_pCoop ) return AGameState::OnUpdate(dt);
	CoopState& coop = *ms_pCoop;
	static TUINT nameRevision = 0;
	if ( ACoopMinigame::PlayerNamesChanged(nameRevision) )
		for ( TINT i = 0; i < 2; ++i ) m_oHud.m_aTeams[i].SetDisplayName(ACoopPartner::GetPlayerName(i));
	if ( coop.mouseUntil > 0 )
	{
		coop.mouseUntil -= dt;
		if ( coop.mouseUntil <= 0 ) coop.aimX = coop.aimY = 0;
	}
	if ( !ACoopMinigame::IsRealtimePoolHost() )
	{
		ACoopMinigame::CanUpdate(this); // Loading handshake only; guest native game never advances.
		PoolViewFrame frame{};
		TUINT sequence = 0, match = 0;
		if ( ACoopMinigame::ReadSpectatorFrame(this,&frame,sizeof(frame),&sequence,&match) && ValidPoolView(frame) )
		{
			const TBOOL titleChanged = !coop.received || std::memcmp(frame.title,coop.view.title,sizeof(frame.title)) != 0;
			const TBOOL turnChanged = coop.received && frame.owner != coop.view.owner;
			const TBOOL infoChanged = !coop.received || std::memcmp(frame.info,coop.view.info,sizeof(frame.info)) != 0;
			const TBOOL fresh = coop.motion.Fresh(sequence,match);
			const TBOOL localInput = frame.owner == ACoopMinigame::GetRealtimePoolLocalPlayer() && frame.stage;
			if (coop.motion.match != match) coop.charge = {};
			PoolViewFrame previous;
			if (coop.driver && !fresh) previous = coop.motion.shown;
			coop.motion.Update(frame,sequence,match,dt,localInput);
			const PoolViewFrame& shown = coop.motion.shown;
			if (fresh) coop.view = frame;
			coop.received = TTRUE;
			m_oStartHudElement.SetForceHide(TTRUE);
			m_GUIElement.Show();
			for ( TINT i = 0; i < NUM_BALLS; ++i )
			{
				const TVector4 position(shown.balls[i][0],shown.balls[i][1],shown.balls[i][2],1);
				auto* ball = TSTATICCAST(AUnitPoolBall,GetBallUnit(i));
				ball->SetPosition(position);
				const TBOOL potted = (frame.pottedBalls & (1u << i)) != 0;
				ball->SetPotted(potted);
				ball->GetPoolSteer()->SetState(potted ? APoolBallSteer::STATE_POTTED : APoolBallSteer::STATE_STOPPED);
				ball->SetVisible((frame.visibleBalls & (1u << i)) != 0);
				ball->GetModelInstance()->SetTransform(position,TVector4::VEC_POSX);
				auto* shadow = TSTATICCAST(AAnimatedUnit,m_aShadows[i]->GetUnit());
				const TVector4 shadowPosition(position.x-0.02f,position.y,position.z-0.02f,1);
				shadow->SetSteerPosition(shadowPosition);
				shadow->SetVisible((frame.visibleBalls & (1u << i)) != 0);
				shadow->GetModelInstance()->SetTransform(shadowPosition,TVector4::VEC_POSX);
			}
			TMatrix44 cue;
			std::memcpy(&cue,shown.cueMatrix,sizeof(cue));
			m_pCue->SetVisible(frame.cueVisible != 0);
			m_pCue->GetModelInstance()->SetTransform(cue);
			m_pCue->GetModelInstance()->SetOpacity(shown.cueOpacity);
			m_pCue->m_vecCuePosition = cue.GetTranslation();
			m_vAimTarget.Set(shown.aim[0],shown.aim[1],shown.aim[2],1);
			m_pAimTarget->SetSteerPosition(m_vAimTarget);
			m_pAimTarget->SetVisible(frame.aimVisible != 0);
			m_pAimTarget->GetModelInstance()->SetTransform(m_vAimTarget,TVector4::VEC_POSX);
			m_eState = STATE(frame.state); // Display phase only; UpdateGame is never called here.
			m_iCurrentTeam = frame.owner;
			UpdateAimGuide(dt); // Read-only collision queries for the dotted guide.
			m_aViews[0].vPosition.Set(shown.cameraPosition[0],shown.cameraPosition[1],shown.cameraPosition[2],1);
			m_aViews[0].vLookAt.Set(shown.cameraPosition[0]+shown.cameraDirection[0],shown.cameraPosition[1]+shown.cameraDirection[1],shown.cameraPosition[2]+shown.cameraDirection[2],1);
			m_pCamera->SetView(0);
			ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
			camera->LookAt(m_aViews[0].vPosition,m_aViews[0].vLookAt);
			camera->SetFOV(shown.fov);
			m_oHud.SetTeamsVisible(TTRUE);
			m_oHud.SetFirstTeamActive(frame.owner == 0);
			if ( turnChanged ) ShowCoopTurn(frame.owner);
			for ( TINT i = 0; i < 2; ++i )
			{
				if ( frame.groups[i] != BALLTYPE_NONE ) m_oHud.SetBallColour(i,frame.groups[i]);
				m_oHud.SetNumLitBalls(i,frame.potted[i]);
			}
			m_oHud.SetMessage(coop.view.message[0] ? coop.view.message : TNULL);
			if ( titleChanged ) m_oHud.SetTitleText(coop.view.title);
			if ( infoChanged ) m_oHud.SetInfoText(coop.view.info);
			m_oHud.Update(dt);
			m_oPowerBar.SetBarVisible(frame.state == STATE_HUMAN_AIM || frame.state == STATE_HUMAN_POWER);
			if ( fresh && ACoopMinigame::IsViewLogEnabled() )
			{
				TINFO("[pool-pvp] frame apply match=%u sequence=%u serial=%u owner=%u state=%u potted=%u,%u mask=%04x hash=%08x\n",match,sequence,frame.serial,frame.owner,frame.state,frame.potted[0],frame.potted[1],frame.pottedBalls,PoolFrameHash(frame));
			}
			if ( coop.driver && !fresh )
			{
				// Read back the applied model/camera position on a render update
				// with no new packet. Log the first moving sample of each kind.
				auto probe = [&](TUINT bit, const char* kind, const TFLOAT* old, const TVector4& now)
				{
					bit <<= localInput ? 3 : 0;
					const TVector4 before(old[0],old[1],old[2],1);
					const TFLOAT stepSq = TVector4::DistanceSq(before,now);
					if ((coop.loggedMotion & bit) || stepSq < 0.00000001f) return;
					coop.loggedMotion |= bit;
					TINFO("[pool-motion] no-new-packet match=%u sequence=%u serial=%u state=%u localInput=%d kind=%s step=%.6f world=%.4f,%.4f,%.4f\n",
					    match,sequence,frame.serial,frame.state,localInput,kind,std::sqrt(stepSq),now.x,now.y,now.z);
				};
				probe(1,"camera",previous.cameraPosition,camera->GetPosition());
				TVector4 sample;
				if (frame.cueVisible)
				{
					m_pCue->GetModelInstance()->GetPosition(sample);
					probe(2,"cue",previous.cueMatrix+12,sample);
				}
				if (frame.visibleBalls & 1u)
				{
					m_pCueBall->GetModelInstance()->GetPosition(sample);
					probe(4,"ball",previous.balls[0],sample);
				}
			}
			const TBOOL canControl = localInput && !ACoopMinigame::BlockInput();
			if ( canControl )
			{
				if ( coop.localSerial != frame.serial ) { coop.localSerial = frame.serial; coop.aimX = coop.aimY = 0; }
				DevCoopDriver(dt);
				ACoopMinigame::SubmitPoolInput(frame.serial,coop.aimX,coop.aimY,TFALSE,TFALSE);
			}
			else
			{
				coop.aimX = coop.aimY = 0;
				ACoopMinigame::SubmitPoolInput(frame.serial,0,0,TFALSE,TFALSE);
			}
			const TBOOL wasConfirmed = coop.charge.confirmed;
			const TFLOAT displayed = coop.charge.Update(frame,canControl,fresh,dt);
			using Charge = PoolSpectator::ChargePreview;
			m_oPowerBar.SetValue(displayed,coop.charge.state != Charge::Idle);
			if (coop.driver && coop.charge.state != Charge::Idle)
			{
				// Read the actual HUD fill after SetValue. The input sequence is
				// the newest host sample already visible when the local edge fired.
				auto log = [&](TUINT bit, const char* event, TUINT inputSequence)
				{
					if (coop.loggedCharge & bit) return;
					coop.loggedCharge |= bit;
					TINFO("[pool-power-view] %s serial=%u inputSequence=%u sequence=%u hostStage=%u host=%.6f bar=%.6f leadMs=%.1f\n",
					    event,frame.serial,inputSequence,sequence,frame.stage,frame.power,m_oPowerBar.m_fValue,coop.charge.lead*1000.0f);
				};
				if (coop.charge.state == Charge::Held && !coop.charge.confirmed && m_oPowerBar.m_fValue > frame.power)
					log(1,"press-before-ack",coop.charge.pressSequence);
				if (coop.charge.state == Charge::Released)
					log(2,"release-before-shot",coop.charge.releaseSequence);
				if (!wasConfirmed && coop.charge.confirmed)
					log(4,"host-confirmed",coop.charge.pressSequence);
			}
		}
		else
		{
			coop.received = TFALSE;
			coop.motion.sequence = 0;
			coop.charge = {};
			coop.aimX = coop.aimY = 0;
			ACoopMinigame::SuspendPoolInput();
		}
		return AGameState::OnUpdate(dt);
	}
	DevCoopDriver(dt);
	const TBOOL result = AMiniGameState::OnUpdate(dt);
	PublishCoopState();
	if ( !m_bStarted ) return result;
	if ( !CoopMini::ViewSendDue(coop.publishWait,dt) ) return result;
	PoolViewFrame frame{};
	frame.magic = POOL_VIEW_MAGIC; frame.serial = GetCoopShotSerial(); frame.owner = m_iCurrentTeam;
	frame.state = m_eState; frame.stage = GetCoopInputStage(); frame.winner = m_iWinner;
	for ( TINT i = 0; i < NUM_BALLS; ++i )
	{
		auto* ball = TSTATICCAST(AUnitPoolBall,GetBallUnit(i));
		const TVector4 position = ball->GetPosition();
		frame.balls[i][0] = position.x; frame.balls[i][1] = position.y; frame.balls[i][2] = position.z;
		if ( ball->IsVisible() ) frame.visibleBalls |= 1u << i;
		if ( ball->IsPotted() ) frame.pottedBalls |= 1u << i;
	}
	TMatrix44 cue;
	m_pCue->GetModelInstance()->GetLocalTransform(cue);
	std::memcpy(frame.cueMatrix,&cue,sizeof(cue));
	frame.cueOpacity = m_pCue->GetModelInstance()->GetOpacity();
	frame.cueVisible = m_pCue->IsVisible() ? 1u : 0u; frame.aimVisible = m_pAimTarget->IsVisible() ? 1u : 0u;
	frame.aim[0] = m_vAimTarget.x; frame.aim[1] = m_vAimTarget.y; frame.aim[2] = m_vAimTarget.z;
	const ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
	const TVector4 position = camera->GetPosition();
	TVector4 direction; camera->GetDirection(direction);
	frame.cameraPosition[0] = position.x; frame.cameraPosition[1] = position.y; frame.cameraPosition[2] = position.z;
	frame.cameraDirection[0] = direction.x; frame.cameraDirection[1] = direction.y; frame.cameraDirection[2] = direction.z;
	frame.fov = camera->GetFOV();
	frame.power = m_pCue->GetPower() / m_pCue->GetMaxPower();
	TMath::Clip(frame.power,0.0f,1.0f);
	for ( TINT i = 0; i < 2; ++i ) { frame.potted[i] = m_aTeams[i].GetNumPotted(); frame.groups[i] = m_aTeams[i].GetBallType(); frame.shots[i] = m_aTeams[i].iNumShots; }
	PoolCopyText(frame.message,m_oHud.m_oMessage.IsVisible() ? m_oHud.m_oMessage.GetText() : TNULL);
	PoolCopyText(frame.title,m_oHud.m_oTitleText.GetText());
	PoolCopyText(frame.info,m_oHud.m_oInfoText.GetText());
	if ( ValidPoolView(frame) )
	{
		TUINT sequence = 0, match = 0;
		ACoopMinigame::PublishSpectatorFrame(this,&frame,sizeof(frame),&sequence,&match);
		if ( sequence && ACoopMinigame::IsViewLogEnabled() ) TINFO("[pool-pvp] frame publish match=%u sequence=%u serial=%u owner=%u state=%u potted=%u,%u mask=%04x hash=%08x\n",match,sequence,frame.serial,frame.owner,frame.state,frame.potted[0],frame.potted[1],frame.pottedBalls,PoolFrameHash(frame));
	}
	return result;
}
