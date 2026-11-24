#include "MiniGame/AGateCrashLocalInput.h"
#include "Player/ACoopMinigameProtocol.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace MudGuest;
static int checks;

static void Check(bool passed, const char* message)
{
    ++checks;
    if (!passed) { std::printf("FAIL: %s\n", message); std::exit(1); }
}

static InputFrame Ready()
{
    InputFrame packet{};
    packet.arena.count = 4;
    for (auto& slot : packet.arena.slots) slot.lives = 3;
    packet.state = LocalState::Ready;
    packet.gateSpeed = 3;
    return packet;
}

static float JumpDuringLoss(int fps, bool held)
{
    LocalInput input{};
    auto f = Ready();
    const float dt = 1.0f / fps;
    input.Update(dt, &f, 1, true, false, false);
    Check(input.Begin(LocalState::Jumping, 1), "jump accepted at first playable snapshot");
    input.Update(dt, nullptr, 1, true, held, false);
    Check(input.shown.animation == JumpStart, "local jump pose on first tick with no echo");
    Check(!input.Begin(LocalState::Jumping, 2), "already airborne cannot predict another jump");
    // No host packet at all while the guest plays its jump_start and takes off.
    for (int i = 0; i < fps / 5; ++i)
        input.Update(dt, nullptr, 1, true, held, i < fps / 20);
    const float projectedY = input.shown.y;
    Check(projectedY < -0.7f && input.shown.lives == 3, "local height after windup with lost frames");
    input.Update(dt, &f, 1, true, held, false);
    Check(input.shown.y < -0.7f, "in-flight idle snapshot leaves local jump intact");
    f.jumpPresses = 1;
    f.state = LocalState::Jumping;
    f.arena.slots[1].animation = JumpUp;
    f.arena.slots[1].x = 0.12f;
    f.arena.slots[1].y = -0.45f;
    f.velocity = 8;
    input.Update(dt, &f, 1, true, held, false);
    Check(input.shown.y < -0.45f && input.shown.x == 0.12f, "accepted jump reconciles height and ground position");
    f.state = LocalState::Locked;
    f.arena.slots[1].y = 0;
    f.arena.slots[1].animation = Contact;
    --f.arena.slots[1].lives;
    input.Update(dt, &f, 1, true, held, false);
    Check(input.shown.y == 0 && input.shown.lives == 2 && input.shown.animation == Contact,
          "host hit overrides projected position, life and animation immediately");
    Check(!input.Begin(LocalState::Kicking, 1), "stunned guest cannot kick");
    return projectedY;
}

int main()
{
    float low = 0, high = 0;
    for (int fps : {30, 60, 120, 144}) {
        const float tap = JumpDuringLoss(fps, false);
        const float held = JumpDuringLoss(fps, true);
        Check(held < tap, "held jump rises above a tap");
        if (fps == 30) low = held;
        if (fps == 144) high = held;
        std::printf("guest %d FPS, 200 ms without snapshots: tap %.3f, held %.3f\n", fps, tap, held);
    }
    Check(std::fabs(low - high) < .22f, "30 and 144 FPS jump arcs agree within one slow frame");
    LocalInput late{};
    auto echoed = Ready();
    late.Update(0, &echoed, 8, true, false, false);
    late.Begin(LocalState::Jumping, 1);
    for (int t = 0; t < 52; ++t)
        late.Update(.01f, t % 5 == 0 ? &echoed : nullptr, 8, true, false, t < 4);
    echoed.state = LocalState::Jumping;
    echoed.jumpPresses = 1;
    echoed.actionTime = .075f;
    echoed.arena.slots[1].y = -.46f;
    echoed.arena.slots[1].animation = JumpUp;
    echoed.velocity = 10;
    late.Update(.01f, &echoed, 8, true, false, false);
    Check(late.shown.animation == JumpDown || late.shown.animation == JumpEnd,
          "late host liftoff cannot play a second ascent after the local descent");
    LocalInput input{};
    auto f = Ready();
    input.Update(.01f, &f, 1, true, false, false);
    Check(input.Begin(LocalState::Kicking, 1), "automatic kick accepted");
    input.Update(.01f, nullptr, 1, true, false, false);
    Check(input.shown.animation == KickLeft, "automatic kick follows incoming gate on first local frame");
    input.Update(.19f, &f, 1, true, false, false);
    Check(input.shown.animation == KickLeft, "stale unacknowledged idle does not erase kick");
    f.kickPresses = 1;
    input.Update(.01f, &f, 1, true, false, false);
    Check(input.shown.animation == Idle, "acknowledged rejected kick returns to host pose");
    Check(input.Begin(LocalState::Kicking, 2, true, false), "next directional kick accepted");
    input.Update(.01f, nullptr, 1, true, false, false);
    Check(input.shown.animation == KickRight, "right kick starts locally on first frame");
    f.kickPresses = 2;
    f.state = LocalState::Kicking;
    f.arena.slots[1].animation = KickRight;
    input.Update(.02f, &f, 1, true, false, false);
    Check(input.shown.animation == KickRight && !input.Begin(LocalState::Kicking, 3),
          "confirmed kick retains same pose and respects host recovery");
    f = Ready();
    input.Update(.01f, &f, 2, true, false, false);
    Check(input.Begin(LocalState::Jumping, 1) && input.Begin(LocalState::Kicking, 1, true, true),
          "simultaneous jump/kick uses native kick priority");
    input.Update(.01f, nullptr, 2, true, false, false);
    Check(input.shown.animation == KickLeft, "left kick starts on first frame");
    input.Update(.01f, nullptr, 2, false, false, false);
    Check(input.shown.animation == Idle, "result phase cancels pending action without packets");
    input.Update(.51f, nullptr, 2, true, false, false);
    Check(!input.Begin(LocalState::Jumping, 2), "stalled session cannot start speculative actions indefinitely");
    f.state = static_cast<LocalState>(99);
    Check(!f.Valid(), "impossible authority state rejected");

    // The guest can release before a slower host runs. Dropping the down
    // packet must deliver one short tap with the next cumulative heartbeat.
    CoopMini::State game;
    game.match = 9; game.game = CoopMini::Mud; game.phase = CoopMini::Playing;
    CoopMini::MudInputPacket wire;
    wire.match = 9; wire.sender = 4; wire.receiver = 7; wire.jumpPresses = 1;
    wire.sequence = 2;
    CoopMini::MudInputBuffer buffer{};
    CoopMini::MudControls controls;
    Check(CoopMini::AcceptMudInput(game, 0, &wire, sizeof(wire), 4, 7, 33, buffer), "release heartbeat survives down loss");
    Check(CoopMini::ConsumeMudInput(game, buffer, 34, controls) && controls.jumpPressed && !controls.jumpHeld, "host receives short tap once");
    Check(!CoopMini::AcceptMudInput(game, 0, &wire, sizeof(wire), 4, 7, 36, buffer), "duplicate delivery discarded");
    Check(CoopMini::ConsumeMudInput(game, buffer, 66, controls) && !controls.jumpPressed, "duplicate host action suppressed");
    std::printf("PASS %d checks\n", checks);
}
