#include "MiniGame/AWhackInput.h"
#include <cstdio>

int main()
{
    int failures = 0;
    WhackInput::KeyAxis keys;
    int column = 1;
    // A key pressed during the spectator turn can be released after control
    // returns. This player did not press it during their own turn.
    if (keys.Set(column, -1, false) || column != 1 || keys.Held())
    {
        std::puts("FAIL unmatched key-up moved the ring after a turn change");
        ++failures;
    }
    column = 1;
    keys.Set(column, -1, true);
    keys.Set(column, -1, true);
    keys.Set(column, 1, true);
    keys.Set(column, -1, false);
    if (column != 2 || !keys.Held())
    {
        std::puts("FAIL releasing left must preserve a held right key");
        ++failures;
    }
    keys.Set(column, 1, false);
    if (column != 1 || keys.Held()) ++failures;
    if (!WhackInput::MouseMoved(-3, 0) || !WhackInput::MouseMoved(0, 2) ||
        !WhackInput::MouseMoved(1, -1) || WhackInput::MouseMoved(0, 0))
    {
        std::puts("FAIL straight mouse movement must take control, idle motion must not");
        ++failures;
    }
    struct { int iCol = 1, iRow = 1, iHole = 4; } player;
    WhackInput::SelectMouseHole(player, 2, 3);
    if (player.iCol != 2 || player.iRow != 0 || player.iHole != 2)
    {
        std::puts("FAIL ring and strike must select hole 2 in the same update");
        ++failures;
    }
    WhackInput::SelectMouseHole(player, -1, 3);
    if (player.iCol != 2 || player.iRow != 0 || player.iHole != 2)
    {
        std::puts("FAIL a cursor outside the board must preserve the selected hole");
        ++failures;
    }
    if (!failures) std::puts("PASS axis-only mouse aiming, immediate strike selection, held and unmatched keys");
    return failures != 0;
}
