#pragma once
#include "AGateCrashGuestView.h"

namespace MudGuest
{
enum class LocalState : std::uint32_t { Locked, Ready, Jumping, Kicking };

// Negotiated extension. The first 92 bytes can still be sent to an older peer.
struct InputFrame
{
    Frame arena;
    std::uint32_t jumpPresses, kickPresses;
    LocalState state;
    float velocity, gateSpeed, actionTime;

    bool Valid() const
    {
        return state <= LocalState::Kicking && std::isfinite(gateSpeed) && std::isfinite(actionTime) && actionTime >= 0 &&
               velocity >= -100.0f && velocity <= 100.0f;
    }
};
static_assert(sizeof(InputFrame) == 116);

constexpr float JumpSpeed = 12.0f, Gravity = 49.0f, HeldLift = 20.0f;
constexpr float KickWindow = 0.2f, KickRecovery = 0.3f;

// Native jump_up adds lift only until the upward velocity reaches 4.
inline Animation AdvanceJump(float& y, float& velocity, float dt, bool held)
{
    if (held && velocity > 4.0f) {
        const float toApex = (velocity - 4.0f) / (Gravity - HeldLift);
        const float lift = dt < toApex ? dt : toApex;
        y -= lift * (velocity - 0.5f * (Gravity - HeldLift) * lift);
        velocity -= (Gravity - HeldLift) * lift;
        dt -= lift;
    }
    y -= dt * (velocity - 0.5f * Gravity * dt);
    velocity -= Gravity * dt;
    if (y >= 0.0f && velocity <= 0.0f) { y = 0.0f; return JumpEnd; }
    return velocity < -4.0f ? JumpDown : velocity <= 4.0f ? JumpApex : JumpUp;
}

// Controls one rendered actor only. The native host still owns impacts,
// gate direction, score, lives and whether an input actually starts an action.
struct LocalInput
{
    InputFrame latest{};
    Slot shown{};
    std::uint32_t match = 0, edge = 0;
    LocalState action = LocalState::Locked;
    Animation pose = Idle;
    float age = 0, packetAge = 0, y = 0, velocity = 0, correction = 0;
    bool firstFrame = false;

    bool Begin(LocalState next, std::uint32_t counter, bool directional = false, bool left = false)
    {
        const bool sameFrameKick = next == LocalState::Kicking && action == LocalState::Jumping && firstFrame;
        const bool recovered = (action == LocalState::Jumping && pose == JumpEnd) ||
                               (action == LocalState::Kicking && age > KickWindow + KickRecovery);
        if (!match || packetAge > 0.5f || latest.state == LocalState::Locked ||
            !(recovered || sameFrameKick || (latest.state == LocalState::Ready && action == LocalState::Locked))) return false;
        action = next;
        edge = counter;
        age = correction = 0;
        y = recovered ? 0.0f : latest.arena.slots[1].y;
        velocity = JumpSpeed;
        firstFrame = true;
        pose = next == LocalState::Jumping ? JumpStart :
            ((directional ? left : latest.gateSpeed > 0) ? KickLeft : KickRight);
        return true;
    }

    void Update(float dt, const InputFrame* fresh, std::uint32_t game, bool enabled, bool held, bool windingUp)
    {
        if (!(dt >= 0.0f && std::isfinite(dt))) dt = 0;
        if (fresh && match != game) {
            *this = {};
            match = game;
        }
        packetAge += dt;
        if (fresh) {
            const bool hit = fresh->arena.slots[1].lives < latest.arena.slots[1].lives;
            latest = *fresh;
            packetAge = 0;
            if (hit || latest.state == LocalState::Locked) action = LocalState::Locked;
        }
        age += dt;
        if (!enabled || packetAge > 0.5f) action = LocalState::Locked;

        if (action != LocalState::Locked) {
            const auto ack = action == LocalState::Jumping ? latest.jumpPresses : latest.kickPresses;
            const bool consumed = std::int32_t(ack - edge) >= 0;
            if ((consumed && latest.state != action) || (!consumed && age > 0.75f))
                action = LocalState::Locked;
            else if (consumed && fresh) {
                if (action == LocalState::Kicking)
                    pose = static_cast<Animation>(latest.arena.slots[1].animation);
                else if (latest.arena.slots[1].y < 0.0f && pose != JumpEnd) {
                    // An arriving takeoff can be older than the local descent.
                    // Align both samples to the same action time before blending.
                    y = latest.arena.slots[1].y;
                    velocity = latest.velocity;
                    const float lead = age - dt - latest.actionTime;
                    pose = AdvanceJump(y, velocity, lead > 0.0f ? lead : 0.0f, held);
                    correction = shown.y - y;
                }
            }
        }
        shown = latest.arena.slots[1];
        if (action == LocalState::Locked) return;

        if (action == LocalState::Jumping && !firstFrame &&
            pose != JumpEnd && (pose != JumpStart || !windingUp)) {
            // The guest's own jump_start animation determines liftoff, just as on
            // the host. Integrate in seconds so the arc is stable at different FPS.
            pose = AdvanceJump(y, velocity, dt, held);
        }
        firstFrame = false;
        correction *= std::exp(-24.0f * dt);
        const float height = y + correction;
        shown.y = height < 0.0f ? height : 0.0f;
        shown.animation = pose;
    }
};
}
