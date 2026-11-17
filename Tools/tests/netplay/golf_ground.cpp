#ifdef NDEBUG
#undef NDEBUG
#endif
#include "MiniGame/AGolfMinigameState_Ground.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

int main()
{
    constexpr float circle = 6.283185307f;
    std::array<GolfGround::Piece, 25> sent{};
    static_assert(sizeof(sent) == 200);
    for (unsigned i = 0; i < sent.size(); ++i)
    {
        const float heading = -3.14f + i * 0.25f;
        sent[i] = GolfGround::Encode(-0.403f * i, 0.017f * i, 0.412f * i, heading,
                                     1 + i % 3, i % 2 != 0, i / 24.0f);
    }
    std::array<GolfGround::Piece, 25> received{};
    std::memcpy(received.data(), sent.data(), sizeof(sent));
    for (unsigned i = 0; i < received.size(); ++i)
    {
        const auto& piece = received[i];
        assert(piece.Type() == 1 + i % 3 && piece.SecondClip() == (i % 2 != 0));
        assert(std::fabs(piece.x / 128.0f + 0.403f * i) <= 0.004f);
        assert(std::fabs(piece.y / 128.0f - 0.017f * i) <= 0.004f);
        assert(std::fabs(piece.z / 128.0f - 0.412f * i) <= 0.004f);
        assert(std::fabs(std::remainder(piece.Heading() - (-3.14f + i * 0.25f), circle)) <= circle / 64.0f);
        assert(std::fabs(piece.Progress() - i / 24.0f) <= 0.002f);
    }
    // A lost middle packet cannot leave stale pieces once a new snapshot arrives.
    sent.fill({});
    sent[24] = GolfGround::Encode(2, -1, 3, 0, 3, false, 2.0f);
    std::memcpy(received.data(), sent.data(), sizeof(sent));
    for (unsigned i = 0; i < 24; ++i) assert(received[i].Type() == 0);
    assert(received[24].Type() == 3 && received[24].Progress() == 1.0f);
    assert(!GolfGround::Encode(300, 0, 0, 0, 1, false, 0).Type());
    assert(!GolfGround::Encode(0, 0, 0, 0, 4, false, 0).Type());
    assert(!GolfGround::Encode(0, 0, 0, std::numeric_limits<float>::infinity(), 1, false, 0).Type());
    assert(!GolfGround::Encode(0, 0, 0, 0, 1, false, std::numeric_limits<float>::quiet_NaN()).Type());
    std::puts("PASS: 25 piece slots, xyz and heading, both clips, animation phase, expiry, bounded encoding");
}
