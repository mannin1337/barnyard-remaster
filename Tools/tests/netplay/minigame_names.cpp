#include "MiniGame/ACoopMinigameHudText.h"
#include <cstdlib>
#include <iostream>

int main()
{
    using namespace CoopMiniHud;
    const Pair english{L"P1", L"P2"}, hungarian{L"J1", L"J2"};
    const Pair players{L"Bimbo", L"Gyurcsany"}, swapped{players[1], players[0]};
    const auto expect = [](const std::wstring& value, std::wstring_view expected)
    {
        if (value != expected)
        {
            std::wcerr << L"Expected [" << expected << L"], got [" << value << L"]\n";
            std::exit(1);
        }
    };
    for (const auto& names : {players, swapped})
    {
        const std::wstring host(names[0]), guest(names[1]);
        expect(Scorecard(L"\x1f | P1: 42   P2: 17\nYou control P1", english, names, L"Mud Jumpers"),
               L"Mud Jumpers | " + host + L": 42   " + guest + L": 17\nYou control " + host);
        expect(Scorecard(L"\x1f | P1: 42   P2: 17\nYou control P2", english, names, L"Mud Jumpers"),
               L"Mud Jumpers | " + host + L": 42   " + guest + L": 17\nYou control " + guest);
        expect(Scorecard(L"J1 (host): 12\nJ2 (guest): 18\nJ2 nyert!", hungarian, names, L""),
               host + L" (host): 12\n" + guest + L" (guest): 18\n" + guest + L" nyert!");
        expect(Scorecard(L"P1: 00:42.020  P2: 00:45.017\nP1 wins!", english, names, L""),
               host + L": 00:42.020  " + guest + L": 00:45.017\n" + host + L" wins!");
    }
    expect(Scorecard(L"\x1f | P1 / P2. XP1 P10 P2_hi", english, {L"P2%s", L"P1"}, L"P1 Cup"),
           L"P1 Cup | P2%s / P1. XP1 P10 P2_hi");
    expect(Scorecard(L"Blue 2, red 1", english, players, L""),
           L"Blue 2, red 1\nP1: Bimbo\nP2: Gyurcsany");
    return 0;
}
