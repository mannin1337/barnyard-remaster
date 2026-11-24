#include "MiniGame/APoolLocalShot.h"
#include "Units/AUnitPoolCuePower.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace PoolLocal;

void Check(bool condition, const char* message)
{
    if (!condition) { std::printf("FAIL %s\n", message); std::exit(1); }
}

int main()
{
    constexpr float dt = 1.0f / 120.0f;
    Check(GetShootEdge(2,true,false,true,false) == ShootEdge::Press, "Space starts the charge");
    Check(GetShootEdge(2,false,true,true,false) == ShootEdge::None, "second bound button adds no new charge sample");
    Check(GetShootEdge(3,false,true,false,true) == ShootEdge::Release, "first release starts the stroke");
    Check(GetShootEdge(3,false,false,false,true) == ShootEdge::None, "second release cannot claim later ball motion");
    Check(GetShootEdge(1,true,false,true,true) == ShootEdge::None, "placement confirm is not a stroke");
    Check(GetShootEdge(2,true,false,true,true) == ShootEdge::Tap, "single frame charge and release");
    const Point forward{0, 1};
    for (const int fps : {15,20,23,30,60,120})
    {
        float power = 15, pull = 0.5f, speed = 0, elapsed = 0;
        int contacts = 0;
        for (int frame = 0; !contacts && frame < fps; ++frame)
        {
            const float frameDt = 1.0f/fps;
            elapsed += frameDt;
            contacts += AdvanceWindup(frameDt,[&](float step) {
                PoolCuePower::PullBack(power,pull,speed,15,2,300*step,step);
                return power <= -2 && speed < 0;
            });
        }
        std::printf("host_fps=%d contacts=%d seconds=%.4f\n", fps,contacts,elapsed);
        Check(contacts == 1 && elapsed <= 0.2f, "full-power contact even on a slow host");
    }
    Placement placement;
    const auto clamp = [](Point p) { return Point{std::fmin(0.025f,p.x),p.z}; };
    const auto firstPlace = placement.Update(7,{0,0},forward,{1,0},dt,clamp);
    Check(firstPlace.x > 0, "ball-in-hand moves on first guest frame");
    const Point quickConfirm = placement.Confirm();
    Check(quickConfirm.x == firstPlace.x && quickConfirm.x != 0, "quick release has current local placement");
    const auto waiting = placement.Update(7,{0,0},forward,{1,0},dt,clamp);
    Check(waiting.x == quickConfirm.x, "held direction cannot move the submitted placement");
    for (int i=0;i<55;++i) placement.Update(7,{0,0},forward,{1,0},dt,clamp);
    Check(placement.position.x == 0.025f, "placement respects native bounds while held");
    for (int i=0;i<160;++i) placement.Update(7,{0,0},forward,{},dt,clamp);
    Check(std::fabs(placement.position.x) < 0.0001f, "rejected position reconciles to host");
    Point nativeBall{-0.15f,0};
    const auto move = [&](Point p) { nativeBall = p; };
    const auto clear = [&]() { return nativeBall.x*nativeBall.x+nativeBall.z*nativeBall.z > 0.09f*0.09f; };
    Check(!PlaceIfClear(nativeBall,{0.04f,0},{0.04f,0},move,clear) && nativeBall.x == -0.15f,
        "overlapping another ball restores prior host position");
    Check(!PlaceIfClear(nativeBall,{0.45f,0},{0.3f,0},move,clear) && nativeBall.x == -0.15f,
        "out-of-bounds confirmation stays in ball-in-hand");
    Check(PlaceIfClear(nativeBall,{0.25f,0},{0.25f,0},move,clear) && nativeBall.x == 0.25f,
        "legal quick confirmation uses exact client position");
    float jitterPower=15, jitterPull=0, jitterSpeed=0;
    bool jitterContact = false;
    for (float frameDt : {0.0f,dt,0.8f,dt}) if (!jitterContact)
        jitterContact = AdvanceWindup(frameDt,[&](float step) {
            Check(step > 0 && step <= 1.0f/30, "zero/oversized native windup step");
            PoolCuePower::PullBack(jitterPower,jitterPull,jitterSpeed,15,2,300*step,step);
            return jitterPower <= -2 && jitterSpeed < 0;
        });
    Check(jitterContact, "occasional long frame cannot lose cue contact");
    Aim aim;
    const Point first = aim.Update(7, forward, forward, 1, dt, false, forward);
    Check(first.x > 0 && first.z > 0.99f, "first aim frame with a stationary host snapshot");
    for (int i = 0; i < 10; ++i) aim.Update(7, forward, forward, 1, dt, false, forward);
    Check(aim.direction.x > first.x, "held aim advances without a host packet");
    for (int i = 0; i < 160; ++i) aim.Update(7, forward, forward, 0, dt, false, forward);
    Check(std::fabs(aim.direction.x) < 0.0001f, "stopped aim converges to authority");
    const Point boundary = aim.Update(8, {1, 0}, {1, 0}, 1, dt, true, forward);
    Check(boundary.z >= 0, "ball-in-hand forward-only rule");

    Stroke stroke;
    Check(stroke.Release(7, {3, 2}, forward, 0.6f, 0.09f), "first eligible release");
    Check(!stroke.Release(7, {3, 2}, {1, 0}, 1, 1), "duplicate release cannot restart or retarget stroke");
    auto pose = stroke.Advance(dt);
    Check(pose.followThrough > 0, "cue moves on first release frame without an acknowledgement");
    for (int i = 0; i < 10; ++i) pose = stroke.Advance(dt);
    Check(pose.ball.z > 2.01f && pose.ball.z <= 2.091f, "cue ball starts locally inside swept clearance");
    const float travelled = pose.ball.z - 2;
    Check(stroke.WhileWaiting(7, true, false), "stroke survives host windup in original serial");
    Check(stroke.WhileWaiting(8, false, true), "native shot counter advances at cue contact");
    const auto held = stroke.Reconcile({3,2.01f});
    Check(held.z >= pose.ball.z - 0.0001f, "first late authoritative contact does not rewind ball");
    const auto caught = stroke.Reconcile({3,2.11f});
    Check(std::fabs(caught.z-2.11f) < 0.0001f, "authority takes over after catching up");
    Check(std::fabs(stroke.Reconcile({3,2.04f}).z-2.04f) < 0.0001f, "return after a rebound stays authoritative");
    Check(!stroke.WhileWaiting(8, true, false), "new player's turn cannot inherit strike");
    stroke = {};
    stroke.Release(10, {0,0}, forward, 1, 0);
    for (int i = 0; i < 20; ++i) pose = stroke.Advance(dt);
    Check(pose.ball.z == 0, "contact at release prevents crossing a ball or cushion");
    for (int i = 0; i < 120; ++i) stroke.Advance(dt);
    Check(!stroke.WhileWaiting(10, true, false), "unacknowledged stroke expires");
    std::printf("PASS first-frame aim=%.5f placement=%.5f cue stroke, predicted ball=%.3f; quick place, overlap rollback, jitter, duplicate and turn reset\n",first.x,quickConfirm.x,travelled);
}
