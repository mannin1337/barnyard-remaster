#include "Player/ACoopMinigameProtocol.h"
#include "Player/ACoopMinigameTiming.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace CoopMini;

static void Check(bool passed, const char* description)
{
    if (passed) return;
    std::fprintf(stderr, "FAIL %s\n", description);
    std::exit(1);
}

static void CheckDarts()
{
    State state;
    state.game = Darts; state.phase = Playing; state.player = 1; state.match = 7;
    const DartsTurn turn{1, 0, 4, 2};
    DartsTimedInputPacket click{{DARTS_INPUT_MAGIC, 9, 5, 7, 1, 4, 2}, 208, 151};
    DartsInputBuffer received;
    auto accept = [&](bool negotiated) {
        return AcceptDartsInput(state, 0, turn, &click, sizeof(click), 9, 5, 1000, received, true, negotiated);
    };
    Check(!accept(false), "legacy peer cannot submit timed input");
    click.stageTimeMs = 99;
    Check(!accept(true), "early second axis rejected");
    click.stageTimeMs = 60001;
    Check(!accept(true), "excessive timestamp rejected");
    click.stageTimeMs = 151;
    Check(accept(true), "negotiated second click accepted");
    U32 ref = 0, timeMs = 0;
    Check(ConsumeDartsInput(state, turn, received, 1010, &ref, &timeMs) && ref == 208 && timeMs == 151,
          "host receives exact first reference and local lock time");
    Check(!ConsumeDartsInput(state, turn, received, 1011, &ref, &timeMs) && !ref && !timeMs, "click applies once");
    click.input.stage = 1;
    const DartsTurn first{1, 0, 4, 1};
    Check(!AcceptDartsInput(state, 0, first, &click, sizeof(click), 9, 5, 1000, received, true, true),
          "timed input only on second axis");
    DartsBlendedInputPacket blended{{DARTS_INPUT_MAGIC, 9, 5, 7, 2, 4, 1}, 210, 208, 32768};
    received = {};
    auto blendClick = [&](bool negotiated) {
        return AcceptDartsInput(state,0,first,&blended,sizeof(blended),9,5,1100,received,true,true,negotiated);
    };
    Check(!blendClick(false), "first-axis interpolation requires a negotiated peer");
    blended.blend = 65536;
    Check(!blendClick(true), "out-of-range blend rejected");
    blended.blend = 32768; blended.previousView = 211;
    Check(!blendClick(true), "reversed snapshot pair rejected");
    blended.previousView = 208;
    Check(blendClick(true), "both source frames may straddle a lost packet");
    U32 earlier = 0, fraction = 0;
    Check(ConsumeDartsInput(state,first,received,1101,&ref,&timeMs,&earlier,&fraction) &&
          ref == 210 && earlier == 208 && fraction == 32768 && timeMs == 0,
          "first axis preserves exactly what the player saw before clicking");
}

static void CheckPool()
{
    State state;
    state.game = Pool; state.phase = Playing; state.player = 1; state.match = 11;
    PoolTurn turn{1, 3, 2};
    PoolShotInputPacket packet;
    packet.input = {POOL_INPUT_MAGIC, 9, 5, 11, 2, 3, 1, 0, 0, 1, 1};
    packet.shot = {0.6f, 0.8f, 0.73f};
    PoolInputBuffer received;
    PoolControls controls;
    auto accept = [&](bool negotiated) {
        return AcceptPoolInput(state, 0, turn, &packet, sizeof(packet), 9, 5, 2000, received, negotiated);
    };
    Check(!accept(false), "legacy peer cannot submit a shot extension");
    packet.shot.power = std::numeric_limits<float>::quiet_NaN();
    Check(!accept(true), "NaN power rejected");
    packet.shot.power = 0.73f;
    packet.shot.dirX = 2;
    Check(!accept(true), "invalid aim vector rejected");
    packet.shot.dirX = 0.6f;
    Check(accept(true), "release recovers from a lost press packet");
    auto heartbeat = packet.input;
    ++heartbeat.sequence;
    Check(AcceptPoolInput(state, 0, turn, &heartbeat, sizeof(heartbeat), 9, 5, 2010, received, true),
          "next heartbeat accepted before simulation");
    Check(ConsumePoolInput(state,turn,received,2015,controls) && controls.shootPressed && controls.shootReleased &&
          controls.hasShotIntent && controls.shot.dirX == 0.6f && controls.shot.dirZ == 0.8f && controls.shot.power == 0.73f,
          "coalesced edges retain selected aim and power");
    Check(!accept(true), "old release cannot overwrite newer packet");
    Check(ConsumePoolInput(state,turn,received,2020,controls) && !controls.shootReleased && !controls.hasShotIntent,
          "a heartbeat cannot cause a duplicate stroke");
    packet.input.sequence += 2;
    ++packet.input.inputEpoch;
    packet.input.shootReleases = 0;
    Check(!accept(true), "intent requires a release");
    ++packet.input.shotSerial;
    Check(!accept(true), "wrong shot rejected");
    PoolPlaceInputPacket place;
    place.input = {POOL_INPUT_MAGIC, 9, 5, 11, 5, 4, 1, 0, 0, 1, 1};
    place.placement = {0.3f, -0.4f};
    turn.serial = 4;
    auto acceptPlace = [&] { return AcceptPoolInput(state,0,turn,&place,sizeof(place),9,5,2050,received,true); };
    Check(!acceptPlace(), "cannot place the white ball during a stroke");
    turn.stage = 1;
    place.placement.x = 0.8f;
    Check(!acceptPlace(), "placement outside the native radius rejected");
    place.placement.x = 0.3f;
    Check(acceptPlace(), "placement relative to the native starting spot");
    Check(ConsumePoolInput(state,turn,received,2052,controls) && controls.hasPlacementIntent &&
          !controls.hasShotIntent && controls.placement.x == 0.3f && controls.placement.z == -0.4f,
          "place without shooting, preserving the selected point");
    Check(ConsumePoolInput(state,turn,received,2053,controls) && !controls.hasPlacementIntent, "placement applies once");
}

static void CheckScores()
{
    State state;
    state.game = Chicks; state.phase = Playing; state.match = 1; state.turn = 3;
    state.scores[0] = 2; state.scores[1] = 5;
    Command pending{Score, 1, 3, 9, 0, 8};
    Check(DisplayedScore(state, pending, 1, 1) == 8, "local score visible before a round trip");
    Check(DisplayedScore(state, pending, 1, 0) == 2 && state.scores[1] == 5, "peer and authoritative scores preserved");
    state.turn = 4;
    Check(DisplayedScore(state, pending, 1, 1) == 5, "old local estimate expires");
    state.turn = 3; state.phase = Results;
    Check(DisplayedScore(state, pending, 1, 1) == 5, "results are authoritative");
}

static void CheckPeerRestart()
{
    State guest;
    guest.match = 1;
    NativeMatch native{1,1,2,true};
    State host;
    Check(Begin(host, Darts, 0), "restarted host offers its first match");
    Check(native.ReplacePeer(guest,false,false), "replacement host can bind on the farm");
    Check(Newer(host,guest), "first offer of the replacement host accepted");
    guest = host;
    Check(Apply(host,1,{Accept,1}), "replacement host accepts joiner");
    Check(Newer(host,guest), "replacement host publishes loading");
    guest = host;
    Check(native.NeedsLoad(guest) && !native.ready && native.playedTurn == ~0u && !native.returnStarted,
          "guest must reload match 1 when replacement host starts at 1");
    native.loaded = native.ready = guest.match;
    Check(!native.NeedsLoad(guest), "same match cannot load twice");
    Check(!native.ReplacePeer(guest,false,false) && native.ready == 1, "active session cannot be rebound");
    host.phase = Idle;
    Check(Begin(host, Pool, 0) && host.match == 2 && Apply(host,1,{Accept,2}), "same host starts match 2");
    Check(native.NeedsLoad(host), "ordinary next match still loads");

    guest.phase = Idle;
    guest.farm = 3;
    Check(!native.ReplacePeer(guest,false,true) && guest.phase == Leaving && guest.farm == 0 && native.loaded == 1,
          "late idle waits for the original native level to return");
    guest.phase = Idle;
    Check(native.ReplacePeer(guest,true,false) && guest.match == 1 && !native.ready, "replacing guest preserves host's IDs");
    Check(Begin(guest,Pool,0) && guest.match == 2, "existing host keeps allocating increasing IDs");
}

int main()
{
    static_assert(sizeof(DartsInputPacket) == 28 && sizeof(DartsViewInputPacket) == 32 && sizeof(DartsTimedInputPacket) == 36);
    static_assert(sizeof(DartsBlendedInputPacket) == 40);
    static_assert(sizeof(PoolInputPacket) == 44 && sizeof(PoolShotInputPacket) == 56);
    static_assert(sizeof(PoolPlaceInputPacket) == 52);
    CheckDarts();
    CheckPool();
    CheckScores();
    CheckPeerRestart();
    std::puts("PASS: negotiated clicks and shots, loss, reorder, local scores, host restart and native return");
}
