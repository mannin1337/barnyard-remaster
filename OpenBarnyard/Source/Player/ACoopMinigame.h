#pragma once
class AMiniGameState;

// LAN minigames (channel 8). Mud is simultaneous and host-authoritative;
// the other games keep their native turns. All state lives here, not in
// the save, remote cow, story synchronisation or local co-op implementation.
class ACoopMinigame
{
public:
    static TBOOL CanInvite();
    static TBOOL IsPlaying();
    static TBOOL ShowsHudStatus();
    static void OpenMenu();
    static void RequestInvite(TINT a_iGame);
    static void RequestLeave();
    static void OnInserted(AMiniGameState* a_pState);
    static void OnRemoved(AMiniGameState* a_pState);
    static TBOOL CanUpdate(AMiniGameState* a_pState);
    static TBOOL BlockInput();
    static TBOOL IsSpectating(AMiniGameState* a_pState);
    static TBOOL IsViewLogEnabled();
    static TBOOL SupportsGolfGroundView();
    static TBOOL SupportsDartsViewAim();
    static TBOOL SupportsDartsTimedAim();
    static TBOOL SupportsDartsBlendedAim();
    static TBOOL SupportsMudViewInput();
    static TBOOL SupportsPoolShotIntent();
    static TBOOL IsScoreDuel(AMiniGameState* a_pState = TNULL);
    // Owner of a native single-player HUD inside a LAN match. Parallel challenges
    // show the local runner; the shared board in an alternating match shows its turn owner.
    static const TWCHAR* GetNativePlayerName();
    static TBOOL PlayerNamesChanged(TUINT& a_rRevision);
    static TBOOL IsRealtimeMud(AMiniGameState* a_pState = TNULL);
    static TBOOL IsRealtimeMudHost();
    static TUINT GetRealtimeMudLocalPlayer();
    struct MudInput
    {
        TBOOL jumpHeld = TFALSE, jumpPressed = TFALSE, kickPressed = TFALSE;
        TBOOL directionalKick = TFALSE, kickLeft = TFALSE;
    };
    static void SubmitMudInput(TBOOL a_bJumpHeld, TBOOL a_bJumpPressed, TBOOL a_bKickPressed,
        TBOOL a_bDirectionalKick, TBOOL a_bKickLeft);
    static TBOOL ConsumeMudInput(MudInput& a_rInput);
    static void GetMudPressCounters(TUINT& a_rJump, TUINT& a_rKick);
    static void SetMudScores(TINT a_iHostScore, TINT a_iGuestScore);
    static void CompleteMudMatch(TINT a_iHostScore, TINT a_iGuestScore);
    static TBOOL IsRealtimeDarts(AMiniGameState* a_pState = TNULL);
    static TBOOL IsRealtimeDartsHost();
    static TUINT GetRealtimeDartsLocalPlayer();
    static void SubmitDartsInput(TUINT a_uiThrowSerial, TUINT a_uiStage, TUINT a_uiViewSequence = 0,
        TUINT a_uiStageTimeMs = 0, TUINT a_uiPreviousView = 0, TUINT a_uiBlend = 0);
    static TBOOL ConsumeDartsInput(TUINT a_uiThrowSerial, TUINT a_uiStage, TUINT* a_pViewSequence = TNULL,
        TUINT* a_pStageTimeMs = TNULL, TUINT* a_pPreviousView = TNULL, TUINT* a_pBlend = TNULL);
    static void DiscardDartsViewInput(TUINT a_uiThrowSerial, TUINT a_uiStage);
    static void SetDartsState(TUINT a_uiPlayer, TUINT a_uiRound, TUINT a_uiThrowSerial,
        TUINT a_uiStage, TINT a_iHostScore, TINT a_iGuestScore);
    static void CompleteDartsMatch(TINT a_iHostScore, TINT a_iGuestScore);
    static TBOOL IsRealtimePool(AMiniGameState* a_pState = TNULL);
    static TBOOL IsRealtimePoolHost();
    static TUINT GetRealtimePoolLocalPlayer();
    struct PoolInput
    {
        TFLOAT aimX = 0.0f, aimY = 0.0f;
        TBOOL shootPressed = TFALSE, shootReleased = TFALSE;
        TBOOL hasShotIntent = TFALSE;
        TFLOAT shotDirX = 0.0f, shotDirZ = 0.0f, shotPower = 0.0f;
        TBOOL hasPlacementIntent = TFALSE;
        TFLOAT placeX = 0.0f, placeZ = 0.0f;
    };
    static void SubmitPoolInput(TUINT a_uiShotSerial, TFLOAT a_fAimX, TFLOAT a_fAimY,
        TBOOL a_bShootPressed, TBOOL a_bShootReleased,
        TFLOAT a_fShotDirX = 0.0f, TFLOAT a_fShotDirZ = 0.0f, TFLOAT a_fShotPower = 0.0f);
    static void SubmitPoolPlacement(TUINT a_uiShotSerial, TFLOAT a_fAimX, TFLOAT a_fAimY,
        TBOOL a_bShootPressed, TBOOL a_bShootReleased, TFLOAT a_fOffsetX, TFLOAT a_fOffsetZ);
    static void SuspendPoolInput();
    static TBOOL ConsumePoolInput(TUINT a_uiShotSerial, PoolInput& a_rInput);
    static void SetPoolState(TUINT a_uiPlayer, TUINT a_uiShotSerial, TUINT a_uiStage,
        TINT a_iHostPotted, TINT a_iGuestPotted);
    static void CompletePoolMatch(TUINT a_uiWinner, TINT a_iHostPotted, TINT a_iGuestPotted);
    // Turn-based spectator games check this before constructing a model snapshot.
    static TBOOL ShouldCaptureSpectatorFrame(AMiniGameState* a_pState, TFLOAT a_fDeltaTime, TBOOL a_bForce = TFALSE);
    static void PublishSpectatorFrame(AMiniGameState* a_pState, const void* a_pFrame, TUINT a_uiSize,
        TUINT* a_pSequence = TNULL, TUINT* a_pMatch = TNULL);
    static TBOOL ReadSpectatorFrame(AMiniGameState* a_pState, void* a_pFrame, TUINT a_uiSize,
        TUINT* a_pSequence = TNULL, TUINT* a_pMatch = TNULL);
    static void SetScore(TINT a_iScore);
    static void CompleteRound(TINT a_iScore, TBOOL a_bFinal);
    static void UpdateOffline(TFLOAT a_fDeltaTime);
};
