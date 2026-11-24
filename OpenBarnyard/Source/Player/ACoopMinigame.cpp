#include "pch.h"
#include "ACoopMinigame.h"
#include "ACoopMinigameProtocol.h"
#include "ACoopMinigameTiming.h"
#include "ACoopNet.h"
#include "ACoopPartner.h"
#include "ACoopProgress.h"
#include "APlayerManager.h"
#include "Helpers/AGameLoader.h"
#include "GameInterface/AGameStateController.h"
#include "GameInterface/ASimState.h"
#include "GameInterface/ABYardMenuState.h"
#include "GameInterface/AMessagePopupState.h"
#include "MiniGame/AMiniGameManager.h"
#include "MiniGame/AMiniGameState.h"
#include "MiniGame/ACoopMinigameHudStatus.h"
#include "MiniGame/ACoopMinigameHudText.h"
#include "MiniGame/ABarnExploreMinigameState.h"
#include "Input/AInputHandler.h"
#include "Locale/AModStrings.h"
#include "Locale/ALocaleManager.h"
#include "Tasks/ARootTask.h"
#include "HUD/AHudManager.h"
#include "GUI/AGUI2.h"
#include "GUI/AGUI2Style.h"
#include "GUI/AGUI2TextBox.h"
#include <GUI/T2GUIFontManager.h>
#include <Toshi/TStringManager.h>
#include <cstddef>
#include <array>
#include <cstring>
#include <cstdlib>
#include <PrimeDebug.h>
#ifdef PRIMEDEBUG_DEV
#include <Console/PConsole.h>
#endif
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING
namespace
{
using namespace CoopMini;
constexpr U32 MAGIC = 0x384d5942; // BYM8
constexpr U32 VERSION = 1;
constexpr U32 VIEW_MAGIC = 0x38565942; // BYV8
constexpr U32 FEATURES = Spectator | ExpandedGames | MudRealtime | DartsPvp | AllMinigames | PoolPvp |
    GolfGroundView | DartsViewAim | MudViewInput | PoolShotIntent | DartsTimedAim | FastViews | DartsBlendedAim;
enum Kind : U32 { Hello, Snapshot, Request };
struct Packet
{
    U32 magic = MAGIC, version = VERSION, kind = Hello;
    U32 sender = 0, receiver = 0;
    State state;
    Command command;
};
static_assert(sizeof(Packet) < ACoopNet::MAX_DATA_SIZE);
static_assert(sizeof(State) == 52 && sizeof(Command) == 24 && sizeof(Packet) == 96, "Keep the negotiated v1 packet layout");
static_assert(sizeof(MudInputPacket) == 36 && sizeof(MudInputPacket) < ACoopNet::MAX_DATA_SIZE);
static_assert(sizeof(DartsInputPacket) == 28 && sizeof(DartsInputPacket) < ACoopNet::MAX_DATA_SIZE);
static_assert(sizeof(DartsViewInputPacket) == 32 && sizeof(DartsViewInputPacket) < ACoopNet::MAX_DATA_SIZE);
static_assert(sizeof(DartsTimedInputPacket) == 36 && sizeof(DartsTimedInputPacket) < ACoopNet::MAX_DATA_SIZE);
static_assert(sizeof(DartsBlendedInputPacket) == 40 && sizeof(DartsBlendedInputPacket) < ACoopNet::MAX_DATA_SIZE);
static_assert(sizeof(PoolInputPacket) == 44 && sizeof(PoolInputPacket) < ACoopNet::MAX_DATA_SIZE);
static_assert(sizeof(PoolShotInputPacket) == 56 && sizeof(PoolShotInputPacket) < ACoopNet::MAX_DATA_SIZE);
static_assert(sizeof(PoolPlaceInputPacket) == 52 && sizeof(PoolPlaceInputPacket) < ACoopNet::MAX_DATA_SIZE);

struct ViewPacket
{
    U32 magic = VIEW_MAGIC, sender = 0, receiver = 0;
    ViewStamp stamp;
    std::uint8_t data[1000];
};
constexpr TUINT VIEW_HEADER_SIZE = offsetof(ViewPacket, data);
static_assert(sizeof(ViewPacket) < ACoopNet::MAX_DATA_SIZE);
struct BufferedView
{
    ViewPacket packet;
    TUINT size = 0;
    DWORD tick = 0;
};
BufferedView s_incomingView, s_outgoingView;
U32 s_peerFeatures, s_viewSequence, s_sentView;
TFLOAT s_viewWait;
MudInputBuffer s_incomingMudInput;
MudInputPacket s_outgoingMudInput, s_sentMudInput;
TFLOAT s_mudInputWait;
DartsTurn s_dartsTurn;
DartsInputBuffer s_incomingDartsInput;
DartsInputPacket s_outgoingDartsInput;
U32 s_outgoingDartsView, s_outgoingDartsTime, s_outgoingDartsPrevious, s_outgoingDartsBlend;
TFLOAT s_dartsInputWait;
PoolTurn s_poolTurn;
PoolInputBuffer s_incomingPoolInput;
PoolInputPacket s_outgoingPoolInput;
PoolShot s_outgoingPoolShot;
PoolPlacement s_outgoingPoolPlace;
TBOOL s_outgoingHasPlacement;
TFLOAT s_poolInputWait;
TBOOL s_poolInputSuspended;

void ResetPoolInput()
{
    s_poolTurn = PoolTurn{};
    s_incomingPoolInput = PoolInputBuffer{};
    s_outgoingPoolInput = PoolInputPacket{};
    s_outgoingPoolShot = {};
    s_outgoingHasPlacement = TFALSE;
    s_poolInputWait = 0.0f;
    s_poolInputSuspended = TFALSE;
}

void ResetDartsInput()
{
    s_dartsTurn = DartsTurn{};
    s_incomingDartsInput = DartsInputBuffer{};
    s_outgoingDartsInput = DartsInputPacket{};
    s_outgoingDartsView = 0;
    s_outgoingDartsTime = 0;
    s_outgoingDartsPrevious = s_outgoingDartsBlend = 0;
    s_dartsInputWait = 0.0f;
}

void ResetMudInput()
{
    s_incomingMudInput = MudInputBuffer{};
    s_outgoingMudInput = s_sentMudInput = MudInputPacket{};
    s_mudInputWait = 0.0f;
}

State s_state;
Command s_pending;
AMiniGameState* s_game = TNULL;
U32 s_localNonce, s_peerNonce, s_sequence, s_lastRemoteCommand;
U32 s_boundPlayer = ~0u;
bool s_retiringPeer = false;
U32 s_lastRequest, s_requestSequence, s_requestedGame;
NativeMatch s_nativeMatch;
DWORD s_lastPeer, s_phaseSince, s_idleSince;
TFLOAT s_sendWait, s_devWait, s_devResultWait, s_devRoundWait;
TINT s_score;
TBOOL s_wasConnected, s_devInvited;
const TCHAR* s_names[] = { "", "AChickenFireMinigameState", "AWhackARaccMiniGameState", "AGolfMiniGameState",
    "AGateCrashMiniGameState", "ADartMiniGame", "AStatuesMiniGameState", "ATeaseMrsBeadyGame",
    "AChasingChicksMiniGame", "AVeggiePatchDefenderGame", "ABikeRaceMicroGame", "ACarChaseMiniGameState",
    "ACowTippingMiniGameState", "ATapperMiniGameState", "ABarnYardPoolMiniGameState" };
static_assert(TARRAYSIZE(s_names) == LastGame + 1);
const TWCHAR* GameTitle(U32 game)
{
    static constexpr MODSTRING titles[] = {
        MODSTRING_COOPMINI_CHICKEN, MODSTRING_COOPMINI_WHACK, MODSTRING_COOPMINI_GOLF,
        MODSTRING_COOPMINI_MUD, MODSTRING_COOPMINI_DARTS
    };
    static_assert(TARRAYSIZE(titles) == Darts - Chicken + 1);
    if (!ValidGame(game)) return L"";
    if (game <= Darts) return AModStrings::Get(titles[game - Chicken]);
    auto* manager = AMiniGameManager::GetSingleton();
    const TINT index = manager->FindMiniGame(TPString8(s_names[game]));
    return index >= 0 ? ALocaleManager::Interface()->GetString(manager->GetMiniGame(index)->GetLocaleTitle()) : L"";
}
TWCHAR s_hudText[512], s_popupText[512];
U32 s_hudTextVersion = 0;
U32 s_playerNameVersion = 0;

template <size_t N>
void NameScorecard(TWCHAR (&text)[N], const TWCHAR* active = nullptr)
{
    TWCHAR hostLabel[32], guestLabel[32];
    const auto* playerFormat = AModStrings::Get(MODSTRING_MINIGAME_PLAYER);
    AModStrings::Format(hostLabel, TARRAYSIZE(hostLabel), playerFormat, 1);
    AModStrings::Format(guestLabel, TARRAYSIZE(guestLabel), playerFormat, 2);
    auto result = CoopMiniHud::Scorecard(text, {hostLabel, guestLabel},
        {ACoopPartner::GetPlayerName(0), ACoopPartner::GetPlayerName(1)}, GameTitle(s_state.game));
    if (active) result.append(L" (").append(active).append(L")");
    TStringManager::String16CopySafe(text, result.c_str(), N);
}
void FormatRaceTime(TWCHAR* text, TUINT capacity, TINT milliseconds, TBOOL finished = TTRUE)
{
    if (!finished)
        TStringManager::String16CopySafe(text, L"--:--.---", capacity);
    else if (milliseconds >= 10000000)
        TStringManager::String16CopySafe(text, AModStrings::Get(MODSTRING_COOPMINI_DNF), capacity);
    else
        TStringManager::String16Format(text, capacity, L"%02d:%02d.%03d", milliseconds / 60000,
            milliseconds / 1000 % 60, milliseconds % 1000);
}

U32 LocalPlayer() { return ACoopNet::GetMode() == ACoopNet::MODE_HOST ? 0u : 1u; }
TBOOL IsHost() { return ACoopNet::GetMode() == ACoopNet::MODE_HOST; }
TBOOL SafeFarm() { return ACoopProgress::IsSafeMoment(); }
TBOOL StoryWorldRestored()
{
    if (!AGameStateController::IsSingletonCreated() || AGameLoader::IsInMiniGame() ||
        ARootTask::GetSingleton()->IsTransitioning()) return TFALSE;
    auto* controller = AGameStateController::GetSingleton();
    if (controller->GetNumStates() == 0) return TFALSE;
    const auto* state = controller->GetCurrentState()->GetClass();
    return (AGameLoader::IsMainGameLoaded() && state == &TGetClass(ASimState)) ||
        (AGameLoader::IsBarnExploreMiniGame() && state == &TGetClass(ABarnExploreMinigameState));
}

#ifndef TOSHI_FINAL
TBOOL Switch(const TCHAR* name, const TCHAR** args = TNULL)
{
    const TCHAR* cmd = AGameLoader::Dev_GetCommandLine();
    const size_t n = std::strlen(name);
    for (const TCHAR* at = cmd; (at = std::strstr(at, name)) != TNULL; ++at)
        if ((at == cmd || at[-1] == ' ' || at[-1] == '"') && (!at[n] || at[n] == ' ' || at[n] == '"'))
        {
            if (args) *args = at + n;
            return TTRUE;
        }
    return TFALSE;
}
#else
TBOOL Switch(const TCHAR*, const TCHAR** = TNULL) { return TFALSE; }
#endif

TCHAR s_shotFile[320];
DWORD s_shotAt;
void QueueShot(const TCHAR* event)
{
#ifndef TOSHI_FINAL
    const TCHAR* arg;
    if (!Switch("-coopminishots", &arg)) return;
    while (*arg == ' ') ++arg;
    TCHAR prefix[240];
    TINT n = 0;
    while (*arg && *arg != ' ' && *arg != '"' && n < TARRAYSIZE(prefix) - 1) prefix[n++] = *arg++;
    prefix[n] = 0;
    if (!n) return;
    TStringManager::String8Format(s_shotFile, TARRAYSIZE(s_shotFile), "%s_%s.png", prefix, event);
    s_shotAt = GetTickCount() + 1500;
#endif
}
void UpdateShot()
{
#ifdef PRIMEDEBUG_DEV
    if (s_shotAt && GetTickCount() >= s_shotAt)
    {
        TCHAR command[360];
        TStringManager::String8Format(command, TARRAYSIZE(command), "screenshot %s", s_shotFile);
        PConsole::Execute(command);
        s_shotAt = 0;
    }
#endif
}

void LogState(const TCHAR* why)
{
    TINFO("[coopmini] %s match=%u revision=%u phase=%u game=%u turn=%u player=%u scores=%d,%d finished=%u ready=%u farm=%u\n",
        why, s_state.match, s_state.revision, s_state.phase, s_state.game, s_state.turn,
        s_state.player, s_state.scores[0], s_state.scores[1], s_state.finished, s_state.ready, s_state.farm);
}
void SendCurrentState();
void Changed(U32 oldPhase)
{
    if (oldPhase != s_state.phase) s_phaseSince = GetTickCount();
    s_sendWait = 0.0f;
    if (IsHost()) SendCurrentState();
    LogState("state");
    if (s_state.phase == Playing)
    {
        TCHAR event[32];
        TStringManager::String8Format(event, TARRAYSIZE(event), "turn%u", s_state.turn);
        QueueShot(event);
    }
}
void LocalCommand(U32 action, TINT score = 0, TBOOL final = TFALSE)
{
    if (!IsHost() && s_pending.action == action && s_pending.match == s_state.match &&
        s_pending.turn == s_state.turn && s_pending.score == score && s_pending.final == U32(final != TFALSE))
        return; // The periodic sender already retries this exact command.
    Command c;
    c.action = action;
    c.match = s_state.match;
    c.turn = s_state.turn;
    c.sequence = ++s_sequence;
    c.score = score;
    c.final = final ? 1u : 0u;
    if (IsHost())
    {
        const U32 old = s_state.phase;
        if (Apply(s_state, 0, c)) Changed(old);
    }
    else
    {
        s_pending = c; // repeated until a newer authoritative snapshot acknowledges it
        s_sendWait = 0.0f;
        SendCurrentState();
    }
}
void Cancel(const TCHAR* why)
{
    if (s_state.phase == Idle || s_state.phase == Leaving) return;
    TINFO("[coopmini] cancelling: %s\n", why);
    LocalCommand(Leave);
    // A disconnected joiner cannot wait for a host acknowledgement.
    if (!ACoopNet::IsConnected() || GetTickCount() - s_lastPeer > 8000)
    {
        s_state.phase = Leaving;
        s_phaseSince = GetTickCount();
    }
}

class MiniPopup : public AMessagePopupState
{
public:
    MiniPopup(TBOOL results);
    virtual void OnRemoval() override;
    virtual TBOOL ProcessInput(const TInputInterface::InputEvent* event) override;
    virtual TBOOL ProcessCommand(AInputCommand command, const TInputInterface::InputEvent* event, TBOOL& stop) override;
    virtual TBOOL OnUpdate(TFLOAT dt) override;
    static TBOOL __stdcall Response(MiniPopup* self, AMessagePopupState*, ResponseEvent* event);
    TBOOL m_results;
    TListener<AMessagePopupState, ResponseEvent, MiniPopup> m_listener;
};
MiniPopup* s_popup;
MiniPopup::MiniPopup(TBOOL results)
    : AMessagePopupState(AModStrings::Get(results ? MODSTRING_COOPMINI_RESULT_TITLE : MODSTRING_COOPMINI_INVITE_TITLE), s_popupText, TPString8(), TFALSE,
        results ? AInputCommand_OK : AInputCommand_Yes,
        results ? AInputCommand_Empty : AInputCommand_No,
        AInputCommand_Empty, AInputCommand_Empty, TFALSE, 0.0f, 0), m_results(results)
{
    m_listener.Connect(GetResponseEmitter(), this, Response, 0);
    s_popup = this;
}
void MiniPopup::OnRemoval()
{
    if (s_popup == this) s_popup = TNULL;
    m_listener.Disconnect();
    AMessagePopupState::OnRemoval();
}
TBOOL MiniPopup::Response(MiniPopup* self, AMessagePopupState*, ResponseEvent* event)
{
    if (self->m_results) ACoopMinigame::RequestLeave();
    else LocalCommand(event->eButton == AInputCommand_Yes ? Accept : Reject);
    self->Remove();
    return TTRUE;
}
TBOOL MiniPopup::ProcessCommand(AInputCommand command, const TInputInterface::InputEvent* event, TBOOL& stop)
{
    if (m_results && Switch("-coopminiautoleave")) return TTRUE;
    return AMessagePopupState::ProcessCommand(command, event, stop);
}
TBOOL MiniPopup::ProcessInput(const TInputInterface::InputEvent* event)
{
    // The unattended probe leaves both result panels visible for eight seconds,
    // while its native scripted gameplay keys continue to arrive.
    if (m_results && Switch("-coopminiautoleave")) return TTRUE;
    return AMessagePopupState::ProcessInput(event);
}
TBOOL MiniPopup::OnUpdate(TFLOAT dt)
{
    if ((!m_results && s_state.phase != Offer) || (m_results && s_state.phase != Results))
    {
        Remove();
        return TTRUE;
    }
    return AMessagePopupState::OnUpdate(dt);
}

class MiniMenu : public ABYardMenuState
{
public:
    TDECLARE_CLASS(MiniMenu, ABYardMenuState);
    MiniMenu(TINT page = 0) : m_choice(0), m_optionCount(0), m_page(page) {}
    virtual void OnInsertion() override
    {
        T2GUIFont* font = T2GUIFontManager::FindFont(AGUI2STYLE_FONT_PRIMARY);
        // Five games plus a page button fit the native dialog at its original
        // font size. The catalogue is filtered before paging for older peers.
        m_optionCount = 0;
        TINT games[LastGame], count = 0;
        for (TINT game = Chicken; game <= LastGame; ++game)
            if (PeerCanPlay(game, s_peerFeatures)) games[count++] = game;
        const TINT pages = (count + 4) / 5;
        m_page = pages ? m_page % pages : 0;
        for (TINT i = m_page * 5; i < count && i < m_page * 5 + 5; ++i)
        {
            AddButton(font, games[i], GameTitle(games[i]));
        }
        if (pages > 1) AddButton(font, -1, AModStrings::Get(MODSTRING_COOPMINI_NEXT_PAGE));
        if (m_optionCount) m_oMenu.SetFocusAt(m_buttons[0]);
        m_oDialogTitle.SetText(AModStrings::Get(MODSTRING_COOPMINI_MENU));
        ABYardMenuState::OnInsertion();
    }
    virtual void OnRemoval() override
    {
        for (TINT i = 0; i < m_optionCount; ++i) m_buttons[i].RemoveSelf();
        ABYardMenuState::OnRemoval();
    }
    virtual void OnMenuItemActivated(AGUI2MenuItem* item) override
    {
        auto* b = TSTATICCAST(AGUI2Button, item);
        m_choice = b->GetId();
        OnButtonActivated(b);
    }
    virtual void OnMenuClose() override
    {
        const TINT choice = m_choice;
        const TINT nextPage = m_page + 1;
        Remove();
        if (choice < 0) AGameStateController::GetSingleton()->PushState(new MiniMenu(nextPage));
        else if (choice) ACoopMinigame::RequestInvite(choice);
    }
private:
    void AddButton(T2GUIFont* font, TINT id, const TWCHAR* title)
    {
        auto& b = m_buttons[m_optionCount++];
        b.Create(font, font->GetTextWidth(title), title, 8.0f);
        b.SetFocusedColour(AGUI2STYLE_COLOR_FOCUSED);
        b.SetEnabledColour(AGUI2STYLE_COLOR_SECONDARY);
        b.SetDisabledColour(AGUI2STYLE_COLOR_DISABLED);
        b.SetId(id);
        b.OnFocusLost();
        m_oMenu.AddMenuItem(b);
    }
    AGUI2Button m_buttons[6];
    TINT m_choice;
    TINT m_optionCount;
    TINT m_page;
};
TDEFINE_CLASS(MiniMenu);

class MiniHud : public AHudElement
{
public:
    TDECLARE_CLASS(MiniHud, AHudElement);
    MiniHud() : AHudElement(MASK_PLAYER_MAIN | MASK_MINIGAME_DEFAULT), m_font(TNULL) {}
    ~MiniHud();
    virtual TBOOL OnAttach() override
    {
        m_font = T2GUIFontManager::FindFont(AGUI2STYLE_FONT_SECONDARY);
        m_text.Create(m_font, 0.0f);
        m_textVersion = ~0u;
        m_text.SetAttachment(T2GUIATTACHMENT_TOPCENTER, T2GUIATTACHMENT_TOPCENTER);
        m_text.SetTextAlign(T2GUIFont::TextAlign_Center);
        m_text.SetScale(0.8f);
        m_text.SetColour(0xffffdf80);
        m_text.SetShadowEnabled(TTRUE);
        m_text.SetShadowColour(0xd0000000);
        m_text.SetShadowOffset(3.0f, 3.0f);
        m_text.SetSize(AGUI2::GetRootElement()->GetWidth() * 0.9f, 0.0f, TFALSE);
        m_text.SetTransform(0.0f, 95.0f);
        AGUI2::GetRootElement()->AddChildTail(m_text);
        m_text.Hide();
        return TTRUE;
    }
    virtual void OnDetach() override
    {
        m_text.RemoveSelf();
        T2GUIFontManager::ReleaseFont(m_font);
        m_font = TNULL;
    }
    virtual void Show() override { m_bCurrentlyVisible = TTRUE; }
    virtual void Hide() override { m_bCurrentlyVisible = TFALSE; m_text.Hide(); }
    virtual void Update(TFLOAT) override
    {
        if (!ACoopMinigame::ShowsHudStatus()) { m_text.Hide(); return; }
        if (m_textVersion != s_hudTextVersion)
        {
            m_text.SetText(s_hudText);
            m_textVersion = s_hudTextVersion;
        }
        m_text.Show();
    }
    AGUI2TextBox m_text;
    T2GUIFont* m_font;
    U32 m_textVersion = ~0u;
};
TDEFINE_CLASS(MiniHud);
MiniHud* s_hud;
MiniHud::~MiniHud() { if (s_hud == this) s_hud = TNULL; }

void ReceiveView(const void* data, TUINT size)
{
    if (!(s_peerFeatures & Spectator) || !s_game || size <= VIEW_HEADER_SIZE || size > sizeof(ViewPacket)) return;
    ViewPacket header;
    std::memcpy(&header, data, VIEW_HEADER_SIZE);
    if (!Bound(header.sender, header.receiver, s_peerNonce, s_localNonce) ||
        !CanWatchView(s_state, LocalPlayer(), header.stamp) ||
        (s_incomingView.size && !NewerView(header.stamp, s_incomingView.packet.stamp))) return;
    const TBOOL first = !s_incomingView.size || s_incomingView.packet.stamp.match != header.stamp.match ||
        s_incomingView.packet.stamp.turn != header.stamp.turn;
    std::memcpy(&s_incomingView.packet, data, size);
    s_incomingView.size = size - VIEW_HEADER_SIZE;
    s_incomingView.tick = GetTickCount();
    if (first) TINFO("[coopmini] spectator receiving game=%u match=%u turn=%u\n", header.stamp.game, header.stamp.match, header.stamp.turn);
}

void Receive(const void* data, TINT size)
{
    U32 magic = 0;
    if (size < TINT(sizeof(magic))) return;
    std::memcpy(&magic, data, sizeof(magic));
    if (magic != MAGIC && (s_retiringPeer || s_boundPlayer != LocalPlayer())) return;
    if (magic == VIEW_MAGIC) { ReceiveView(data, TUINT(size)); return; }
    if (magic == POOL_INPUT_MAGIC)
    {
        if (!ACoopMinigame::IsRealtimePoolHost() || !s_game) return;
        AcceptPoolInput(s_state, LocalPlayer(), s_poolTurn, data, TUINT(size),
            s_peerNonce, s_localNonce, GetTickCount(), s_incomingPoolInput, ACoopMinigame::SupportsPoolShotIntent());
        return;
    }
    if (magic == DARTS_INPUT_MAGIC)
    {
        if (!ACoopMinigame::IsRealtimeDartsHost() || !s_game) return;
        if (AcceptDartsInput(s_state, LocalPlayer(), s_dartsTurn, data, TUINT(size),
            s_peerNonce, s_localNonce, GetTickCount(), s_incomingDartsInput,
            ACoopMinigame::SupportsDartsViewAim(), ACoopMinigame::SupportsDartsTimedAim(), ACoopMinigame::SupportsDartsBlendedAim()))
            TINFO("[coopmini] darts input rx match=%u serial=%u stage=%u sequence=%u\n", s_state.match,
                s_incomingDartsInput.packet.throwSerial, s_incomingDartsInput.packet.stage, s_incomingDartsInput.packet.sequence);
        return;
    }
    if (magic == MUD_INPUT_MAGIC)
    {
        if (!ACoopMinigame::IsRealtimeMudHost() || !s_game) return;
        const auto previous = s_incomingMudInput.packet;
        if (AcceptMudInput(s_state, LocalPlayer(), data, TUINT(size), s_peerNonce, s_localNonce, GetTickCount(), s_incomingMudInput))
        {
            const auto& input = s_incomingMudInput.packet;
            if (!previous.sequence || previous.jumpPresses != input.jumpPresses || previous.kickPresses != input.kickPresses || previous.jumpHeld != input.jumpHeld)
                TINFO("[coopmini] realtime mud input rx match=%u sequence=%u held=%u jump=%u kick=%u mode=%u\n",
                    input.match, input.sequence, input.jumpHeld, input.jumpPresses, input.kickPresses, input.kickMode);
        }
        return;
    }
    if (size != sizeof(Packet)) return;
    Packet p;
    std::memcpy(&p, data, sizeof(p));
    if (p.magic != MAGIC || p.version != VERSION || !p.sender || p.kind > Request) return;
    if (p.kind == Hello)
    {
        if (p.receiver && p.receiver != s_localNonce) return;
        const DWORD now = GetTickCount();
        const bool roleChanged = s_peerNonce && s_boundPlayer != LocalPlayer();
        const bool different = s_peerNonce && (roleChanged || s_peerNonce != p.sender);
        if (different && (roleChanged || now - s_lastPeer > 8000))
        {
            if (s_state.phase != Idle)
            {
                if (roleChanged && !s_retiringPeer)
                {
                    s_retiringPeer = true;
                    s_pending = {};
                    s_state.phase = Leaving;
                    s_state.farm = 0;
                    s_idleSince = 0;
                    s_phaseSince = now;
                }
                return;
            }
            if (!s_nativeMatch.ReplacePeer(s_state,IsHost(),s_game || ARootTask::GetSingleton()->IsTransitioning()))
            {
                s_retiringPeer = true;
                s_pending = {};
                s_idleSince = 0;
                s_phaseSince = now;
                return;
            }
            s_retiringPeer = false;
            s_pending = {};
            s_requestedGame = s_requestSequence = s_sequence = 0;
            s_viewSequence = s_sentView = 0;
            s_score = 0;
            s_viewWait = s_sendWait = s_devResultWait = s_devRoundWait = 0;
            s_idleSince = s_shotAt = 0;
        }
        else if (different || (s_retiringPeer && s_state.phase != Idle)) return;
        else s_retiringPeer = false; // Original partner reappeared after the return completed.
        if (!s_peerNonce || different)
        {
            s_peerNonce = p.sender;
            s_boundPlayer = LocalPlayer();
            s_lastRemoteCommand = 0;
            s_lastRequest = 0;
            s_incomingView.size = s_outgoingView.size = 0;
            ResetMudInput();
            ResetDartsInput();
            ResetPoolInput();
            TINFO("[coopmini] partner bound nonce=%08x\n", s_peerNonce);
        }
        if (s_peerNonce == p.sender)
        {
            s_lastPeer = GetTickCount();
            // This field is unused in the original Hello. An old peer stays
            // compatible with the first three games and advertises zero bits.
            s_peerFeatures = p.command.sequence & FEATURES;
        }
        return;
    }
    if (s_retiringPeer || s_boundPlayer != LocalPlayer() || !Bound(p.sender, p.receiver, s_peerNonce, s_localNonce)) return;
    s_lastPeer = GetTickCount();
    if (IsHost() && p.kind == Request)
    {
        if (p.command.action == Invite)
        {
            if (p.command.sequence <= s_lastRequest || !PeerCanPlay(p.state.game, s_peerFeatures)) return;
            if (s_state.phase == Idle && SafeFarm())
            {
                s_lastRequest = p.command.sequence;
                if (Begin(s_state, p.state.game, 1)) Changed(Idle);
            }
            return;
        }
        if (p.command.sequence <= s_lastRemoteCommand || p.command.match != s_state.match) return;
        // A stale score must never overwrite a round result or a newer live score.
        const U32 old = s_state.phase;
        if (ApplyOrdered(s_state, 1, p.command, s_lastRemoteCommand))
        {
            if (p.command.action != Score) Changed(old);
        }
    }
    else if (!IsHost() && p.kind == Snapshot && PeerCanPlay(p.state.game, s_peerFeatures) && Newer(p.state, s_state, s_requestedGame))
    {
        const U32 old = s_state.phase;
        const U32 oldTurn = s_state.turn;
        if (p.state.phase == Idle && s_nativeMatch.loaded && s_nativeMatch.loaded == p.state.match &&
            (s_game || ARootTask::GetSingleton()->IsTransitioning()))
        {
            if (old != Leaving) { s_state.phase = Leaving; s_phaseSince = GetTickCount(); }
            return; // The host is home already. Finish the local return before accepting Idle.
        }
        if (p.state.match != s_state.match || p.state.phase == Offer) s_requestedGame = 0;
        s_state = p.state;
        // Never replace a critical command with a live score before acknowledgement.
        if ((s_pending.action == Accept && s_state.phase != Offer) ||
            (s_pending.action == Ready && (s_state.ready & 2)) ||
            (s_pending.action == RoundDone && (s_state.turn != s_pending.turn || (DuelGame(s_state.game) && (s_state.finished & 2)))) ||
            ((s_pending.action == Leave || s_pending.action == Reject) && s_state.phase == Leaving) ||
            (s_pending.action == Farm && (s_state.farm & 2))) s_pending = Command{};
        if (old != s_state.phase || oldTurn != s_state.turn) Changed(old);
    }
}

void SendCurrentState()
{
    if (s_retiringPeer || s_boundPlayer != LocalPlayer() || !s_peerNonce || !s_localNonce || !ACoopNet::IsConnected()) return;
    Packet p;
    p.sender = s_localNonce;
    p.receiver = s_peerNonce;
    p.state = s_state;
    p.kind = IsHost() ? Snapshot : Request;
    if (!IsHost())
    {
        if (s_requestedGame && s_state.phase == Idle)
        {
            p.command.action = Invite;
            p.command.sequence = s_requestSequence;
            p.state.game = s_requestedGame;
        }
        else p.command = s_pending;
    }
    ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &p, sizeof(p));
}
void Send()
{
    Packet hello;
    hello.sender = s_localNonce;
    hello.receiver = s_peerNonce;
    hello.command.sequence = FEATURES;
    ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &hello, sizeof(hello));
    SendCurrentState();
}

void SendView(DWORD now)
{
    // Send each completed pose once. A later network tick retries a socket
    // send that could not be queued.
    if (!s_outgoingView.size || !(s_peerFeatures & Spectator) ||
        !ACoopNet::IsConnected() || !s_game ||
        (ACoopMinigame::BlockInput() && !(IsHost() && HostGame(s_state.game)))) return;
    auto& out = s_outgoingView.packet;
    if (out.stamp.sequence == s_sentView || now - s_outgoingView.tick > 500 ||
        !CanWatchView(s_state, 1u - LocalPlayer(), out.stamp)) return;
    out.sender = s_localNonce;
    out.receiver = s_peerNonce;
    if (ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &out, TINT(VIEW_HEADER_SIZE + s_outgoingView.size)))
        s_sentView = out.stamp.sequence;
}

void SendMudInput(TFLOAT dt)
{
    if (IsHost() || !ACoopMinigame::IsRealtimeMud() ||
        s_state.phase != Playing || !s_game || !ACoopNet::IsConnected()) return;
    if (!ViewSendDue(s_mudInputWait, dt)) return;
    auto& input = s_outgoingMudInput;
    input.sender = s_localNonce;
    input.receiver = s_peerNonce;
    input.match = s_state.match;
    if (++input.sequence == 0) ++input.sequence;
    if (!s_sentMudInput.sequence || s_sentMudInput.jumpPresses != input.jumpPresses ||
        s_sentMudInput.kickPresses != input.kickPresses || s_sentMudInput.jumpHeld != input.jumpHeld)
        TINFO("[coopmini] realtime mud input tx match=%u sequence=%u held=%u jump=%u kick=%u mode=%u\n",
            input.match, input.sequence, input.jumpHeld, input.jumpPresses, input.kickPresses, input.kickMode);
    s_sentMudInput = input;
    ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &input, sizeof(input));
}

void SendDartsInput(TFLOAT dt)
{
    if (IsHost() || !ACoopMinigame::IsRealtimeDarts() ||
        ACoopMinigame::BlockInput() || !s_game || !ACoopNet::IsConnected() || !s_outgoingDartsInput.sequence) return;
    if (!ViewSendDue(s_dartsInputWait, dt)) return;
    auto& input = s_outgoingDartsInput;
    input.sender = s_localNonce;
    input.receiver = s_peerNonce;
    input.match = s_state.match;
    // Repeat this edge until native aim state advances; only a human click
    // increments sequence. Duplicated heartbeats cannot become new clicks.
    if (s_outgoingDartsPrevious && ACoopMinigame::SupportsDartsBlendedAim())
    {
        const DartsBlendedInputPacket click{input, s_outgoingDartsView, s_outgoingDartsPrevious, s_outgoingDartsBlend};
        ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &click, sizeof(click));
    }
    else if (s_outgoingDartsTime && s_outgoingDartsView && ACoopMinigame::SupportsDartsTimedAim())
    {
        const DartsTimedInputPacket timed{input, s_outgoingDartsView, s_outgoingDartsTime};
        ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &timed, sizeof(timed));
    }
    else if (s_outgoingDartsView && ACoopMinigame::SupportsDartsViewAim())
    {
        const DartsViewInputPacket view{input, s_outgoingDartsView};
        ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &view, sizeof(view));
    }
    else ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &input, sizeof(input));
}

void SendPoolInput(TFLOAT dt)
{
    if (s_poolInputSuspended || IsHost() || !ACoopMinigame::IsRealtimePool() ||
        ACoopMinigame::BlockInput() || !s_game || !ACoopNet::IsConnected() || !s_outgoingPoolInput.shotSerial) return;
    if (!ViewSendDue(s_poolInputWait, dt)) return;
    auto& input = s_outgoingPoolInput;
    input.sender = s_localNonce;
    input.receiver = s_peerNonce;
    input.match = s_state.match;
    if (++input.sequence == 0) ++input.sequence;
    if (ACoopMinigame::SupportsPoolShotIntent() && ValidPoolShot(s_outgoingPoolShot))
    {
        const PoolShotInputPacket release{input, s_outgoingPoolShot};
        ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &release, sizeof(release));
    }
    else if (ACoopMinigame::SupportsPoolShotIntent() && s_outgoingHasPlacement)
    {
        const PoolPlaceInputPacket release{input, s_outgoingPoolPlace};
        ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &release, sizeof(release));
    }
    else ACoopNet::SendData(ACoopNet::CHANNEL_MINIGAME, &input, sizeof(input));
}

void RefreshHudText(DWORD now)
{
    // SetText invalidates line layout. Only reformat when something shown has
    // changed, including a language switch or the other player's view going stale.
    static std::array<U32, 8> previous{};
    static std::wstring previousNames[2];
    static DWORD lastNameCheck = 0;
    if (s_state.phase == Idle || s_state.phase == Results)
    {
        previous = {};
        s_hudText[0] = 0;
        return;
    }
    const U32 local = LocalPlayer();
    const TINT scores[] = {DisplayedScore(s_state, s_pending, local, 0), DisplayedScore(s_state, s_pending, local, 1)};
    const U32 finished = s_state.finished | (HasLocalScore(s_state, s_pending, local) &&
        s_pending.action == RoundDone && s_pending.final ? 1u << local : 0u);
    const TBOOL blocked = s_state.phase == Playing && ACoopMinigame::BlockInput();
    const TBOOL watching = s_state.phase == Playing && s_state.player != local &&
        s_incomingView.size && CanWatchView(s_state, local, s_incomingView.packet.stamp) &&
        now - s_incomingView.tick <= 1000;
    auto* locale = ALocaleManager::Interface();
#ifdef BARNYARD_COMMUNITY_PATCH
    const U32 language = locale ? U32(locale->GetTextLanguage()) : ~0u;
#else
    const U32 language = locale ? U32(locale->GetLanguage()) : ~0u;
#endif
    const std::array<U32, 8> current = { s_state.match, s_state.revision, s_state.phase,
        local, language, U32(blocked != 0) | (U32(watching != 0) << 1), U32(scores[local]), finished };
    if (previous == current && now - lastNameCheck < 500) return;
    lastNameCheck = now;
    const auto* host = ACoopPartner::GetPlayerName(0);
    const auto* guest = ACoopPartner::GetPlayerName(1);
    const bool changedNames = previousNames[0] != host || previousNames[1] != guest;
    if (previous == current && !changedNames) return;
    if (changedNames) ++s_playerNameVersion;
    previous = current;
    previousNames[0] = host;
    previousNames[1] = guest;
    ++s_hudTextVersion;
    s_hudText[0] = 0;
    if (s_state.phase == Offer) AModStrings::Format(s_hudText, TARRAYSIZE(s_hudText), AModStrings::Get(MODSTRING_COOPMINI_WAIT_REPLY), GameTitle(s_state.game));
    if (s_state.phase == Loading) AModStrings::Format(s_hudText, TARRAYSIZE(s_hudText), AModStrings::Get(MODSTRING_COOPMINI_WAIT_PLAYERS), GameTitle(s_state.game));
    if (s_state.phase == Playing && s_state.game == Mud) AModStrings::Format(s_hudText, TARRAYSIZE(s_hudText),
        AModStrings::Get(MODSTRING_COOPMINI_MUD_PLAYING), CoopMiniHud::Title, s_state.scores[0], s_state.scores[1], local + 1);
    else if (s_state.phase == Playing && LowerScoreWins(s_state.game))
    {
        TWCHAR hostTime[64], guestTime[64];
        FormatRaceTime(hostTime, TARRAYSIZE(hostTime), scores[0], (finished & 1) != 0);
        FormatRaceTime(guestTime, TARRAYSIZE(guestTime), scores[1], (finished & 2) != 0);
        AModStrings::Format(s_hudText, TARRAYSIZE(s_hudText), AModStrings::Get(MODSTRING_COOPMINI_TIME_PLAYING),
            CoopMiniHud::Title, hostTime, guestTime,
            AModStrings::Get(blocked ? MODSTRING_COOPMINI_DUEL_WAITING : MODSTRING_COOPMINI_DUEL_RUNNING));
    }
    else if (s_state.phase == Playing && DuelGame(s_state.game)) AModStrings::Format(s_hudText, TARRAYSIZE(s_hudText),
        AModStrings::Get(MODSTRING_COOPMINI_DUEL_PLAYING), CoopMiniHud::Title, scores[0], scores[1],
        AModStrings::Get(blocked ? MODSTRING_COOPMINI_DUEL_WAITING : MODSTRING_COOPMINI_DUEL_RUNNING));
    else if (s_state.phase == Playing) AModStrings::Format(s_hudText, TARRAYSIZE(s_hudText),
        AModStrings::Get(s_state.game == Darts ? MODSTRING_COOPMINI_DARTS_PLAYING :
            s_state.game == Pool ? MODSTRING_COOPMINI_POOL_PLAYING : MODSTRING_COOPMINI_PLAYING),
        CoopMiniHud::Title, scores[0], scores[1], s_state.turn + 1,
        AModStrings::Get(s_state.player == LocalPlayer() ? MODSTRING_COOPMINI_YOUR_TURN :
            (watching ? MODSTRING_COOPMINI_WATCHING : MODSTRING_COOPMINI_PARTNER_TURN)));
    if (s_state.phase == Leaving) TStringManager::String16CopySafe(s_hudText, AModStrings::Get(MODSTRING_COOPMINI_LEAVING), TARRAYSIZE(s_hudText));
    if (s_state.phase == Playing)
    {
        const auto* active = s_state.game == Mud || DuelGame(s_state.game) ? nullptr :
            (s_state.player == 0 ? host : guest);
        NameScorecard(s_hudText, active);
    }
}

void Update(TFLOAT dt)
{
    if (!s_localNonce)
    {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        s_localNonce = U32(counter.QuadPart) ^ GetCurrentProcessId() ^ GetTickCount();
        if (!s_localNonce) s_localNonce = 1;
    }
    if (!s_hud && AHudManager::IsSingletonCreated() && AGUI2::IsSingletonCreated())
    {
        s_hud = new MiniHud();
        if (!AHudManager::GetSingleton()->AddElement(s_hud, TTRUE, TTRUE, TFALSE)) { delete s_hud; s_hud = TNULL; }
    }
    const DWORD now = GetTickCount();
    const TBOOL connected = ACoopNet::IsConnected();
    if (connected) s_wasConnected = TTRUE;
    const DWORD grace = s_state.phase == Loading ? 90000 : 8000;
    if (s_state.phase != Idle && s_state.phase != Leaving &&
        ((s_wasConnected && now - s_lastPeer > grace) || ACoopNet::GetMode() == ACoopNet::MODE_OFF)) Cancel("partner disconnected");
    if ((s_state.phase == Offer || s_state.phase == Loading) && now - s_phaseSince > 120000) Cancel("invitation/loading timeout");

    // Development only: exercise the very same invitation path as the pause menu.
    if (!s_devInvited && Switch("-coopmini") && IsHost() && ACoopMinigame::CanInvite() && SafeFarm())
    {
        s_devWait += dt;
        if (s_devWait > 2.0f)
        {
            s_devInvited = TTRUE;
            const TCHAR* gameArg = TNULL;
            const TINT game = Switch("-coopminigame", &gameArg) ? std::atoi(gameArg) : Chicken;
            ACoopMinigame::RequestInvite(game);
        }
    }
    if (s_requestedGame && IsHost() && s_state.phase == Idle && SafeFarm())
    {
        if (PeerCanPlay(s_requestedGame, s_peerFeatures) && Begin(s_state, s_requestedGame, 0)) Changed(Idle);
        s_requestedGame = 0;
    }
    if (s_state.phase == Offer && s_state.inviter != LocalPlayer() && !(s_state.accepted & (1u << LocalPlayer())) && SafeFarm() && !s_popup)
    {
        TWCHAR invitation[TARRAYSIZE(s_popupText)];
        AModStrings::Format(invitation, TARRAYSIZE(invitation),
            AModStrings::Get(s_state.game == Mud ? MODSTRING_COOPMINI_MUD_INVITE :
                s_state.game == Darts ? MODSTRING_COOPMINI_DARTS_INVITE :
                s_state.game == Pool ? MODSTRING_COOPMINI_POOL_INVITE :
                DuelGame(s_state.game) ? MODSTRING_COOPMINI_DUEL_INVITE : MODSTRING_COOPMINI_INVITE), GameTitle(s_state.game));
        AModStrings::Format(s_popupText, TARRAYSIZE(s_popupText), L"%s\n%s", ACoopPartner::GetPlayerName(s_state.inviter), invitation);
        AGameStateController::GetSingleton()->PushState(new MiniPopup(TFALSE));
    }
#ifdef BARNYARD_COMMUNITY_PATCH
    if (s_popup && !s_popup->m_results && Switch("-coopminiaccept") && s_popup->GetMenuState() == ABYardMenuState::MENUSTATE_MENU_VISIBLE) s_popup->Dev_ChooseButton(0);
#endif
    if (s_nativeMatch.NeedsLoad(s_state) && SafeFarm())
    {
        auto* manager = AMiniGameManager::GetSingleton();
        const TINT game = manager->FindMiniGame(TPString8(s_names[s_state.game]));
        if (game < 0) { Cancel("minigame missing"); return; }
        auto* players = APlayerManager::GetSingleton()->GetMiniGamePlayerSet();
        players->Reset();
        players->AddHumanPlayer(AInputHandler::GetSingleton()->GetMainController(), -1);
        manager->MakeValidMiniGamePlayerSet(game);
        players->FinishUpAddingAIs();
        manager->SetMiniGameVariant(0);
        s_nativeMatch.loaded = s_state.match;
        s_nativeMatch.ready = 0;
        s_nativeMatch.playedTurn = ~0u;
        s_nativeMatch.returnStarted = TFALSE;
        s_score = 0;
        ResetMudInput();
        ResetDartsInput();
        ResetPoolInput();
        s_incomingView.size = s_outgoingView.size = 0;
        TINFO("[coopmini] loading native game=%s match=%u (farm snapshot, no quest start)\n", s_names[s_state.game], s_state.match);
        AGameLoader::SetTransitionToMiniGame(game, TTRUE);
    }
    if (s_state.phase == Results && !s_popup && s_game && AGameStateController::GetSingleton()->GetCurrentState() == s_game)
    {
        const TINT winner = ResultWinner(s_state);
        const MODSTRING outcome = winner < 0 ? MODSTRING_COOPMINI_DRAW :
            (winner == 0 ? MODSTRING_COOPMINI_HOST_WINS : MODSTRING_COOPMINI_GUEST_WINS);
        if (LowerScoreWins(s_state.game))
        {
            TWCHAR hostTime[64], guestTime[64];
            FormatRaceTime(hostTime, TARRAYSIZE(hostTime), s_state.scores[0]);
            FormatRaceTime(guestTime, TARRAYSIZE(guestTime), s_state.scores[1]);
            AModStrings::Format(s_popupText, TARRAYSIZE(s_popupText), AModStrings::Get(MODSTRING_COOPMINI_TIME_RESULTS),
                CoopMiniHud::Title, hostTime, guestTime, AModStrings::Get(outcome));
        }
        else AModStrings::Format(s_popupText, TARRAYSIZE(s_popupText), AModStrings::Get(MODSTRING_COOPMINI_RESULTS),
            CoopMiniHud::Title, s_state.scores[0], s_state.scores[1], AModStrings::Get(outcome));
        NameScorecard(s_popupText);
        AGameStateController::GetSingleton()->PushState(new MiniPopup(TTRUE));
        TINFO("[coopmini] same results shown P1=%d P2=%d\n", s_state.scores[0], s_state.scores[1]);
        QueueShot("results");
    }
    if (s_state.phase == Results && Switch("-coopminiautoleave"))
    {
        s_devResultWait += dt;
        if (s_devResultWait > 8.0f) ACoopMinigame::RequestLeave();
    }
    // A bounded unattended smoke probe ends through the normal leave path,
    // after observing the first native handoff. Golf's intro needs more than
    // eight seconds before the guest can move; QA may request up to 120 seconds.
    const TCHAR* smokeSeconds = TNULL;
    if (s_state.phase == Playing && s_state.turn >= 1 && Switch("-coopminismoke", &smokeSeconds))
    {
        s_devRoundWait += dt;
        const TINT requested = smokeSeconds ? std::atoi(smokeSeconds) : 0;
        const TFLOAT duration = requested >= 8 && requested <= 120 ? TFLOAT(requested) : 8.0f;
        if (s_devRoundWait > duration) ACoopMinigame::RequestLeave();
    }
    if (s_state.phase == Leaving)
    {
        if (s_popup && AGameStateController::GetSingleton()->GetCurrentState() == s_popup) s_popup->Remove();
        if (!s_nativeMatch.returnStarted && s_nativeMatch.loaded == s_state.match && AGameLoader::IsInMiniGame() && !ARootTask::GetSingleton()->IsTransitioning())
        {
            s_nativeMatch.returnStarted = TTRUE;
            // Restore the farm or barn from which this player joined.
            TINFO("[coopmini] returning to saved farm, match=%u\n", s_state.match);
            AGameLoader::LeaveMiniGame(TTRUE);
        }
        const TBOOL loadedHere = s_nativeMatch.loaded == s_state.match;
        if ((!loadedHere || StoryWorldRestored()) && !(s_state.farm & (1u << LocalPlayer())))
        {
            LocalCommand(Farm);
            if (s_idleSince == 0)
            {
                s_idleSince = now;
                if (loadedHere)
                {
                    const TBOOL barn = AGameLoader::IsBarnExploreMiniGame();
                    QueueShot(barn ? "barn" : "farm");
                    TINFO("[coopmini] %s restored match=%u (no minigame quest result)\n", barn ? "barn" : "farm", s_state.match);
                }
                else TINFO("[coopmini] cancelled before local load, match=%u\n", s_state.match);
            }
        }
        if (s_idleSince && now - s_idleSince > 5000 && (s_retiringPeer || s_state.farm == 3 || !connected || now - s_lastPeer > 8000))
        {
            if (s_retiringPeer || IsHost() || !connected || now - s_lastPeer > 8000)
            {
                s_state.phase = Idle;
                ++s_state.revision;
                Changed(Leaving);
            }
        }
    }
    if (s_state.phase == Idle)
    {
        s_idleSince = 0;
        s_pending = Command{};
        if (!s_game && (s_state.farm & (1u << LocalPlayer()))) s_nativeMatch = {};
        s_devResultWait = 0;
        s_devRoundWait = 0;
    }
    RefreshHudText(now);
    UpdateShot();
    s_sendWait -= dt;
    if (s_sendWait <= 0.0f) { s_sendWait = 0.2f; Send(); }
    SendView(now);
    SendMudInput(dt);
    SendDartsInput(dt);
    SendPoolInput(dt);
}
struct Registration
{
    Registration()
    {
        ACoopNet::SetDataHandler(ACoopNet::CHANNEL_MINIGAME, Receive);
        ACoopNet::AddUpdateHandler(Update);
    }
} s_registration;
}

TBOOL ACoopMinigame::CanInvite()
{
    return s_state.phase == CoopMini::Idle && !s_retiringPeer && s_boundPlayer == LocalPlayer() &&
        ACoopNet::IsConnected() && s_peerNonce &&
        AGameLoader::IsStoryMode() && AGameLoader::IsMainGameLoaded() && !AGameLoader::IsInMiniGame();
}
TBOOL ACoopMinigame::IsPlaying()
{
    return s_state.phase >= CoopMini::Loading && s_nativeMatch.loaded == s_state.match;
}
TBOOL ACoopMinigame::ShowsHudStatus()
{
    if (!s_hud || s_hud->IsForceHidden()) return TFALSE;
    return CoopMiniHud::ShowsMatchStatus(s_state.phase, s_hudText[0] != 0, s_hud->GetMask(), [] {
        return AGameStateController::GetSingleton()->GetCurrentState()->GetHUDMask();
    });
}
void ACoopMinigame::OpenMenu() { AGameStateController::GetSingleton()->PushState(new MiniMenu()); }
void ACoopMinigame::RequestInvite(TINT game)
{
    if (!CanInvite() || !CoopMini::PeerCanPlay(U32(game), s_peerFeatures)) return;
    s_requestedGame = U32(game);
    s_requestSequence = ++s_sequence;
    s_sendWait = 0;
}
void ACoopMinigame::RequestLeave() { LocalCommand(CoopMini::Leave); }
void ACoopMinigame::OnInserted(AMiniGameState* state)
{
    if (IsPlaying()) s_game = state;
}
void ACoopMinigame::OnRemoved(AMiniGameState* state)
{
    if (s_game == state)
    {
        s_game = TNULL;
        s_incomingView.size = s_outgoingView.size = 0;
        ResetMudInput();
        ResetDartsInput();
        ResetPoolInput();
    }
}
TBOOL ACoopMinigame::CanUpdate(AMiniGameState* state)
{
    if (!IsPlaying() || s_game != state) return TTRUE;
    if (s_state.phase == CoopMini::Loading && s_nativeMatch.ready != s_state.match)
    {
        s_nativeMatch.ready = s_state.match;
        LocalCommand(CoopMini::Ready);
        TINFO("[coopmini] local native game ready player=%u match=%u\n", LocalPlayer(), s_state.match);
        if (IsRealtimeMud(state)) TINFO("[coopmini] realtime mud ready local-slot=%u authority=%s match=%u\n",
            LocalPlayer(), IsHost() ? "host" : "guest", s_state.match);
    }
    const TBOOL hostGame = HostGame(s_state.game);
    if (hostGame)
    {
        if (s_state.phase != CoopMini::Playing || !IsHost()) return TFALSE;
    }
    else if (BlockInput()) return TFALSE;
    if (s_nativeMatch.playedTurn != s_state.turn)
    {
        const TBOOL firstTurn = s_nativeMatch.playedTurn == ~0u;
        s_nativeMatch.playedTurn = s_state.turn;
        // Golf owns its flyover/tee preparation and does not use the base countdown.
        if (s_state.game != CoopMini::Golf && (!hostGame || firstTurn)) state->CoopStartTurn();
        TINFO("[coopmini] local turn begins player=%u turn=%u\n", LocalPlayer(), s_state.turn);
    }
    return TTRUE;
}
TBOOL ACoopMinigame::BlockInput()
{
    return IsPlaying() && (s_state.phase != CoopMini::Playing ||
        (DuelGame(s_state.game) ? (s_state.finished & (1u << LocalPlayer())) != 0 :
            (!IsRealtimeMud() && s_state.player != LocalPlayer())) ||
        (s_pending.action == CoopMini::RoundDone && s_pending.turn == s_state.turn));
}
TBOOL ACoopMinigame::IsSpectating(AMiniGameState* state)
{
    const TBOOL viewing = HostGame(s_state.game) ? !IsHost() && (s_state.phase == Playing || s_state.phase == Results) :
        s_state.phase == Playing && s_state.player != LocalPlayer();
    return s_game == state && IsPlaying() && !DuelGame(s_state.game) && viewing &&
        (s_peerFeatures & Spectator) && ACoopNet::IsConnected();
}
TBOOL ACoopMinigame::IsScoreDuel(AMiniGameState* state)
{
    return IsPlaying() && DuelGame(s_state.game) && (!state || s_game == state);
}
const TWCHAR* ACoopMinigame::GetNativePlayerName()
{
    return ACoopPartner::GetPlayerName(DuelGame(s_state.game) ? LocalPlayer() : s_state.player);
}
TBOOL ACoopMinigame::PlayerNamesChanged(TUINT& revision)
{
    if (revision == s_playerNameVersion) return TFALSE;
    revision = s_playerNameVersion;
    return TTRUE;
}
TBOOL ACoopMinigame::SupportsGolfGroundView() { return (s_peerFeatures & CoopMini::GolfGroundView) != 0; }
TBOOL ACoopMinigame::SupportsDartsViewAim() { return (s_peerFeatures & CoopMini::DartsViewAim) != 0; }
TBOOL ACoopMinigame::SupportsDartsTimedAim() { return SupportsDartsViewAim() && (s_peerFeatures & CoopMini::DartsTimedAim) != 0; }
TBOOL ACoopMinigame::SupportsDartsBlendedAim() { return SupportsDartsViewAim() && (s_peerFeatures & CoopMini::DartsBlendedAim) != 0; }
TBOOL ACoopMinigame::SupportsMudViewInput() { return (s_peerFeatures & CoopMini::MudViewInput) != 0; }
TBOOL ACoopMinigame::SupportsPoolShotIntent() { return (s_peerFeatures & CoopMini::PoolShotIntent) != 0; }
TBOOL ACoopMinigame::IsViewLogEnabled()
{
    static const TBOOL enabled = Switch("-netlog") || Switch("-cooplog");
    return enabled;
}
TBOOL ACoopMinigame::IsRealtimeMud(AMiniGameState* state)
{
    return IsPlaying() && s_state.game == CoopMini::Mud && s_state.phase <= CoopMini::Results &&
        (!state || s_game == state);
}
TBOOL ACoopMinigame::IsRealtimeMudHost() { return IsRealtimeMud() && IsHost(); }
TUINT ACoopMinigame::GetRealtimeMudLocalPlayer() { return LocalPlayer(); }
void ACoopMinigame::SubmitMudInput(TBOOL held, TBOOL jump, TBOOL kick, TBOOL directional, TBOOL left)
{
    if (!IsRealtimeMud() || IsHost() || BlockInput()) return;
    const U32 jumpHeld = held ? 1u : 0u;
    const TBOOL changed = jump || kick || s_outgoingMudInput.jumpHeld != jumpHeld;
    s_outgoingMudInput.jumpHeld = jumpHeld;
    if (jump) ++s_outgoingMudInput.jumpPresses;
    if (kick)
    {
        ++s_outgoingMudInput.kickPresses;
        s_outgoingMudInput.kickMode = directional ? (left ? 1u : 2u) : 0u;
    }
    if (changed)
    {
        s_mudInputWait = 0.0f;
        SendMudInput(0.0f);
    }
}
void ACoopMinigame::GetMudPressCounters(TUINT& jump, TUINT& kick)
{
    jump = IsHost() ? s_incomingMudInput.consumedJump : s_outgoingMudInput.jumpPresses;
    kick = IsHost() ? s_incomingMudInput.consumedKick : s_outgoingMudInput.kickPresses;
}
TBOOL ACoopMinigame::ConsumeMudInput(MudInput& input)
{
    input = MudInput{};
    if (!IsRealtimeMudHost()) return TFALSE;
    MudControls controls;
    const TBOOL fresh = CoopMini::ConsumeMudInput(s_state, s_incomingMudInput, GetTickCount(), controls);
    input.jumpHeld = controls.jumpHeld;
    input.jumpPressed = controls.jumpPressed;
    input.kickPressed = controls.kickPressed;
    input.directionalKick = controls.directionalKick;
    input.kickLeft = controls.kickLeft;
    if (input.jumpPressed || input.kickPressed)
        TINFO("[coopmini] realtime mud input applied slot=1 match=%u held=%d jump=%d kick=%d\n",
            s_state.match, input.jumpHeld, input.jumpPressed, input.kickPressed);
    return fresh;
}
void ACoopMinigame::SetMudScores(TINT hostScore, TINT guestScore)
{
    if (!IsRealtimeMudHost()) return;
    if (hostScore < 0) hostScore = 0;
    if (guestScore < 0) guestScore = 0;
    if (CoopMini::SetMudScores(s_state, LocalPlayer(), hostScore, guestScore)) s_sendWait = 0.0f;
}
void ACoopMinigame::CompleteMudMatch(TINT hostScore, TINT guestScore)
{
    if (!IsRealtimeMudHost()) return;
    if (hostScore < 0) hostScore = 0;
    if (guestScore < 0) guestScore = 0;
    const U32 old = s_state.phase;
    if (CompleteMud(s_state, LocalPlayer(), hostScore, guestScore))
    {
        Changed(old);
        TINFO("[coopmini] realtime mud complete match=%u scores=%d,%d\n", s_state.match, hostScore, guestScore);
    }
}
TBOOL ACoopMinigame::IsRealtimeDarts(AMiniGameState* state)
{
    return IsPlaying() && s_state.game == CoopMini::Darts && s_state.phase <= CoopMini::Results &&
        (s_peerFeatures & CoopMini::DartsPvp) && (!state || s_game == state);
}
TBOOL ACoopMinigame::IsRealtimeDartsHost() { return IsRealtimeDarts() && IsHost(); }
TUINT ACoopMinigame::GetRealtimeDartsLocalPlayer() { return LocalPlayer(); }
void ACoopMinigame::SubmitDartsInput(TUINT serial, TUINT stage, TUINT viewSequence, TUINT stageTimeMs,
    TUINT previousView, TUINT blend)
{
    if (!IsRealtimeDarts() || IsHost() || BlockInput() || !serial || stage < 1 || stage > 2) return;
    if (stageTimeMs && (!SupportsDartsTimedAim() || !viewSequence || !ValidDartsStageTime(stage, stageTimeMs))) return;
    if (previousView && (!SupportsDartsBlendedAim() || !ValidDartsBlend(stage, viewSequence, previousView, blend))) return;
    s_outgoingDartsInput.throwSerial = serial;
    s_outgoingDartsInput.stage = stage;
    s_outgoingDartsView = SupportsDartsViewAim() ? viewSequence : 0;
    s_outgoingDartsTime = stageTimeMs;
    s_outgoingDartsPrevious = previousView;
    s_outgoingDartsBlend = blend;
    if (++s_outgoingDartsInput.sequence == 0) ++s_outgoingDartsInput.sequence;
    s_dartsInputWait = 0.0f;
    SendDartsInput(0.0f);
}
TBOOL ACoopMinigame::ConsumeDartsInput(TUINT serial, TUINT stage, TUINT* viewSequence, TUINT* stageTimeMs,
    TUINT* previousView, TUINT* blend)
{
    if (viewSequence) *viewSequence = 0;
    if (stageTimeMs) *stageTimeMs = 0;
    if (previousView) *previousView = 0;
    if (blend) *blend = 0;
    if (!IsRealtimeDartsHost() || serial != s_dartsTurn.serial || stage != s_dartsTurn.stage) return TFALSE;
    return CoopMini::ConsumeDartsInput(s_state, s_dartsTurn, s_incomingDartsInput, GetTickCount(),
        viewSequence, stageTimeMs, previousView, blend);
}
void ACoopMinigame::DiscardDartsViewInput(TUINT serial, TUINT stage)
{
    if (IsRealtimeDartsHost() && serial == s_dartsTurn.serial && stage == s_dartsTurn.stage)
        CoopMini::DiscardDartsViewInput(s_incomingDartsInput, serial, stage);
}
void ACoopMinigame::SetDartsState(TUINT player, TUINT round, TUINT serial, TUINT stage, TINT hostScore, TINT guestScore)
{
    if (!IsRealtimeDartsHost()) return;
    if (CoopMini::SetDartsState(s_state, LocalPlayer(), s_dartsTurn, player, round, serial, stage, hostScore, guestScore))
    {
        s_sendWait = 0.0f;
        SendCurrentState();
    }
}
void ACoopMinigame::CompleteDartsMatch(TINT hostScore, TINT guestScore)
{
    if (!IsRealtimeDartsHost()) return;
    const U32 old = s_state.phase;
    if (CompleteDarts(s_state, LocalPlayer(), hostScore, guestScore))
    {
        Changed(old);
        TINFO("[coopmini] darts complete match=%u scores=%d,%d\n", s_state.match, hostScore, guestScore);
    }
}
TBOOL ACoopMinigame::IsRealtimePool(AMiniGameState* state)
{
    return IsPlaying() && s_state.game == CoopMini::Pool && s_state.phase <= CoopMini::Results &&
        (s_peerFeatures & CoopMini::PoolPvp) && (!state || s_game == state);
}
TBOOL ACoopMinigame::IsRealtimePoolHost() { return IsRealtimePool() && IsHost(); }
TUINT ACoopMinigame::GetRealtimePoolLocalPlayer() { return LocalPlayer(); }
static void SubmitPoolControls(TUINT serial, TFLOAT aimX, TFLOAT aimY, TBOOL pressed, TBOOL released,
    const PoolShot& shot, const PoolPlacement* placement)
{
    if (!ACoopMinigame::IsRealtimePool() || IsHost() || ACoopMinigame::BlockInput() || !serial ||
        !(aimX >= -1.0f && aimX <= 1.0f) || !(aimY >= -1.0f && aimY <= 1.0f)) return;
    const TBOOL resumed = s_poolInputSuspended;
    const TBOOL changedShot = s_outgoingPoolInput.shotSerial != serial;
    const TBOOL wasAiming = s_outgoingPoolInput.aimX != 0.0f || s_outgoingPoolInput.aimY != 0.0f;
    const TBOOL aiming = aimX != 0.0f || aimY != 0.0f;
    if (changedShot || released || placement) s_outgoingPoolShot = {};
    if (changedShot || released || !placement) s_outgoingHasPlacement = TFALSE;
    if (released)
    {
        if (placement && ValidPoolPlacement(*placement))
        {
            s_outgoingPoolPlace = *placement;
            s_outgoingHasPlacement = TTRUE;
        }
        else if (!placement) s_outgoingPoolShot = shot;
    }
    CoopMini::UpdatePoolInput(s_outgoingPoolInput, serial, aimX, aimY, pressed != TFALSE, released != TFALSE);
    s_poolInputSuspended = TFALSE;
    // Begin/end of stick motion and button edges bypass the periodic refresh.
    // A held analog stick keeps its normal bounded packet cadence.
    if (pressed || released || resumed || wasAiming != aiming)
    {
        s_poolInputWait = 0.0f;
        SendPoolInput(0.0f);
    }
}
void ACoopMinigame::SubmitPoolInput(TUINT serial, TFLOAT aimX, TFLOAT aimY, TBOOL pressed, TBOOL released,
    TFLOAT shotDirX, TFLOAT shotDirZ, TFLOAT shotPower)
{
    SubmitPoolControls(serial, aimX, aimY, pressed, released, {shotDirX, shotDirZ, shotPower}, TNULL);
}
void ACoopMinigame::SubmitPoolPlacement(TUINT serial, TFLOAT aimX, TFLOAT aimY, TBOOL pressed, TBOOL released,
    TFLOAT offsetX, TFLOAT offsetZ)
{
    const PoolPlacement place{offsetX, offsetZ};
    SubmitPoolControls(serial, aimX, aimY, pressed, released, {}, &place);
}
void ACoopMinigame::SuspendPoolInput()
{
    if (!IsRealtimePool() || IsHost() || s_poolInputSuspended) return;
    s_poolInputSuspended = TTRUE;
    if (++s_outgoingPoolInput.inputEpoch == 0) ++s_outgoingPoolInput.inputEpoch;
    s_outgoingPoolInput.shootPresses = s_outgoingPoolInput.shootReleases = 0;
    s_outgoingPoolInput.aimX = s_outgoingPoolInput.aimY = 0.0f;
    s_outgoingPoolShot = {};
    s_outgoingHasPlacement = TFALSE;
}
TBOOL ACoopMinigame::ConsumePoolInput(TUINT serial, PoolInput& input)
{
    input = PoolInput{};
    if (!IsRealtimePoolHost() || serial != s_poolTurn.serial) return TFALSE;
    PoolControls controls;
    const TBOOL fresh = CoopMini::ConsumePoolInput(s_state, s_poolTurn, s_incomingPoolInput, GetTickCount(), controls);
    input.aimX = controls.aimX;
    input.aimY = controls.aimY;
    input.shootPressed = controls.shootPressed;
    input.shootReleased = controls.shootReleased;
    input.hasShotIntent = controls.hasShotIntent;
    input.shotDirX = controls.shot.dirX;
    input.shotDirZ = controls.shot.dirZ;
    input.shotPower = controls.shot.power;
    input.hasPlacementIntent = controls.hasPlacementIntent;
    input.placeX = controls.placement.x;
    input.placeZ = controls.placement.z;
    return fresh;
}
void ACoopMinigame::SetPoolState(TUINT player, TUINT serial, TUINT stage, TINT hostPotted, TINT guestPotted)
{
    if (!IsRealtimePoolHost()) return;
    if (CoopMini::SetPoolState(s_state, LocalPlayer(), s_poolTurn, player, serial, stage, hostPotted, guestPotted))
    {
        s_sendWait = 0.0f;
        SendCurrentState();
    }
}
void ACoopMinigame::CompletePoolMatch(TUINT winner, TINT hostPotted, TINT guestPotted)
{
    if (!IsRealtimePoolHost()) return;
    const U32 old = s_state.phase;
    if (CompletePool(s_state, LocalPlayer(), winner, hostPotted, guestPotted))
    {
        Changed(old);
        TINFO("[coopmini] pool complete match=%u winner=%u potted=%d,%d\n", s_state.match, winner, hostPotted, guestPotted);
    }
}
TBOOL ACoopMinigame::ShouldCaptureSpectatorFrame(AMiniGameState* state, TFLOAT dt, TBOOL force)
{
    if (s_game != state || !IsPlaying() || !(s_peerFeatures & Spectator)) return TFALSE;
    if (HostGame(s_state.game))
    {
        if (!IsHost() || (s_state.phase != Playing && s_state.phase != Results)) return TFALSE;
    }
    else if (BlockInput()) return TFALSE;
    const auto& stamp = s_outgoingView.packet.stamp;
    if (force || !s_outgoingView.size || stamp.match != s_state.match || stamp.turn != s_state.turn)
        s_viewWait = 0.0f;
    return ViewSendDue(s_viewWait, dt, (s_peerFeatures & FastViews) ? 1.0f / 60.0f : 1.0f / 30.0f);
}
void ACoopMinigame::PublishSpectatorFrame(AMiniGameState* state, const void* frame, TUINT size, TUINT* sequence, TUINT* match)
{
    if (sequence) *sequence = 0;
    if (match) *match = 0;
    if (s_game != state || !IsPlaying() || (HostGame(s_state.game) ? !IsHost() ||
        (s_state.phase != Playing && s_state.phase != Results) : BlockInput()) || !(s_peerFeatures & Spectator) ||
        !frame || !size || size > sizeof(s_outgoingView.packet.data)) return;
    if (++s_viewSequence == 0) ++s_viewSequence;
    s_outgoingView.packet.stamp = {s_state.match, s_state.turn, s_state.game, s_viewSequence};
    std::memcpy(s_outgoingView.packet.data, frame, size);
    s_outgoingView.size = size;
    s_outgoingView.tick = GetTickCount();
    SendView(s_outgoingView.tick);
    if (sequence) *sequence = s_viewSequence;
    if (match) *match = s_state.match;
}
TBOOL ACoopMinigame::ReadSpectatorFrame(AMiniGameState* state, void* frame, TUINT size, TUINT* sequence, TUINT* match)
{
    if (sequence) *sequence = 0;
    if (match) *match = 0;
    if (!IsSpectating(state) || !frame || !size || s_incomingView.size != size ||
        GetTickCount() - s_incomingView.tick > 1000 || !CanWatchView(s_state, LocalPlayer(), s_incomingView.packet.stamp)) return TFALSE;
    std::memcpy(frame, s_incomingView.packet.data, size);
    if (sequence) *sequence = s_incomingView.packet.stamp.sequence;
    if (match) *match = s_incomingView.packet.stamp.match;
    return TTRUE;
}
void ACoopMinigame::SetScore(TINT score)
{
    // Native result totals clamp negative penalties to zero.
    if (score < 0) score = 0;
    if (!IsPlaying() || HostGame(s_state.game) || BlockInput() || !CoopMini::ValidScore(score)) return;
    if (s_score == score) return;
    s_score = score;
    LocalCommand(CoopMini::Score, score);
    RefreshHudText(GetTickCount());
}
void ACoopMinigame::CompleteRound(TINT score, TBOOL final)
{
    if (score < 0) score = 0;
    if (!IsPlaying() || HostGame(s_state.game) || BlockInput()) return;
    TINFO("[coopmini] native round complete player=%u turn=%u score=%d final=%d\n", LocalPlayer(), s_state.turn, score, final);
    s_score = score;
    LocalCommand(CoopMini::RoundDone, score, final);
    RefreshHudText(GetTickCount());
}
void ACoopMinigame::UpdateOffline(TFLOAT dt)
{
    // ACoopNet intentionally stops invoking handlers after its session is closed.
    if (ACoopNet::GetMode() == ACoopNet::MODE_OFF && s_state.phase != CoopMini::Idle) Update(dt);
}
