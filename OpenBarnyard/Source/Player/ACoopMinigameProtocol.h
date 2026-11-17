#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// Engine-independent state reducer. Channel 8 retransmits its entire state;
// commands may be lost, duplicated or reordered by UDP.
namespace CoopMini
{
using U32 = std::uint32_t;
enum Game : U32 { Chicken = 1, Whack, Golf, Mud, Darts, Mailman, Beady, Chicks, Veggie,
    Bike, JoyRide, CowTipping, Barntender, Pool, LastGame = Pool };
enum Feature : U32 { Spectator = 1, ExpandedGames = 2, MudRealtime = 4, DartsPvp = 8, AllMinigames = 16, PoolPvp = 32,
    GolfGroundView = 64, DartsViewAim = 128 };
enum Phase : U32 { Idle, Offer, Loading, Playing, Results, Leaving };
enum Action : U32 { None, Invite, Accept, Reject, Ready, Score, RoundDone, Leave, Farm };
struct State
{
    U32 match = 0, revision = 0, phase = Idle, game = Chicken, inviter = 0;
    U32 turn = 0, player = 0, accepted = 0, ready = 0, finished = 0, farm = 0;
    std::int32_t scores[2] = {};
};
struct Command
{
    U32 action = None, match = 0, turn = 0, sequence = 0, final = 0;
    std::int32_t score = 0;
};
struct ViewStamp
{
    U32 match = 0, turn = 0, game = 0, sequence = 0;
};
inline bool ValidGame(U32 game) { return game >= Chicken && game <= LastGame; }
inline bool HostGame(U32 game) { return game == Mud || game == Darts || game == Pool; }
inline bool DuelGame(U32 game) { return game >= Mailman && game <= Barntender; }
inline bool LowerScoreWins(U32 game) { return game == Bike || game == JoyRide; }
inline int ResultWinner(const State& s)
{
    if (s.game == Pool) return int(s.player); // Native eight-ball rules decide, including fouls.
    if (s.scores[0] == s.scores[1]) return -1;
    return (LowerScoreWins(s.game) ? s.scores[0] < s.scores[1] : s.scores[0] > s.scores[1]) ? 0 : 1;
}
inline bool PeerCanPlay(U32 game, U32 features)
{
    const U32 mudFeatures = Spectator | ExpandedGames | MudRealtime;
    const U32 dartsFeatures = Spectator | ExpandedGames | DartsPvp;
    const U32 poolFeatures = Spectator | AllMinigames | PoolPvp;
    return ValidGame(game) && (DuelGame(game) ? (features & AllMinigames) != 0 :
        game == Pool ? (features & poolFeatures) == poolFeatures :
        game == Darts ? (features & dartsFeatures) == dartsFeatures :
        game == Mud ? (features & mudFeatures) == mudFeatures :
        (game <= Golf || (features & ExpandedGames)));
}
inline bool CanWatchView(const State& s, U32 viewer, const ViewStamp& frame)
{
    const bool audience = HostGame(s.game) ? viewer == 1 && (s.phase == Playing || s.phase == Results) :
        viewer < 2 && s.phase == Playing && s.player == 1u - viewer;
    return !DuelGame(s.game) && audience && frame.sequence != 0 &&
        frame.match == s.match && frame.turn == s.turn && frame.game == s.game;
}
// Compare sequences only within the turn already validated by CanWatchView.
inline bool NewerView(const ViewStamp& incoming, const ViewStamp& previous)
{
    return incoming.match != previous.match || incoming.turn != previous.turn ||
        incoming.game != previous.game || std::int32_t(incoming.sequence - previous.sequence) > 0;
}
inline bool ValidScore(std::int32_t score) { return score >= 0 && score <= 10000000; }
inline bool Valid(const State& s)
{
    return s.phase <= Leaving && ValidGame(s.game) && s.player < 2 &&
        s.inviter < 2 && s.accepted <= 3 && s.ready <= 3 && s.finished <= 3 && s.farm <= 3 &&
        ValidScore(s.scores[0]) && ValidScore(s.scores[1]) && (s.phase == Idle || s.match != 0);
}
inline bool Begin(State& s, U32 game, U32 inviter)
{
    if (s.phase != Idle || !ValidGame(game) || inviter > 1) return false;
    const U32 id = s.match + 1;
    s = State{};
    s.match = id ? id : 1;
    s.revision = 1;
    s.phase = Offer;
    s.game = game;
    s.inviter = inviter;
    s.accepted = 1u << inviter;
    return true;
}
inline bool Apply(State& s, U32 player, const Command& c)
{
    if (player > 1 || c.match != s.match || s.phase == Idle) return false;
    const U32 bit = 1u << player;
    bool changed = false;
    switch (c.action)
    {
        case Accept:
            if (s.phase != Offer || (s.accepted & bit)) return false;
            s.accepted |= bit;
            if (s.accepted == 3) s.phase = Loading;
            changed = true;
            break;
        case Reject:
        case Leave:
            if (s.phase == Leaving) return false;
            s.phase = Leaving;
            changed = true;
            break;
        case Ready:
            if (s.phase != Loading || (s.ready & bit)) return false;
            s.ready |= bit;
            if (s.ready == 3) s.phase = Playing;
            changed = true;
            break;
        case Score:
        case RoundDone:
            if (HostGame(s.game) || s.phase != Playing || c.turn != s.turn || !ValidScore(c.score) ||
                (DuelGame(s.game) ? (s.finished & bit) != 0 : s.player != player)) return false;
            if (DuelGame(s.game) && c.action == RoundDone && !c.final) return false;
            if (c.action == Score && s.scores[player] == c.score) return false;
            s.scores[player] = c.score;
            if (c.action == RoundDone)
            {
                if (c.final) s.finished |= bit;
                if (!DuelGame(s.game)) ++s.turn;
                if (s.finished == 3) s.phase = Results;
                else if (!DuelGame(s.game) && !(s.finished & (1u << (1u - player)))) s.player = 1u - player;
            }
            changed = true;
            break;
        case Farm:
            if (s.phase != Leaving || (s.farm & bit)) return false;
            s.farm |= bit;
            changed = true;
            break;
        default: break;
    }
    if (changed) ++s.revision;
    return changed;
}
// Both participants play in the same Mud simulation. Only its host commits the
// two final totals; no extra command fields or changes to the v1 packet layout.
inline bool SetMudScores(State& s, U32 player, std::int32_t hostScore, std::int32_t guestScore)
{
    if (player != 0 || s.game != Mud || s.phase != Playing ||
        !ValidScore(hostScore) || !ValidScore(guestScore) ||
        (s.scores[0] == hostScore && s.scores[1] == guestScore)) return false;
    s.scores[0] = hostScore;
    s.scores[1] = guestScore;
    ++s.revision;
    return true;
}
inline bool CompleteMud(State& s, U32 player, std::int32_t hostScore, std::int32_t guestScore)
{
    if (player != 0 || s.game != Mud || s.phase != Playing ||
        !ValidScore(hostScore) || !ValidScore(guestScore)) return false;
    s.scores[0] = hostScore;
    s.scores[1] = guestScore;
    s.finished = 3;
    s.phase = Results;
    ++s.revision;
    return true;
}
// Track command ordering even for idempotent no-ops. A repeated score with a
// newer serial must prevent an older, different score from going backwards.
inline bool ApplyOrdered(State& s, U32 player, const Command& c, U32& lastSerial)
{
    if (player > 1 || c.match != s.match || c.sequence <= lastSerial) return false;
    lastSerial = c.sequence;
    return Apply(s, player, c);
}

// A nonce is a process/session incarnation, not an identity or secret. Matching
// both ends prevents delayed packets from an old partner being replayed here.
inline bool Bound(U32 sender, U32 receiver, U32 remote, U32 local)
{
    return sender != 0 && receiver != 0 && sender == remote && receiver == local;
}

struct DartsTurn
{
    U32 player = 0, round = 0, serial = 0, stage = 0;
};
inline bool SetDartsState(State& s, U32 authority, DartsTurn& current, U32 player,
    U32 round, U32 serial, U32 stage, std::int32_t hostScore, std::int32_t guestScore)
{
    if (authority != 0 || s.game != Darts || s.phase != Playing || player > 1 ||
        round > 2 || (!serial && stage) || stage > 2 || !ValidScore(hostScore) || !ValidScore(guestScore)) return false;
    const bool changed = current.player != player || current.round != round || current.serial != serial ||
        current.stage != stage || s.scores[0] != hostScore || s.scores[1] != guestScore;
    if (!changed) return false;
    current = {player, round, serial, stage};
    s.player = player;
    s.turn = round;
    s.scores[0] = hostScore;
    s.scores[1] = guestScore;
    ++s.revision;
    return true;
}
inline bool CompleteDarts(State& s, U32 authority, std::int32_t hostScore, std::int32_t guestScore)
{
    if (authority != 0 || s.game != Darts || s.phase != Playing ||
        !ValidScore(hostScore) || !ValidScore(guestScore)) return false;
    s.scores[0] = hostScore;
    s.scores[1] = guestScore;
    s.finished = 3;
    s.phase = Results;
    ++s.revision;
    return true;
}
constexpr U32 DARTS_INPUT_MAGIC = 0x38445942; // BYD8, negotiated by DartsPvp
struct DartsInputPacket
{
    U32 magic = DARTS_INPUT_MAGIC, sender = 0, receiver = 0;
    U32 match = 0, sequence = 0, throwSerial = 0, stage = 0;
};
struct DartsInputBuffer
{
    DartsInputPacket packet;
    U32 consumedSerial = 0, consumedStage = 0, consumedSequence = 0, tick = 0;
    U32 viewSequence = 0;
    bool received = false;
};
// Optional negotiated extension; the legacy click remains exactly 28 bytes.
struct DartsViewInputPacket
{
    DartsInputPacket input;
    U32 viewSequence = 0;
};
// A bad view reference may be retried by a NEW click. Keep packet/sequence
// counters so the rejected click's heartbeat cannot become another attempt.
inline bool DiscardDartsViewInput(DartsInputBuffer& input, U32 serial, U32 stage)
{
    if (!input.viewSequence || input.consumedSerial != serial || input.consumedStage != stage) return false;
    input.consumedSerial = input.consumedStage = 0;
    return true;
}
inline bool DartsInputTurn(const State& s, const DartsTurn& current, const DartsInputPacket& p)
{
    return s.game == Darts && s.phase == Playing && s.player == 1 && current.player == 1 &&
        p.match == s.match && p.throwSerial != 0 && p.throwSerial == current.serial &&
        p.stage >= 1 && p.stage <= 2 && p.stage == current.stage;
}
inline bool AcceptDartsInput(const State& s, U32 localPlayer, const DartsTurn& current,
    const void* data, std::size_t size, U32 remote, U32 local, U32 now, DartsInputBuffer& input, bool allowView = false)
{
    const bool extended = allowView && size == sizeof(DartsViewInputPacket);
    if (!data || (size != sizeof(DartsInputPacket) && !extended) || localPlayer != 0) return false;
    U32 viewSequence = 0;
    if (extended)
    {
        DartsViewInputPacket view;
        std::memcpy(&view, data, sizeof(view));
        viewSequence = view.viewSequence;
        if (!viewSequence) return false;
    }
    DartsInputPacket p;
    std::memcpy(&p, data, sizeof(p));
    if (p.magic != DARTS_INPUT_MAGIC || !Bound(p.sender,p.receiver,remote,local) ||
        !p.sequence || !DartsInputTurn(s,current,p)) return false;
    if (input.received && (input.packet.match != p.match || std::int32_t(p.sequence-input.packet.sequence) <= 0)) return false;
    if (input.consumedSerial == p.throwSerial && input.consumedStage == p.stage) return false;
    input.packet = p;
    input.viewSequence = viewSequence;
    input.tick = now;
    input.received = true;
    return true;
}
inline bool ConsumeDartsInput(const State& s, const DartsTurn& current, DartsInputBuffer& input, U32 now, U32* viewSequence = nullptr)
{
    if (viewSequence) *viewSequence = 0;
    if (!input.received || input.consumedSequence == input.packet.sequence ||
        (input.consumedSerial == input.packet.throwSerial && input.consumedStage == input.packet.stage)) return false;
    const bool apply = DartsInputTurn(s,current,input.packet) && now-input.tick <= 500;
    // Even an expired or now-mismatched edge is spent. A heartbeat cannot
    // resurrect it in a later native aiming stage or throw.
    input.consumedSequence = input.packet.sequence;
    if (apply)
    {
        if (viewSequence) *viewSequence = input.viewSequence;
        input.consumedSerial = input.packet.throwSerial;
        input.consumedStage = input.packet.stage;
    }
    return apply;
}

struct PoolTurn
{
    U32 player = 0, serial = 0, stage = 0;
};
inline bool SetPoolState(State& s, U32 authority, PoolTurn& current, U32 player,
    U32 serial, U32 stage, std::int32_t hostScore, std::int32_t guestScore)
{
    if (authority != 0 || s.game != Pool || s.phase != Playing || player > 1 || !serial ||
        stage > 3 || !ValidScore(hostScore) || !ValidScore(guestScore)) return false;
    if (current.player == player && current.serial == serial && current.stage == stage &&
        s.scores[0] == hostScore && s.scores[1] == guestScore) return false;
    current = {player, serial, stage};
    s.player = player;
    s.turn = serial-1;
    s.scores[0] = hostScore;
    s.scores[1] = guestScore;
    ++s.revision;
    return true;
}
inline bool CompletePool(State& s, U32 authority, U32 winner, std::int32_t hostScore, std::int32_t guestScore)
{
    if (authority != 0 || s.game != Pool || s.phase != Playing || winner > 1 ||
        !ValidScore(hostScore) || !ValidScore(guestScore)) return false;
    s.scores[0] = hostScore;
    s.scores[1] = guestScore;
    s.player = winner;
    s.finished = 3;
    s.phase = Results;
    ++s.revision;
    return true;
}
constexpr U32 POOL_INPUT_MAGIC = 0x38505942; // BYP8, negotiated by PoolPvp
struct PoolInputPacket
{
    U32 magic = POOL_INPUT_MAGIC, sender = 0, receiver = 0;
    U32 match = 0, sequence = 0, shotSerial = 0;
    U32 inputEpoch = 1;
    float aimX = 0, aimY = 0;
    U32 shootPresses = 0, shootReleases = 0;
};
inline void UpdatePoolInput(PoolInputPacket& p, U32 serial, float aimX, float aimY, bool pressed, bool released)
{
    if (p.shotSerial != serial)
    {
        p.shootPresses = p.shootReleases = 0;
        p.shotSerial = serial;
    }
    p.aimX = aimX;
    p.aimY = aimY;
    if (pressed) ++p.shootPresses;
    if (released) ++p.shootReleases;
}
struct PoolControls
{
    float aimX = 0, aimY = 0;
    bool shootPressed = false, shootReleased = false;
};
struct PoolInputBuffer
{
    PoolInputPacket packet;
    U32 consumedPresses = 0, consumedReleases = 0, tick = 0;
    bool received = false, epochResetPending = false;
};
inline bool PoolInputTurn(const State& s, const PoolTurn& current, const PoolInputPacket& p)
{
    return s.game == Pool && s.phase == Playing && s.player == 1 && current.player == 1 &&
        current.stage >= 1 && current.stage <= 3 && p.match == s.match &&
        p.shotSerial != 0 && p.shotSerial == current.serial;
}
inline bool AcceptPoolInput(const State& s, U32 localPlayer, const PoolTurn& current,
    const void* data, std::size_t size, U32 remote, U32 local, U32 now, PoolInputBuffer& input)
{
    if (!data || size != sizeof(PoolInputPacket) || localPlayer != 0) return false;
    PoolInputPacket p;
    std::memcpy(&p, data, sizeof(p));
    if (p.magic != POOL_INPUT_MAGIC || !Bound(p.sender,p.receiver,remote,local) || !p.sequence || !p.inputEpoch ||
        !PoolInputTurn(s,current,p) || !(p.aimX >= -1 && p.aimX <= 1) || !(p.aimY >= -1 && p.aimY <= 1)) return false;
    if (input.received && (input.packet.match != p.match || std::int32_t(p.sequence-input.packet.sequence) <= 0)) return false;
    const bool newEpoch = p.inputEpoch != input.packet.inputEpoch;
    if (input.received && newEpoch && std::int32_t(p.inputEpoch-input.packet.inputEpoch) <= 0) return false;
    const bool newShot = p.shotSerial != input.packet.shotSerial;
    const bool reset = newShot || newEpoch;
    if (p.shootPresses-(reset ? 0 : input.packet.shootPresses) > 256 ||
        p.shootReleases-(reset ? 0 : input.packet.shootReleases) > 256) return false;
    if (reset) input.consumedPresses = input.consumedReleases = 0;
    if (newShot) input.epochResetPending = false;
    else if (newEpoch) input.epochResetPending = true;
    input.packet = p;
    input.tick = now;
    input.received = true;
    return true;
}
inline bool ConsumePoolInput(const State& s, const PoolTurn& current, PoolInputBuffer& input, U32 now, PoolControls& controls)
{
    controls = PoolControls{};
    if (input.epochResetPending)
    {
        // Cancel native POWER first, even if an outage was shorter than the
        // timeout. Preserve new-epoch edges for the next simulation frame.
        input.epochResetPending = false;
        return false;
    }
    const bool fresh = input.received && PoolInputTurn(s,current,input.packet) && now-input.tick <= 500;
    if (fresh)
    {
        controls.aimX = input.packet.aimX;
        controls.aimY = input.packet.aimY;
        controls.shootPressed = input.packet.shootPresses != input.consumedPresses;
        controls.shootReleased = input.packet.shootReleases != input.consumedReleases;
    }
    input.consumedPresses = input.packet.shootPresses;
    input.consumedReleases = input.packet.shootReleases;
    return fresh;
}

constexpr U32 MUD_INPUT_MAGIC = 0x38495942; // BYI8, negotiated by MudRealtime
struct MudInputPacket
{
    U32 magic = MUD_INPUT_MAGIC, sender = 0, receiver = 0;
    U32 match = 0, sequence = 0, jumpHeld = 0, jumpPresses = 0, kickPresses = 0, kickMode = 0;
};
struct MudControls
{
    bool jumpHeld = false, jumpPressed = false, kickPressed = false, directionalKick = false, kickLeft = false;
};
struct MudInputBuffer
{
    MudInputPacket packet;
    U32 consumedJump = 0, consumedKick = 0, tick = 0;
    bool received = false;
};
inline bool AcceptMudInput(const State& s, U32 localPlayer, const void* data, std::size_t size,
    U32 remote, U32 local, U32 now, MudInputBuffer& input)
{
    if (!data || size != sizeof(MudInputPacket) || localPlayer != 0 || s.game != Mud || s.phase != Playing) return false;
    MudInputPacket p;
    std::memcpy(&p, data, sizeof(p));
    if (p.magic != MUD_INPUT_MAGIC || !Bound(p.sender,p.receiver,remote,local) ||
        p.match != s.match || !p.sequence || p.jumpHeld > 1 || p.kickMode > 2) return false;
    if (input.received && (input.packet.match != p.match || std::int32_t(p.sequence - input.packet.sequence) <= 0)) return false;
    // Reject backwards or implausibly large event-counter jumps without changing
    // the accepted sequence. Normal packet loss recovers the most recent tap.
    if (p.jumpPresses - input.packet.jumpPresses > 256 || p.kickPresses - input.packet.kickPresses > 256) return false;
    input.packet = p;
    input.tick = now;
    input.received = true;
    return true;
}
inline bool ConsumeMudInput(const State& s, MudInputBuffer& input, U32 now, MudControls& controls)
{
    controls = MudControls{};
    const bool fresh = s.game == Mud && s.phase == Playing && input.received &&
        input.packet.match == s.match && now - input.tick <= 500;
    if (fresh)
    {
        controls.jumpHeld = input.packet.jumpHeld != 0;
        controls.jumpPressed = input.packet.jumpPresses != input.consumedJump;
        controls.kickPressed = input.packet.kickPresses != input.consumedKick;
        controls.directionalKick = input.packet.kickMode != 0;
        controls.kickLeft = input.packet.kickMode == 1;
    }
    // Expired, unconsumed edges cannot spring back to life on a later heartbeat.
    input.consumedJump = input.packet.jumpPresses;
    input.consumedKick = input.packet.kickPresses;
    return fresh;
}
inline bool Newer(const State& incoming, const State& current, U32 requestedGame = 0)
{
    if (!Valid(incoming) || incoming.match < current.match) return false;
    if (incoming.match == current.match) return incoming.revision > current.revision;
    if (incoming.phase == Offer) return true;
    if (current.phase != Idle) return false;
    // A connected guest may miss the entire Offer phase. Acknowledge a
    // cancelled match, or load the accepted game if this guest requested it.
    return incoming.phase == Leaving ||
        (incoming.phase == Loading && incoming.inviter == 1 &&
         incoming.game == requestedGame && incoming.accepted == 3);
}
}
