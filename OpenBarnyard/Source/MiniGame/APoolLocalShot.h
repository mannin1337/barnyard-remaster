#pragma once
#include <cmath>

namespace PoolLocal
{
enum class ShootEdge { None, Press, Release, Tap };

inline ShootEdge GetShootEdge(unsigned stage, bool idle, bool heldThisShot, bool down, bool up)
{
    const bool press = stage == 2 && idle && down;
    if (up && (press || heldThisShot)) return press ? ShootEdge::Tap : ShootEdge::Release;
    if (press) return ShootEdge::Press;
    return ShootEdge::None;
}

struct Point { float x = 0, z = 0; };

inline Point Unit(Point p)
{
    const float length = std::sqrt(p.x * p.x + p.z * p.z);
    return length > 0.00001f ? Point{p.x/length, p.z/length} : Point{};
}

inline Point Rotate(Point p, float cosine, float sine)
{
    return {cosine*p.x + sine*p.z, cosine*p.z - sine*p.x};
}

struct Aim
{
    Point direction{};
    unsigned serial = 0;
    float speed = 0.007f, previousAxis = 0, idle = 0;

    Point Update(unsigned shot, Point host, Point camera, float axis, float dt, bool forwardOnly, Point forward)
    {
        host = Unit(host);
        if (shot != serial)
        {
            *this = {};
            serial = shot;
            direction = host;
        }
        dt = std::fmin(0.05f, std::fmax(0.0f, dt));
        if (std::fabs(axis) > 0.0001f)
        {
            if (axis * previousAxis < 0) speed = 0.007f;
            camera = Unit(camera);
            // Retail moves the target along camera-right and projects back onto
            // the unit circle. Integrate at its 60 Hz reference speed.
            const float step = axis * speed * dt * 60;
            const Point next = Unit({direction.x + camera.z*step, direction.z - camera.x*step});
            if (!forwardOnly || next.x*forward.x + next.z*forward.z >= 0)
                direction = next;
            speed = std::fmin(0.05f, speed + dt*0.015f);
            idle = 0;
        }
        else
        {
            idle += dt;
            if (idle > 0.12f)
            {
                // Allow in-flight aim inputs to reach the host before settling.
                const float mix = 1 - std::exp(-dt * 18);
                const float yaw = std::atan2(host.x*direction.z-host.z*direction.x,
                    direction.x*host.x+direction.z*host.z) * mix;
                direction = Rotate(direction, std::cos(yaw), std::sin(yaw));
            }
            if (idle >= 0.5f) speed = 0.007f;
        }
        previousAxis = axis;
        return direction;
    }
};

struct Stroke
{
    struct Pose { Point ball; float followThrough; };
    unsigned serial = 0;
    Point origin{}, direction{};
    float power = 0, clearance = 0, age = 0, travel = 0;
    bool caughtUp = false;
    bool Release(unsigned shot, Point ball, Point heading, float strength, float distance)
    {
        if (!shot || shot == serial) return false;
        serial = shot; origin = ball; direction = heading; power = strength; clearance = distance;
        age = travel = 0;
        caughtUp = false;
        return true;
    }
    Pose Advance(float dt)
    {
        age += dt;
        // A brief forward cue stroke precedes contact. Stop the visual ball
        // at the first obstacle; only native host physics can resolve a hit.
        const float follow = std::fmin(1.0f, age / 0.04f);
        travel = std::fmin(clearance, std::fmax(0.0f, age - 0.04f) * power * 15.0f);
        return {{origin.x + direction.x * travel, origin.z + direction.z * travel}, follow};
    }
    bool WhileWaiting(unsigned shot, bool original, bool follow) const
    {
        return serial && age <= 0.65f && ((shot == serial && original) || (shot == serial+1 && follow));
    }
    Point Reconcile(Point host)
    {
        const float x = host.x-origin.x, z = host.z-origin.z;
        const float along = x*direction.x + z*direction.z;
        if (along >= travel || age >= 0.35f || std::fabs(x*direction.z-z*direction.x) > 0.03f) caughtUp = true;
        if (caughtUp) return host;
        // A contact snapshot can carry the new shot counter before native
        // physics has stepped. Hold the small visual lead while it catches up.
        const float blend = std::fmin(1.0f, std::fmax(0.0f, (0.35f-age)/0.15f));
        const float lead = (travel-along)*blend;
        return {host.x + direction.x*lead, host.z + direction.z*lead};
    }
};

struct Placement
{
    Point position{};
    unsigned serial = 0;
    float idle = 0, confirmationWait = 0;

    Point Confirm()
    {
        confirmationWait = 0.25f;
        return position;
    }
    template<class Clamp>
    Point Update(unsigned shot, Point host, Point camera, Point axes, float dt, Clamp clamp)
    {
        if (serial != shot) { *this = {}; serial = shot; position = host; }
        dt = std::fmin(0.05f,std::fmax(0.0f,dt));
        if (confirmationWait > 0)
        {
            confirmationWait -= dt;
            idle = 0.12f;
            return position;
        }
        if (std::fabs(axes.x) + std::fabs(axes.z) > 0.0001f)
        {
            camera = Unit(camera);
            const float step = 0.007f*60*dt;
            position.x += (camera.z*axes.x - camera.x*axes.z)*step;
            position.z += (-camera.x*axes.x - camera.z*axes.z)*step;
            idle = 0;
        }
        else if ((idle += dt) > 0.12f)
        {
            const float mix = 1-std::exp(-dt*18);
            position.x += (host.x-position.x)*mix;
            position.z += (host.z-position.z)*mix;
        }
        return position = clamp(position);
    }
};

template<class Move, class IsClear>
bool PlaceIfClear(Point previous, Point requested, Point bounded, Move move, IsClear isClear)
{
    const float x = requested.x-bounded.x, z = requested.z-bounded.z;
    if (x*x+z*z > 0.000001f) return false;
    move(bounded);
    if (isClear()) return true;
    move(previous);
    return false;
}

template<class Step>
bool AdvanceWindup(float dt, Step step)
{
    if (!(dt > 0)) return false;
    if (dt <= 1.0f/30) return step(dt);
    // The retail recurrence reverses direction or stalls at large dt.
    // A full-power strike completes well within this bounded catch-up.
    for (float remaining = std::fmin(dt,0.25f); remaining > 0;)
    {
        const float part = std::fmin(remaining,1.0f/60);
        if (step(part)) return true;
        remaining -= part;
    }
    return false;
}
}
