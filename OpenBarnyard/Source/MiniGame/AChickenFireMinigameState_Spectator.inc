// LAN Chicken Launch presentation only. Included by AChickenFireMinigameState.cpp.
// No replica owns an AUnit, steer, collision object, or animation callback.
#include "AChickenFireSpectatorMotion.h"

namespace ChickenSpectator
{
static constexpr TUINT32 MAGIC = 0x31565043; // CPV1
struct Pose
{
    TMatrix44 matrix;
    TCHAR type[24] = {};
    TCHAR set[24] = {};
    TCHAR animation[32] = {};
    TFLOAT animationTime = 0.0f;
    TUINT32 shown = 0;
};
struct Frame
{
    TMatrix44 camera;
    Pose poses[4]; // Peck, launch cow, Freddy, visible scorecard
    TUINT32 magic = MAGIC;
    TINT variant = 0, state = 0, card = -1;
    TFLOAT fov = 1.0f;
    TUINT16 ringCount = 0, coinCount = 0;
    TUINT8 ringAlpha[200] = {};
    TUINT8 coinShown[31] = {};
};
static_assert(sizeof(Frame) <= 1000, "Chicken spectator frame exceeds channel limit");

static void BlendTransform(TMatrix44& shown, const TMatrix44& next, TFLOAT alpha)
{
    // Peck's native maximum flight speed is 60 units/s. The playback buffer
    // resets after 200 ms, so a larger displacement is a camera cut or teleport.
    const TVector4 position = shown.GetTranslation();
    if (TVector4::DistanceSq(position, next.GetTranslation()) > 16.0f * 16.0f) return;
    if (shown.IsOrthonormal() && next.IsOrthonormal())
    {
        TQuaternion from, to, rotation;
        TMatrix44::MatToQuat(from, shown);
        TMatrix44::MatToQuat(to, next);
        if (std::fabs(from.MagnitudeSq() - 1.0f) < 0.002f &&
            std::fabs(to.MagnitudeSq() - 1.0f) < 0.002f)
        {
            TQuaternion::Nlerp(rotation, from, to, alpha);
            shown.SetFromQuaternion(rotation);
        }
    }
    shown.GetTranslation().Lerp4(position, next.GetTranslation(), alpha);
}

static void BlendPresentation(Frame& shown, const Frame& next, TFLOAT alpha)
{
    // Discrete state, visibility, clip changes and camera cuts switch when the
    // presentation clock reaches the next sample. Never mix two launch phases.
    if (shown.state != next.state) return;
    BlendTransform(shown.camera, next.camera, alpha);
    shown.fov += (next.fov - shown.fov) * alpha;
    for (TINT i = 0; i < 4; ++i)
    {
        Pose& pose = shown.poses[i];
        const Pose& target = next.poses[i];
        if (!pose.shown || !target.shown || std::strcmp(pose.type, target.type) ||
            (i == 3 && shown.card != next.card)) continue;
        BlendTransform(pose.matrix, target.matrix, alpha);
        const TFLOAT step = target.animationTime - pose.animationTime;
        if (step >= 0.0f && step < 0.4f && !std::strcmp(pose.set, target.set) &&
            !std::strcmp(pose.animation, target.animation))
            pose.animationTime += step * alpha;
    }
}

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
    pose.shown = model->IsUpdatingSkeleton() && model->IsVisible() ? 1 : 0;
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
static void Apply(const Pose& pose, AAnimatableObject*& object)
{
    if (!pose.type[0]) { DestroyCopy(object); return; }
    AAnimatableObjectManager* manager = AAnimatableObjectManager::GetSingleton();
    AAnimatableObjectType* type = manager->FindType(TPString8(pose.type));
    if (!type) { DestroyCopy(object); return; } // Never load arbitrary network paths.
    if (object && object->GetObjectType() != type) DestroyCopy(object);
    if (!object)
    {
        object = manager->CreateAnimatableObject(type, TNULL, AAnimatableObject::CREATE_FLAGS_NO_ANIM_CONTROLLER);
        if (!object || !object->GetModelInstance()) { DestroyCopy(object); return; }
        // The manager must never execute a remote pose's breakpoints. We set
        // its skeleton time directly, and never call AAnimatableObject::Update.
        object->AddFlags(AAnimatableObject::FLAGS_NO_ANIMATION);
        object->GetModelInstance()->GetSceneObject()->DisableOpacity();
        object->GetModelInstance()->SetShadowMode(AModelInstance::SHADOWMODE_DYNAMIC);
    }
    object->SetSkeletonUpdating(pose.shown != 0, TTRUE);
    object->SetVisible(pose.shown != 0);
    object->GetModelInstance()->SetTransform(pose.matrix);
    if (!pose.shown || !pose.animation[0]) return;
    if (pose.set[0]) object->SwitchToNamedAnimationSet(TPString8(pose.set));
    const TPString8 name(pose.animation);
    if (!object->HasAnimation(name)) return;
    AToshiAnimationInterface& animation = object->GetToshiAnimInterface();
    AToshiAnimationRef* current = animation.GetAnim();
    if (!current || current->GetNamedAnimation()->GetName() != name)
    {
        animation.KillAllAnimations();
        if ((g_oAnimationRefPool.GetCapacity() - g_oAnimationRefPool.GetUsedSize()) /
            sizeof(AToshiAnimationRef) < 16) return;
        current = animation.PlayAnim(name, 1.0f, TTRUE);
    }
    if (current)
    {
        animation.SetAnimSpeed(current, 0.0f);
        animation.SetAnimTime(current, pose.animationTime);
        object->GetModelInstance()->GetSkeletonInstance()->UpdateState(TTRUE);
    }
}
struct Visibility
{
    TBOOL skeleton = TFALSE, visible = TFALSE;
    void Save(AAnimatableObject* object)
    {
        if (!object || !object->GetModelInstance()) return;
        skeleton = object->GetModelInstance()->IsUpdatingSkeleton();
        visible = object->GetModelInstance()->IsVisible();
    }
    void Restore(AAnimatableObject* object) const
    {
        if (!object || !object->GetModelInstance()) return;
        object->SetSkeletonUpdating(skeleton, TTRUE);
        object->SetVisible(visible);
    }
};
static TBOOL TypeMatches(const Pose& pose, AAnimatableObject* local, TBOOL player)
{
    if (!pose.type[0]) return !pose.shown;
    if (player && (!std::strcmp(pose.type, "cfp_americanm") || !std::strcmp(pose.type, "cfp_americanf"))) return TTRUE;
    return local && local->GetObjectType()->GetName() == TPString8(pose.type);
}
} // namespace ChickenSpectator

struct AChickenFireMinigameState::CoopSpectator
{
    AAnimatableObject* copies[4] = {};
    ChickenSpectator::Playback<ChickenSpectator::Frame> playback;
#ifndef TOSHI_FINAL
    TBOOL traceMotion = TFALSE, haveFlightPosition = TFALSE;
    TVector4 lastPeckPosition, lastCameraPosition;
#endif
    ChickenSpectator::Visibility bodies[3], cards[10], coins[241];
    TFLOAT rings[200][2] = {};
    TINT ringCount = 0, coinCount = 0, cardCount = 0;
    TMatrix44 camera;
    TFLOAT fov = 1.0f;
    TBOOL gui, portrait, feathers, text, bashHidden, pickupHidden, startHidden;
    TBOOL applied = TFALSE;
};

void AChickenFireMinigameState::CoopPublishSpectator()
{
    if (!ACoopMinigame::IsPlaying() || ACoopMinigame::BlockInput() ||
        m_pCoopSpectator || m_iNumPlayers != 1 || !GetPeckUnit() ||
        m_vecObjects1.Size() != m_vecObjects2.Size()) return;
    ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
    if (!camera) return;
    ChickenSpectator::Frame frame;
    frame.variant = m_iVariant; frame.state = m_eState;
    camera->GetMatrix(frame.camera); frame.fov = camera->GetFOV();
    ChickenSpectator::Capture(GetPeckObject(), frame.poses[0]);
    ChickenSpectator::Capture(m_pPlayerModel, frame.poses[1]);
    ChickenSpectator::Capture(m_pFreddy, frame.poses[2]);
    ChickenSpectator::Capture(TNULL, frame.poses[3]);
    for (TINT i = 0; i < m_vecObjects4.Size(); ++i)
    {
        AModelInstance* model = m_vecObjects4[i]->GetModelInstance();
        if (model->IsUpdatingSkeleton() && model->IsVisible())
        {
            frame.card = i;
            ChickenSpectator::Capture(m_vecObjects4[i], frame.poses[3]);
            break;
        }
    }
    frame.ringCount = TUINT16(m_vecObjects1.Size());
    frame.coinCount = TUINT16(m_vecObjects3.Size());
    for (TINT i = 0; i < frame.ringCount; ++i)
    {
        TFLOAT alpha = m_vecObjects1[i]->GetModelInstance()->GetOpacity();
        if (!std::isfinite(alpha)) alpha = 0.0f;
        TMath::Clip(alpha, 0.0f, 1.0f);
        frame.ringAlpha[i] = TUINT8(alpha * 255.0f);
    }
    for (TINT i = 0; i < frame.coinCount; ++i)
    {
        AModelInstance* model = m_vecObjects3[i]->GetModelInstance();
        if (model->IsUpdatingSkeleton() && model->IsVisible() && model->GetOpacity() > 0.01f)
            frame.coinShown[i / 8] |= TUINT8(1u << (i % 8));
    }
    ACoopMinigame::PublishSpectatorFrame(this, &frame, sizeof(frame));
}

void AChickenFireMinigameState::CoopUpdateSpectator(TFLOAT deltaTime)
{
    ChickenSpectator::Frame frame;
    TUINT sequence, match;
    if (!ACoopMinigame::ReadSpectatorFrame(this, &frame, sizeof(frame), &sequence, &match) ||
        frame.magic != ChickenSpectator::MAGIC || frame.variant != m_iVariant ||
        m_iNumPlayers != 1 || !GetPeckUnit() ||
        frame.state < STATE_WAIT_FOR_BASH || frame.state > STATE_STOPPED ||
        frame.card < -1 || frame.card >= m_vecObjects4.Size() ||
        frame.ringCount > 200 || frame.coinCount > 241 ||
        frame.ringCount != m_vecObjects1.Size() || frame.ringCount != m_vecObjects2.Size() ||
        frame.coinCount != m_vecObjects3.Size() ||
        !ChickenSpectator::ValidMatrix(frame.camera) || !std::isfinite(frame.fov) ||
        frame.fov < 0.1f || frame.fov > 3.0f) return;
    for (const ChickenSpectator::Pose& pose : frame.poses)
        if (!ChickenSpectator::ValidMatrix(pose.matrix) || pose.shown > 1 ||
            !ChickenSpectator::ValidName(pose.type) || !ChickenSpectator::ValidName(pose.set) ||
            !ChickenSpectator::ValidName(pose.animation) || !std::isfinite(pose.animationTime) ||
            pose.animationTime < 0.0f || pose.animationTime > 3600.0f) return;
    AAnimatableObject* bodies[] = { GetPeckObject(), m_pPlayerModel, m_pFreddy };
    for (TINT i = 0; i < 3; ++i)
        if (!ChickenSpectator::TypeMatches(frame.poses[i], bodies[i], i == 1)) return;
    if (!ChickenSpectator::TypeMatches(frame.poses[3], frame.card < 0 ? TNULL : m_vecObjects4[frame.card], TFALSE)) return;
    ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
    if (!camera) return;
    if (!m_pCoopSpectator)
    {
        m_pCoopSpectator = new CoopSpectator();
        CoopSpectator& saved = *m_pCoopSpectator;
#ifndef TOSHI_FINAL
        saved.traceMotion = std::strstr(AGameLoader::Dev_GetCommandLine(), "-devchickenmotion") != TNULL;
#endif
        camera->GetMatrix(saved.camera); saved.fov = camera->GetFOV();
        for (TINT i = 0; i < 3; ++i) saved.bodies[i].Save(bodies[i]);
        saved.ringCount = m_vecObjects1.Size(); saved.coinCount = m_vecObjects3.Size();
        saved.cardCount = m_vecObjects4.Size();
        for (TINT i = 0; i < saved.ringCount; ++i)
        {
            saved.rings[i][0] = m_vecObjects1[i]->GetModelInstance()->GetOpacity();
            saved.rings[i][1] = m_vecObjects2[i]->GetModelInstance()->GetOpacity();
        }
        for (TINT i = 0; i < saved.coinCount; ++i) saved.coins[i].Save(m_vecObjects3[i]);
        for (TINT i = 0; i < saved.cardCount; ++i) saved.cards[i].Save(m_vecObjects4[i]);
        saved.gui = m_GUIElement.IsVisible(); saved.portrait = m_oPortrait.IsVisible();
        saved.feathers = m_oFeathers.IsVisible(); saved.text = m_oTextBox.IsVisible();
        saved.bashHidden = m_oBashHud.IsForceHidden();
        saved.pickupHidden = m_oPickUpHudElement.IsForceHidden();
        saved.startHidden = m_oStartHudElement.IsForceHidden();
        TINFO("[coopchickenview] begin local-state=%d score=%d\n", m_eState, m_iScore);
    }
    CoopSpectator& saved = *m_pCoopSpectator;
    TFLOAT alpha;
    const TBOOL fresh = saved.playback.Render(frame, sequence, match, deltaTime, frame,
        alpha, ChickenSpectator::BlendPresentation);
    // Native flight/scoring remain behind CanUpdate. Only render state is changed.
    m_oReleaseListener.Disconnect();
    for (AAnimatableObject* body : bodies)
        if (body) body->SetSkeletonUpdating(TFALSE, TTRUE);
    for (TINT i = 0; i < m_vecObjects4.Size(); ++i)
        m_vecObjects4[i]->SetSkeletonUpdating(TFALSE, TTRUE);
    for (TINT i = 0; i < frame.ringCount; ++i)
    {
        const TFLOAT alpha = frame.ringAlpha[i] / 255.0f;
        m_vecObjects1[i]->GetModelInstance()->SetOpacity(alpha);
        m_vecObjects2[i]->GetModelInstance()->SetOpacity(alpha);
    }
    for (TINT i = 0; i < frame.coinCount; ++i)
        m_vecObjects3[i]->SetSkeletonUpdating((frame.coinShown[i / 8] & (1u << (i % 8))) != 0, TTRUE);
    m_GUIElement.SetVisible(TFALSE); m_oPortrait.Hide();
    m_oFeathers.SetVisible(TFALSE); m_oTextBox.Hide();
    m_oBashHud.SetForceHide(TTRUE); m_oPickUpHudElement.SetForceHide(TTRUE);
    m_oStartHudElement.SetForceHide(TTRUE);
    ACameraManager::GetSingleton()->DetachCameraHelpers();
    camera->SetMatrix(frame.camera); camera->SetFOV(frame.fov);
    for (TINT i = 0; i < 4; ++i) ChickenSpectator::Apply(frame.poses[i], saved.copies[i]);
#ifndef TOSHI_FINAL
    if (saved.traceMotion)
    {
        const TBOOL inFlight = frame.state == STATE_TUMBLE || frame.state == STATE_FLYING ||
            frame.state == STATE_DIVE || frame.state == STATE_DESCEND;
        if (inFlight && saved.copies[0] && saved.copies[0]->GetModelInstance()->IsVisible())
        {
            TVector4 peck;
            saved.copies[0]->GetModelInstance()->GetPosition(peck);
            const TVector4 eye = camera->GetPosition();
            if (saved.haveFlightPosition && !fresh && alpha > 0.0f && alpha < 1.0f)
            {
                const TFLOAT birdStep = TVector4::DistanceSq(saved.lastPeckPosition, peck);
                const TFLOAT cameraStep = TVector4::DistanceSq(saved.lastCameraPosition, eye);
                if (birdStep > 0.0000001f && cameraStep > 0.0000001f)
                {
                    TINFO("[coopchickenview] flight advanced between packets sequence=%u state=%d alpha=%.3f peck=%.4f camera=%.4f\n",
                        sequence, frame.state, alpha, TMath::Sqrt(birdStep), TMath::Sqrt(cameraStep));
                    saved.traceMotion = TFALSE;
                }
            }
            saved.lastPeckPosition = peck; saved.lastCameraPosition = eye;
            saved.haveFlightPosition = TTRUE;
        }
        else saved.haveFlightPosition = TFALSE;
    }
#else
    (void)fresh;
#endif
    if (!saved.applied && frame.poses[0].shown && saved.copies[0])
    {
        saved.applied = TTRUE;
        TINFO("[coopchickenview] Chicken spectator applied state=%d\n", frame.state);
    }
}

void AChickenFireMinigameState::CoopStopSpectator(TBOOL resume)
{
    if (!m_pCoopSpectator) return;
    CoopSpectator& saved = *m_pCoopSpectator;
    for (AAnimatableObject*& copy : saved.copies) ChickenSpectator::DestroyCopy(copy);
    AAnimatableObject* bodies[] = { GetPeckUnit() ? GetPeckObject() : TNULL, m_pPlayerModel, m_pFreddy };
    for (TINT i = 0; i < 3; ++i) saved.bodies[i].Restore(bodies[i]);
    for (TINT i = 0; i < saved.ringCount && i < m_vecObjects1.Size() && i < m_vecObjects2.Size(); ++i)
    {
        m_vecObjects1[i]->GetModelInstance()->SetOpacity(saved.rings[i][0]);
        m_vecObjects2[i]->GetModelInstance()->SetOpacity(saved.rings[i][1]);
    }
    for (TINT i = 0; i < saved.coinCount && i < m_vecObjects3.Size(); ++i) saved.coins[i].Restore(m_vecObjects3[i]);
    for (TINT i = 0; i < saved.cardCount && i < m_vecObjects4.Size(); ++i) saved.cards[i].Restore(m_vecObjects4[i]);
    ACamera* camera = ACameraManager::GetSingleton()->GetCurrentCamera();
    if (camera) { camera->SetMatrix(saved.camera); camera->SetFOV(saved.fov); }
    m_GUIElement.SetVisible(saved.gui && (!resume || m_bStarted));
    m_oPortrait.SetVisible(saved.portrait && (!resume || m_bStarted));
    m_oFeathers.SetVisible(saved.feathers && (!resume || m_bStarted));
    m_oTextBox.SetVisible(saved.text && (!resume || m_bStarted));
    m_oBashHud.SetForceHide(saved.bashHidden || (resume && !m_bStarted));
    m_oPickUpHudElement.SetForceHide(saved.pickupHidden || (resume && !m_bStarted));
    // CanUpdate has already called CoopStartTurn/Setup on resume. Do not undo
    // that new native countdown with the old spectator's hidden HUD setting.
    if (!resume) m_oStartHudElement.SetForceHide(saved.startHidden);
    delete m_pCoopSpectator; m_pCoopSpectator = TNULL;
    TINFO("[coopchickenview] end resume=%d local-state=%d score=%d\n", resume, m_eState, m_iScore);
}

