#include "MiniGame/ACoopMinigameHudStatus.h"
#include <cstdio>
#include <cstdlib>

namespace
{
void Expect(bool matches, const char* message)
{
    if (!matches)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
}

int main()
{
    using namespace CoopMini;
    using namespace CoopMiniHud;
    constexpr U32 minigame = 1u << 1, interaction = 1u << 7, playerMain = 1u << 30;
    constexpr U32 scorecard = minigame | playerMain;
    constexpr U32 challenge = minigame | interaction;
    constexpr float frame = 1.0f / 64.0f;
    unsigned maskReads = 0;
    const auto shows = [&](U32 phase, bool hasText, U32 screenMask) {
        return ShowsMatchStatus(phase, hasText, scorecard, [&] { ++maskReads; return screenMask; });
    };

    // Chasing Chicks and Veggie Patch Defender enable both HUD elements. A farm
    // line already at full opacity must disappear on the scorecard's first frame.
    const bool playing = shows(Playing, true, challenge);
    Expect(playing, "The LAN scorecard should be drawn on the shared challenge HUD");
    Expect(PartnerStatusAlpha(1.0f, frame, true, playing) == 0.0f,
           "The white farm status overlaps the first frame of the LAN scorecard");

    for (const auto phase : {Offer, Loading, Playing, Leaving})
    {
        Expect(shows(phase, true, playerMain), "Missing LAN status on the farm");
        const auto before = maskReads;
        Expect(!shows(phase, false, challenge), "Empty scorecard owns the line");
        Expect(maskReads == before, "An empty scorecard should not query the game state's HUD mask");
        Expect(!shows(phase, true, 0), "Modal dialog owns both HUD elements");
        Expect(!shows(phase, true, interaction), "Barn only shows the farm HUD");
    }
    const auto before = maskReads;
    Expect(!shows(Results, true, challenge), "Results are shown in a popup");
    const bool idle = shows(Idle, true, playerMain);
    Expect(maskReads == before, "An inactive scorecard should not query the game state's HUD mask");
    Expect(!idle, "Stale match text should not hide a partner's solo activity on the farm");
    const float restored = PartnerStatusAlpha(0.0f, frame, true, idle);
    Expect(restored == frame * 4.0f, "The farm status should fade back in after the match");
    Expect(PartnerStatusAlpha(restored, frame, false, idle) == 0.0f, "Farm status should fade out normally");
    Expect(PartnerStatusAlpha(0.0f, frame, false, idle) == 0.0f, "Offline play should have no farm status");
}
