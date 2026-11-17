#pragma once
#include <cmath>
#include <cstdint>

namespace MudGuest
{
// Existing display-only wire format, also understood by v5.5.
struct Slot { float x, y, z; std::uint32_t animation; std::int32_t lives; };
struct Frame { std::uint32_t magic, count; float angle; Slot slots[4]; };
static_assert(sizeof(Frame) == 92);

enum Animation : std::uint32_t {
    Idle, ServeIdle, Serve, JumpStart, JumpUp, JumpApex, JumpDown, JumpEnd,
    KickLeft, KickRight, ImpactLeft, ImpactRight, Victory, JumpLiftoff, Contact
};

inline float Wrap(float angle)
{
    constexpr float pi = 3.14159265f;
    if (angle > pi) return angle - 2.0f * pi;
    if (angle < -pi) return angle + 2.0f * pi;
    return angle;
}

inline bool HardPose(const Slot& slot)
{
    return slot.animation == ImpactLeft || slot.animation == ImpactRight ||
           slot.animation == Victory || slot.animation == Contact;
}

inline bool Playing(const Frame& f)
{
    int alive = 0;
    for (const Slot& actor : f.slots) {
        if (actor.animation == ServeIdle || actor.animation == Serve || actor.animation == Victory) return false;
        alive += actor.lives > 0;
    }
    return alive > 1 && f.slots[1].lives > 0;
}

struct View
{
    struct Sample { Frame frame; double time; std::uint32_t sequence; };
    Sample samples[6]{};
    int count = 0;
    double now = 0, received = 0, jumpUntil = 0;
    std::uint32_t sequence = 0, match = 0, jumpSequence = 0;
    bool moving = false, jumpPending = false;
    const char* jumpResolution = nullptr;
    Frame shown{};

    bool BeginJump()
    {
        if (!count || !moving || jumpPending || now - received > 0.15) return false;
        const Frame& latest = samples[count - 1].frame;
        if (!Playing(latest) || !Playing(shown) || latest.slots[1].animation != Idle ||
            shown.slots[1].animation != Idle || std::fabs(latest.slots[1].y) > 0.01f) return false;
        jumpSequence = sequence;
        jumpUntil = now + 0.30;
        jumpPending = true;
        return true;
    }

    void EndJump(const char* why)
    {
        if (jumpPending) jumpResolution = why;
        jumpPending = false;
    }

    // Fractional local render time advances between packets. The short queue
    // interpolates existing host poses and holds the last one on a network stall.
    bool Update(float dt, const Frame* packet, std::uint32_t serial, std::uint32_t game, bool inputEnabled = true)
    {
        if (dt > 0 && std::isfinite(dt)) now += dt;
        jumpResolution = nullptr;
        const std::uint32_t step = serial - sequence;
        const bool fresh = packet && serial && (match != game || !count || (step && step < 0x80000000u));
        if (fresh) {
            double stamp = now;
            if (!count || game != match || step > 6 || now - received >= 0.20) {
                count = 0;
                moving = false;
                EndJump("reset");
            }
            else {
                const Sample& previous = samples[count - 1];
                if (Playing(*packet) && std::fabs(Wrap(packet->angle - previous.frame.angle)) > 0.001f) moving = true;
                // Sequence spacing removes ordinary arrival-time jitter. Keep
                // the inferred clock near arrival time for older, slower hosts.
                stamp = previous.time + step / 30.0;
                if (stamp < now - 0.025) stamp = now - 0.025;
                if (stamp > now + 0.025) stamp = now + 0.025;
                if (stamp <= previous.time) stamp = previous.time + 0.001;
            }
            if (count == 6) {
                for (int i = 1; i < count; ++i) samples[i - 1] = samples[i];
                --count;
            }
            samples[count++] = {*packet, stamp, serial};
            match = game;
            sequence = serial;
            received = now;
        }
        if (!count) return false;

        const double cursor = now - 0.045;
        int lower = 0;
        while (lower + 1 < count && samples[lower + 1].time <= cursor) ++lower;
        const Sample& from = samples[lower];
        shown = from.frame;
        if (lower + 1 < count && cursor > from.time) {
            const Sample& to = samples[lower + 1];
            const float t = float((cursor - from.time) / (to.time - from.time));
            shown.angle = Wrap(from.frame.angle + Wrap(to.frame.angle - from.frame.angle) * t);
            for (int i = 0; i < 4; ++i) {
                const Slot& a = from.frame.slots[i];
                const Slot& b = to.frame.slots[i];
                const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                if (a.lives != b.lives || HardPose(a) || HardPose(b) || dx*dx + dy*dy + dz*dz > 9.0f) continue;
                shown.slots[i].x += dx * t;
                shown.slots[i].y += dy * t;
                shown.slots[i].z += dz * t;
            }
        }
        const Frame& latest = samples[count - 1].frame;
        for (int i = 0; i < 4; ++i) {
            // Hit, knockout and final poses also reset native position.
            if (shown.slots[i].lives != latest.slots[i].lives || HardPose(latest.slots[i]))
                shown.slots[i] = latest.slots[i];
        }
        if (jumpPending) {
            const auto actual = latest.slots[1].animation;
            const auto visible = shown.slots[1].animation;
            const std::uint32_t elapsedPackets = from.sequence - jumpSequence;
            if (!inputEnabled)
                EndJump("phase");
            else if (!Playing(latest) || actual == KickLeft || actual == KickRight || HardPose(latest.slots[1]))
                EndJump("host-override");
            else if (elapsedPackets && elapsedPackets < 0x80000000u &&
                     ((visible >= JumpStart && visible <= JumpEnd) || visible == JumpLiftoff))
                EndJump("host-jump");
            else if (now >= jumpUntil)
                EndJump("timeout");
            else
                shown.slots[1].animation = JumpStart;
        }
        return fresh;
    }
};
} // namespace MudGuest
