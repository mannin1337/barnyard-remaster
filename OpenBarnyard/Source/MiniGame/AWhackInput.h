#pragma once

namespace WhackInput
{
class KeyAxis
{
    bool negative = false, positive = false;
public:
    bool Held() const { return negative || positive; }
    bool Set(int& position, int direction, bool down)
    {
        bool& key = direction < 0 ? negative : positive;
        if (key == down) return false;
        key = down;
        position = 1 + int(positive) - int(negative);
        return true;
    }
};

inline bool MouseMoved(float dx, float dy)
{
    return dx != 0.0f || dy != 0.0f;
}

template<class Player>
void SelectMouseHole(Player& player, int hole, int columns)
{
    if (hole >= 0)
    {
        player.iCol = hole % columns;
        player.iRow = hole / columns;
        player.iHole = hole;
    }
}
}
