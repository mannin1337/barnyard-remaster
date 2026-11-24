// LAN golf presentation only. Included at the end of AGolfMinigameState.cpp.
// No replica owns an AUnit, steer, collision object, or animation callback.
#include "ASpectatorBlendClock.h"
namespace GolfSpectator
{
static constexpr TUINT32 MAGIC = 0x31565047; // GPV1
struct Pose
{
    TMatrix44 matrix;
    TCHAR type[48] = {};
    TCHAR set[24] = {};
    TCHAR animation[32] = {};
    TFLOAT animationTime = 0.0f;
    TUINT32 shown = 0;
};
struct Frame
{
    TUINT32 magic = MAGIC;
    TINT index = -1, hole = 0, state = 0;
    TMatrix44 camera;
    TFLOAT fov = 1.0f;
    Pose poses[4]; // tee cow, gopher, ball (including carried ball), rescue gopher
};
static_assert(sizeof(Frame) <= 1000, "Golf spectator frame exceeds channel limit");
static constexpr TUINT32 GROUND_MAGIC = 0x32565047; // GPV2
static constexpr TINT TRAILS = AGolfGopherUnit::NUM_TRAIL_OBJECTS;
static constexpr TINT PIECES = TRAILS + AGolfGopherUnit::NUM_MOUND_OBJECTS;
struct GroundFrame
{
    Frame view;
    GolfGround::Piece pieces[PIECES] = {};
};
static_assert(sizeof(GroundFrame) <= 1000, "Golf ground frame exceeds channel limit");

template<TSIZE N> static void CopyName(TCHAR (&out)[N], const TCHAR* value)
{
    if (!value) return;
    std::strncpy(out, value, N - 1);
    out[N - 1] = 0;
}
template<TSIZE N> static TBOOL ValidName(const TCHAR (&value)[N])
{
    for (TSIZE i = 0; i < N; ++i)
    {
        if (!value[i]) return TTRUE;
        if (value[i] < 32 || value[i] > 126) return TFALSE;
    }
    return TFALSE;
}
static TBOOL ValidMatrix(const TMatrix44& matrix)
{
    const TFLOAT* values = reinterpret_cast<const TFLOAT*>(&matrix);
    for (TINT i = 0; i < 16; ++i)
        if (!std::isfinite(values[i]) || std::fabs(values[i]) > 4096.0f) return TFALSE;
    for (TINT row = 0; row < 3; ++row)
    {
        const TFLOAT* axis = values + row * 4;
        const TFLOAT lengthSq = axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2];
        if (lengthSq < 0.01f || lengthSq > 16.0f || std::fabs(axis[3]) > 0.001f) return TFALSE;
    }
    return std::fabs(values[15] - 1.0f) < 0.001f;
}
static void Capture(AAnimatableObject* object, Pose& pose)
{
    pose.matrix.Identity();
    if (!object || !object->GetModelInstance()) return;
    AModelInstance* model = object->GetModelInstance();
    model->GetLocalTransform(pose.matrix);
    pose.shown = model->IsUpdatingSkeleton() ? 1 : 0;
    CopyName(pose.type, object->GetObjectType()->GetName().GetString());
    AToshiAnimationInterface& animation = object->GetToshiAnimInterface();
    if (animation.GetAnimationSet())
        CopyName(pose.set, animation.GetAnimationSet()->GetName().GetString());
    AToshiAnimationRef* main = animation.GetAnim();
    if (main && main->GetNamedAnimation())
    {
        CopyName(pose.animation, main->GetNamedAnimation()->GetName().GetString());
        pose.animationTime = object->GetAnimationTime(main->GetNamedAnimation()->GetName());
    }
}
static void DestroyCopy(AAnimatableObject*& object)
{
    if (object) AAnimatableObjectManager::GetSingleton()->DestroyAnimatableObject(object);
    object = TNULL;
}
static TBOOL PrepareCopy(AAnimatableObjectType* type, AAnimatableObject*& object)
{
    AAnimatableObjectManager* manager = AAnimatableObjectManager::GetSingleton();
    if (!type) { DestroyCopy(object); return TFALSE; }
    if (object && object->GetObjectType() != type) DestroyCopy(object);
    if (!object)
    {
        object = manager->CreateAnimatableObject(type, TNULL, AAnimatableObject::CREATE_FLAGS_NO_ANIM_CONTROLLER);
        if (!object || !object->GetModelInstance()) { DestroyCopy(object); return TFALSE; }
        // The manager must never execute a remote pose's breakpoints. We set
        // its skeleton time directly, and never call AAnimatableObject::Update.
        object->AddFlags(AAnimatableObject::FLAGS_NO_ANIMATION);
        object->GetModelInstance()->GetSceneObject()->DisableOpacity();
        object->GetModelInstance()->SetShadowMode(AModelInstance::SHADOWMODE_DYNAMIC);
    }
    return TTRUE;
}
// Sample the render skeleton only. A snapshot must never allocate a gameplay
// animation reference or execute a breakpoint (25 sods can be visible at once).
static TBOOL Sample(AAnimatableObject* object, const TPString8& name, TFLOAT time, TBOOL normalized)
{
    ANamedAnimationSetRef set = object->GetToshiAnimInterface().GetAnimationSet();
    ANamedAnimation* named = set ? set->GetNamedAnimation(name) : TNULL;
    if (!named || named->GetSequenceId() < 0) return TFALSE;
    TSkeletonInstance* skeleton = object->GetModelInstance()->GetSkeletonInstance();
    const TUINT16 id = TUINT16(named->GetSequenceId());
    if (!skeleton || id >= skeleton->GetSkeleton()->GetSequenceCount()) return TFALSE;
    TAnimation* clip = skeleton->GetAnimation(id);
    if (!clip)
    {
        skeleton->RemoveAllAnimations();
        clip = skeleton->AddAnimation(id, 1.0f, 0.0f);
        if (!clip) return TFALSE;
        clip->SetSpeed(0.0f);
    }
    const TFLOAT duration = clip->GetSequencePtr()->GetDuration();
    if (duration > 0.0f)
    {
        const TFLOAT sampleTime = normalized ? time * duration : time;
        clip->SetTime(sampleTime >= duration ? std::nextafter(duration, 0.0f) : sampleTime);
    }
    skeleton->UpdateState(TTRUE);
    return TTRUE;
}
static void Apply(const Pose& pose, AAnimatableObject*& object)
{
    AAnimatableObjectType* type = pose.type[0] ? AAnimatableObjectManager::GetSingleton()->FindType(TPString8(pose.type)) : TNULL;
    if (!PrepareCopy(type, object)) return;
    object->SetSkeletonUpdating(pose.shown != 0, TTRUE);
    object->SetVisible(pose.shown != 0);
    object->GetModelInstance()->SetTransform(pose.matrix);
    if (!pose.shown || !pose.animation[0]) return;
    if (pose.set[0]) object->SwitchToNamedAnimationSet(TPString8(pose.set));
    Sample(object, TPString8(pose.animation), pose.animationTime, TFALSE);
}

static AAnimatableObject* NativePiece(AGolfGopherUnit* gopher, TINT index)
{
    if (!gopher) return TNULL;
    return index < TRAILS ? gopher->GetTrailPiece(index) : gopher->GetMoundPiece(index - TRAILS);
}
static const TCHAR* const SOD_TYPES[] = {"", "sod_1", "sod_2", "sod_3"};
static const TCHAR* GroundClip(TBOOL trail, TBOOL second)
{
    return trail ? (second ? "r_tunnel" : "l_tunnel") : (second ? "submerge" : "surface");
}
static GolfGround::Piece CaptureGround(AAnimatableObject* object, const TVector4& origin, TBOOL trail)
{
    if (!object || !object->GetModelInstance()->IsUpdatingSkeleton()) return {};
    const TPString8& name = object->GetObjectType()->GetName();
    unsigned type = 1;
    while (type <= 3 && name != TPString8(SOD_TYPES[type])) ++type;
    const TPString8 secondClip(GroundClip(trail, TTRUE));
    const TBOOL second = object->IsAnimPlaying(secondClip, 0.0f);
    const TPString8 clip(GroundClip(trail, second));
    // After the surface animation finishes, native keeps the raised mound's
    // final pose. A trail with no animation is already gone.
    const TBOOL playing = object->IsAnimPlaying(clip, 0.0f);
    if (trail && !playing) return {};
    const TFLOAT phase = playing ? object->GetAnimationProgress(clip) : 1.0f;
    TMatrix44 matrix;
    object->GetModelInstance()->GetLocalTransform(matrix);
    const TVector4& pos = matrix.GetTranslation();
    const TFLOAT heading = std::atan2(-matrix.m_f21, -matrix.m_f23);
    return GolfGround::Encode(pos.x-origin.x, pos.y-origin.y, pos.z-origin.z, heading, type, second, phase);
}
static TBOOL ApplyGround(const GolfGround::Piece& piece, const TVector4& origin, TBOOL trail, AAnimatableObject*& copy)
{
    if (!piece.Type())
    {
        if (copy) copy->SetSkeletonUpdating(TFALSE, TFALSE);
        return TFALSE;
    }
    AAnimatableObjectType* type = AAnimatableObjectManager::GetSingleton()->FindType(TPString8(SOD_TYPES[piece.Type()]));
    if (!PrepareCopy(type, copy)) return TFALSE;
    const TFLOAT yaw = piece.Heading();
    const TVector4 direction(std::sin(yaw), 0.0f, std::cos(yaw), 1.0f);
    const TVector4 pos(origin.x + piece.x / 128.0f, origin.y + piece.y / 128.0f, origin.z + piece.z / 128.0f, 1.0f);
    copy->GetModelInstance()->SetTransform(pos, direction);
    const TFLOAT alpha = trail || piece.SecondClip() ? 1.0f - piece.Progress() : 1.0f;
    copy->SetOpacity(alpha, TFALSE);
    const TBOOL shown = alpha > 0.0f && Sample(copy, TPString8(GroundClip(trail, piece.SecondClip())), piece.Progress(), TTRUE);
    copy->SetSkeletonUpdating(shown, TFALSE);
    copy->SetVisible(shown);
    return shown;
}

#ifdef PRIMEDEBUG_DEV
// Request one renderer-owned frame per process once the replica has a visible
// mound and its gopher is holding the peek pose, or has nearly finished surfacing.
static void DevCaptureSurface(ACamera* camera, const Pose& pose, AAnimatableObject* gopher)
{
    static bool done = std::strstr(AGameLoader::Dev_GetCommandLine(), "-devgolfviewshots") == nullptr;
    if (done || !camera || !gopher || !pose.shown) return;
    const bool idle = !std::strcmp(pose.animation, "inground_idle") ||
        (!std::strcmp(pose.set, "above") && !std::strcmp(pose.animation, "idle"));
    const bool surfacing = !std::strcmp(pose.animation, "inground_surface") ||
        !std::strcmp(pose.animation, "aboveground_surface") || !std::strcmp(pose.animation, "surface");
    if (!idle && !surfacing) return;
    ANamedAnimationSetRef set = gopher->GetToshiAnimInterface().GetAnimationSet();
    ANamedAnimation* named = set ? set->GetNamedAnimation(TPString8(pose.animation)) : TNULL;
    TSkeletonInstance* skeleton = gopher->GetModelInstance()->GetSkeletonInstance();
    if (!skeleton || !named || named->GetSequenceId() < 0 || named->GetSequenceId() >= skeleton->GetSkeleton()->GetSequenceCount()) return;
    TAnimation* clip = skeleton->GetAnimation(TUINT16(named->GetSequenceId()));
    if (!clip || (surfacing && clip->GetSeqTime() < 0.65f * clip->GetSequencePtr()->GetDuration())) return;
    ARenderer* renderer = ARenderer::GetSingleton();
    TVector2 screen, size;
    renderer->GetMainViewportSize(size);
    if (!renderer->ProjectToScreen(camera, pose.matrix.GetTranslation(), screen) ||
        screen.x < 0.12f * size.x || screen.x > 0.88f * size.x ||
        screen.y < 0.23f * size.y || screen.y > 0.88f * size.y) return;
    done = true;
    T2CommandLine options(AGameLoader::Dev_GetCommandLine());
    const TCHAR* prefix = options.GetNextParameter("-devgolfviewshots");
    if (!prefix || !*prefix || *prefix == '-' || std::strlen(prefix) + sizeof("_surface.png") > MAX_PATH) return;
    TCHAR name[MAX_PATH], command[] = "screenshot";
    TStringManager::String8Format(name, TARRAYSIZE(name), "%s_surface.png", prefix);
    TCHAR* arguments[] = {command, name};
    if (PConsole::ExecuteCommand(arguments, TARRAYSIZE(arguments)) == PConsole::RESULT_OK)
        TINFO("[coopgolfview] surface frame queued clip=%s time=%.3f screen=%.1f,%.1f\n", pose.animation, clip->GetSeqTime(), screen.x, screen.y);
}
#endif

static void BlendTransform(TMatrix44& out, const TMatrix44& start, const TMatrix44& end, TFLOAT alpha, TBOOL rotate)
{
    const TVector4& a = start.GetTranslation();
    const TVector4& b = end.GetTranslation();
    // Hard camera cuts, moved tee shots and teleports use the new pose at once.
    if (TVector4::DistanceSq(a, b) > 64.0f) return;
    if (rotate)
    {
        TQuaternion from, to, rotation;
        TMatrix44::MatToQuat(from, start);
        TMatrix44::MatToQuat(to, end);
        const TFLOAT dot = TVector4::DotProduct4(from.AsVector4(), to.AsVector4());
        // Camera transforms must be rigid for quaternion interpolation. Preserve
        // the supplied matrix for unusual bases and large changes of viewpoint.
        if (std::isfinite(dot) && std::fabs(dot) <= 1.001f &&
            std::fabs(from.AsVector4().MagnitudeSq4() - 1.0f) < 0.002f &&
            std::fabs(to.AsVector4().MagnitudeSq4() - 1.0f) < 0.002f)
        {
            if (std::fabs(dot) < 0.8f) return;
            TQuaternion::Nlerp(rotation, from, to, alpha);
            out.SetFromQuaternion(rotation);
        }
    }
    out.GetTranslation().Lerp4(a, b, alpha);
}

struct Motion
{
    Frame start{}, shown{};
    TUINT sequence = 0;
    ASpectatorBlendClock timing;

    void Update(const Frame& next, TUINT serial, TFLOAT dt)
    {
        const TBOOL fresh = serial != sequence;
        const TBOOL cut = !sequence || shown.index != next.index || shown.state != next.state;
        if (fresh)
        {
            if (!cut) start = shown;
            sequence = serial;
        }
        const TFLOAT alpha = timing.Advance(dt, fresh, cut);
        shown = next;
        if (alpha >= 1.0f) return;
        BlendTransform(shown.camera, start.camera, next.camera, alpha, TTRUE);
        shown.fov = start.fov + (next.fov - start.fov) * alpha;
        for (TINT i = 0; i < 4; ++i)
        {
            const Pose& a = start.poses[i];
            const Pose& b = next.poses[i];
            if (!a.shown || !b.shown || std::strcmp(a.type, b.type)) continue;
            BlendTransform(shown.poses[i].matrix, a.matrix, b.matrix, alpha, TFALSE);
            // Never interpolate across a set/clip change or backwards through a loop.
            const TFLOAT step = b.animationTime - a.animationTime;
            if (step >= 0.0f && step < 0.2f && !std::strcmp(a.set, b.set) && !std::strcmp(a.animation, b.animation))
                shown.poses[i].animationTime = a.animationTime + step * alpha;
        }
    }
};
} // namespace GolfSpectator

struct AGolfMinigameState::CoopSpectator
{
    AAnimatableObject* copies[4] = {};
    AAnimatableObject* groundCopies[GolfSpectator::PIECES] = {};
    TBOOL loggedTrail = TFALSE, loggedMound = TFALSE;
    GolfSpectator::Motion motion;
    TBOOL loggedSmooth = TFALSE;
    TINT index, state, lives, scores[MAX_PLAYERS], strokes[MAX_PLAYERS];
    TBOOL reload, finished, playFinished, loseLife;
    TFLOAT elapsed, limit;
    TINT shownIndex = -1;
    TBOOL applied = TFALSE;
};

void AGolfMinigameState::CoopPublishSpectator(TFLOAT deltaTime)
{
    if (!ACoopMinigame::IsPlaying() || ACoopMinigame::BlockInput() || m_pCoopSpectator ||
        !m_bHoleLoaded || !m_pCourse || m_iNumPlayers != 1 || m_iCurrentHole < 0 ||
        m_iCurrentHole >= m_pCourse->iNumHoles) return;
    ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
    if (!camera || !ACoopMinigame::ShouldCaptureSpectatorFrame(this, deltaTime)) return;
    GolfSpectator::GroundFrame extended;
    GolfSpectator::Frame& frame = extended.view;
    frame.index = m_iCurrentHole;
    frame.hole = m_pCourse->aHoles[m_iCurrentHole];
    frame.state = m_eGameState;
    camera->GetMatrix(frame.camera);
    frame.fov = camera->GetFOV();
    AAnimatedUnit* cow = TSTATICCAST(AAnimatedUnit, GetPlayerUnit());
    AAnimatedUnit* gopher = m_aPlayers[0].pGopher ? TSTATICCAST(AAnimatedUnit, m_aPlayers[0].pGopher->GetUnit()) : TNULL;
    AAnimatedUnit* ball = m_aPlayers[0].pBall ? TSTATICCAST(AAnimatedUnit, m_aPlayers[0].pBall->GetUnit()) : TNULL;
    GolfSpectator::Capture(cow ? cow->GetAnimatableObject() : TNULL, frame.poses[0]);
    GolfSpectator::Capture(gopher ? gopher->GetAnimatableObject() : TNULL, frame.poses[1]);
    GolfSpectator::Capture(ball ? ball->GetAnimatableObject() : TNULL, frame.poses[2]);
    if (!frame.poses[2].shown)
    {
        AAnimatableObject* held = m_aPlayers[0].pBallModel;
        if (held && held->GetModelInstance()->IsUpdatingSkeleton())
            GolfSpectator::Capture(held, frame.poses[2]);
        else if (m_aPlayers[0].pGopherGolf && m_aPlayers[0].pGopherGolf->GetModelInstance()->IsUpdatingSkeleton())
            GolfSpectator::Capture(m_aPlayers[0].pGopherGolf->FindAttachedObject(TPS8(cb_llocator)), frame.poses[2]);
    }
    GolfSpectator::Capture(m_aPlayers[0].pGopherGolf, frame.poses[3]);
    if (ACoopMinigame::SupportsGolfGroundView())
    {
        frame.magic = GolfSpectator::GROUND_MAGIC;
        if (frame.state == GAMESTATE_PLAY && gopher)
            for (TINT i = 0; i < GolfSpectator::PIECES; ++i)
                extended.pieces[i] = GolfSpectator::CaptureGround(
                    GolfSpectator::NativePiece(TSTATICCAST(AGolfGopherUnit, gopher), i),
                    frame.poses[1].matrix.GetTranslation(), i < GolfSpectator::TRAILS);
        ACoopMinigame::PublishSpectatorFrame(this, &extended, sizeof(extended));
    }
    else ACoopMinigame::PublishSpectatorFrame(this, &frame, sizeof(frame));
}

void AGolfMinigameState::CoopUpdateSpectator(TFLOAT deltaTime)
{
    GolfSpectator::GroundFrame extended;
    GolfSpectator::Frame& frame = extended.view;
    TUINT sequence = 0;
    const TBOOL hasGround = ACoopMinigame::SupportsGolfGroundView() &&
        ACoopMinigame::ReadSpectatorFrame(this, &extended, sizeof(extended), &sequence);
    if ((!hasGround && !ACoopMinigame::ReadSpectatorFrame(this, &frame, sizeof(frame), &sequence)) ||
        frame.magic != (hasGround ? GolfSpectator::GROUND_MAGIC : GolfSpectator::MAGIC) || !m_pCourse || m_iNumPlayers != 1 ||
        frame.index < 0 || frame.index >= m_pCourse->iNumHoles ||
        frame.hole != m_pCourse->aHoles[frame.index] || frame.hole < 1 || frame.hole > MAX_HOLES ||
        frame.state < GAMESTATE_INTRO || frame.state > GAMESTATE_END ||
        !GolfSpectator::ValidMatrix(frame.camera) || !std::isfinite(frame.fov) ||
        frame.fov < 0.1f || frame.fov > 3.0f) return;
    for (const GolfSpectator::Pose& pose : frame.poses)
        if (!GolfSpectator::ValidMatrix(pose.matrix) || pose.shown > 1 ||
            !GolfSpectator::ValidName(pose.type) || !GolfSpectator::ValidName(pose.set) ||
            !GolfSpectator::ValidName(pose.animation) || !std::isfinite(pose.animationTime) ||
            pose.animationTime < 0.0f || pose.animationTime > 3600.0f) return;

    if (!m_pCoopSpectator)
    {
        m_pCoopSpectator = new CoopSpectator();
        CoopSpectator& saved = *m_pCoopSpectator;
        saved.index = m_iCurrentHole; saved.state = m_eGameState; saved.lives = m_iLives;
        saved.reload = m_bReloadHole; saved.finished = m_bCourseFinished;
        saved.playFinished = m_bPlayFinished; saved.loseLife = m_bLoseLife;
        saved.elapsed = m_fElapsedTime; saved.limit = m_fTimeLimit;
        for (TINT i = 0; i < m_iNumPlayers; ++i)
        { saved.scores[i] = m_aPlayers[i].iScore; saved.strokes[i] = m_aPlayers[i].iStrokes; }
        saved.shownIndex = m_iCurrentHole;
        TINFO("[coopgolfview] begin local-index=%d next=%d\n", saved.index, saved.index + (saved.reload ? 1 : 0));
    }
    CoopSpectator& saved = *m_pCoopSpectator;
    if (saved.shownIndex != frame.index)
    {
        for (AAnimatableObject*& copy : saved.copies) GolfSpectator::DestroyCopy(copy);
        for (AAnimatableObject*& copy : saved.groundCopies) GolfSpectator::DestroyCopy(copy);
        m_iCurrentHole = frame.index - 1; // LoadHole increments before loading.
        LoadHole();
        saved.shownIndex = frame.index;
        TINFO("[coopgolfview] terrain index=%d hole=%d\n", frame.index, frame.hole);
    }
    // These listeners belong to the local tee/result, not the replica. A normal
    // SetupPlayerTurn recreates them when this player's next native turn starts.
    m_oSwingListener.Disconnect();
    m_oPlayListeners.Clear();
    for (TINT i = 0; i < m_iNumPlayers; ++i)
    {
        Player& player = m_aPlayers[i];
        player.oAnimListener.Disconnect();
        if (player.pGopher && player.pGopher->GetUnit()) TSTATICCAST(AAnimatedUnit, player.pGopher->GetUnit())->SetVisible(TFALSE);
        if (player.pBall && player.pBall->GetUnit()) TSTATICCAST(AAnimatedUnit, player.pBall->GetUnit())->SetVisible(TFALSE);
        player.pGopherGolf->SetSkeletonUpdating(TFALSE, TTRUE);
        player.pBallModel->SetSkeletonUpdating(TFALSE, TTRUE);
        player.oReticle.SetVisible(TFALSE);
    }
    if (GetPlayerUnit()) TSTATICCAST(AAnimatedUnit, GetPlayerUnit())->SetVisible(TFALSE);
    m_pTee->SetSkeletonUpdating(TFALSE, TTRUE);
    m_oFlagReticle.SetVisible(TFALSE);
    m_GUIElement.SetVisible(TFALSE);
    m_oPowerBar.SetForceHide(TTRUE);
    m_pTimerOrStrokes->Hide(); m_oLifeHud.SetVisible(TFALSE);
    m_oPopup.Hide(); m_oTextBox.Hide(); m_oHoleText.Hide(); m_oInstructionsText.Hide();
    ACameraManager::GetSingleton()->DetachCameraHelpers();
    ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
    const TBOOL fresh = saved.motion.sequence != sequence;
    const TBOOL blending = saved.motion.timing.IsBlending();
    const TVector4 lastCamera = saved.motion.sequence ? saved.motion.shown.camera.GetTranslation() : frame.camera.GetTranslation();
    saved.motion.Update(frame, sequence, deltaTime);
    const GolfSpectator::Frame& shown = saved.motion.shown;
    if (camera) { camera->SetMatrix(shown.camera); camera->SetFOV(shown.fov); }
    if (fresh || blending)
        for (TINT i = 0; i < 4; ++i) GolfSpectator::Apply(shown.poses[i], saved.copies[i]);
    if (hasGround && fresh)
    {
        auto* local = TSTATICCAST(AGolfGopherUnit, m_aPlayers[0].pGopher ? m_aPlayers[0].pGopher->GetUnit() : TNULL);
        TINT trail = 0, mound = 0;
        AAnimatableObject* sample = TNULL;
        for (TINT i = 0; i < GolfSpectator::PIECES; ++i)
        {
            if (auto* piece = GolfSpectator::NativePiece(local, i)) piece->SetSkeletonUpdating(TFALSE, TFALSE);
            const TBOOL isTrail = i < GolfSpectator::TRAILS;
            if (GolfSpectator::ApplyGround(extended.pieces[i], frame.poses[1].matrix.GetTranslation(), isTrail, saved.groundCopies[i]))
            {
                if (isTrail) ++trail;
                else ++mound;
                if ((!saved.loggedTrail && isTrail) || (!saved.loggedMound && !isTrail)) sample = saved.groundCopies[i];
            }
        }
        if (sample && camera)
        {
            TVector4 pos;
            sample->GetModelInstance()->GetPosition(pos);
            TVector2 screen;
            const TBOOL projected = ARenderer::GetSingleton()->ProjectToScreen(camera, pos, screen);
            TINFO("[coopgolfview] ground applied index=%d hole=%d trail=%d mound=%d projected=%d screen=%.1f,%.1f\n",
                frame.index, frame.hole, trail, mound, projected, screen.x, screen.y);
            saved.loggedTrail |= trail != 0;
            saved.loggedMound |= mound != 0;
        }
#ifdef PRIMEDEBUG_DEV
        if (mound == AGolfGopherUnit::NUM_MOUND_OBJECTS && !extended.pieces[GolfSpectator::TRAILS].SecondClip())
            GolfSpectator::DevCaptureSurface(camera, shown.poses[1], saved.copies[1]);
#endif
    }
    if (!fresh && blending && !saved.loggedSmooth &&
        TVector4::DistanceSq(lastCamera, shown.camera.GetTranslation()) > 0.0000001f)
    {
        saved.loggedSmooth = TTRUE;
        TINFO("[coopgolfview] camera advanced between packets sequence=%u interval=%.1fms\n", sequence, saved.motion.timing.duration*1000.0f);
    }
    if (!saved.applied && camera)
    {
        for (TINT i = 0; i < 4; ++i)
        {
            if (frame.poses[i].shown && saved.copies[i])
            {
                saved.applied = TTRUE;
                TINFO("[coopgolfview] Golf spectator applied index=%d hole=%d\n", frame.index, frame.hole);
                break;
            }
        }
    }
}

void AGolfMinigameState::CoopStopSpectator(TBOOL resume)
{
    if (!m_pCoopSpectator) return;
    CoopSpectator saved = *m_pCoopSpectator;
    for (AAnimatableObject*& copy : m_pCoopSpectator->copies) GolfSpectator::DestroyCopy(copy);
    for (AAnimatableObject*& copy : m_pCoopSpectator->groundCopies) GolfSpectator::DestroyCopy(copy);
    delete m_pCoopSpectator;
    m_pCoopSpectator = TNULL;
    m_iCurrentHole = saved.index; m_eGameState = GAMESTATE(saved.state);
    m_bReloadHole = saved.reload; m_bCourseFinished = saved.finished;
    m_bPlayFinished = saved.playFinished; m_bLoseLife = saved.loseLife;
    m_fElapsedTime = saved.elapsed; m_fTimeLimit = saved.limit;
    if (resume && !saved.finished && saved.state != GAMESTATE_END)
    {
        // LAN yields only between holes/retries, so the next local turn is a
        // fresh native tee. Never advance from the spectator's displayed hole.
        const TINT next = saved.index + (saved.reload ? 1 : 0);
        m_iCurrentHole = next - 1;
        m_bReloadHole = TFALSE;
        LoadHole();
        if (GetPlayerUnit()) TSTATICCAST(AAnimatedUnit, GetPlayerUnit())->SetVisible(TTRUE);
        m_pTee->SetSkeletonUpdating(TTRUE, TTRUE);
        m_GUIElement.SetVisible(TTRUE);
    }
    m_iLives = saved.lives;
    m_bCourseFinished = saved.finished;
    for (TINT i = 0; i < m_iNumPlayers; ++i)
    {
        m_aPlayers[i].iScore = saved.scores[i];
        if (!resume) m_aPlayers[i].iStrokes = saved.strokes[i];
    }
    TINFO("[coopgolfview] end resume=%d local-index=%d score=%d lives=%d\n", resume, m_iCurrentHole, m_aPlayers[0].iScore, m_iLives);
}
