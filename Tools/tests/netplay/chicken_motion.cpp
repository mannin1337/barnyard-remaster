#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include "MiniGame/AChickenFireSpectatorMotion.h"

struct Frame { float bird, camera; };

int main()
{
    ChickenSpectator::Playback<Frame> playback;
    Frame input{}, output{}, last{};
    unsigned serial = 0;
    unsigned match = 1;
    auto tick = [&](float dt) {
        float alpha = 0;
        return playback.Render(input, serial, match, dt, output, alpha,
            [](Frame& shown, const Frame& next, float t) {
                shown.bird += (next.bird - shown.bird) * t;
                shown.camera += (next.camera - shown.camera) * t;
            });
    };
    int noPacketFramesMoved = 0;
    for (int i = 0; i < 120; ++i)
    {
        const bool fresh = (i % 4 == 0 && i != 52);
        if (fresh) { ++serial; input = { float(i), float(i*2) }; }
        tick(1.0f / 120);
        if (i > 9 && !fresh && output.bird > last.bird && output.camera > last.camera)
            ++noPacketFramesMoved;
        assert(output.bird >= last.bird && output.bird <= input.bird);
        last = output;
    }
    if (noPacketFramesMoved < 75) {
        std::printf("FAIL bird and camera moved on only %d frames without a packet\n", noPacketFramesMoved);
        return 1;
    }
    for (int i = 0; i < 120; ++i) tick(1.0f / 120);
    assert(output.bird == input.bird && output.camera == input.camera);
    ++serial; input = {5000, 5000}; tick(1.0f / 120);
    assert(output.bird == input.bird);
    ++match; input = {-100, -200}; tick(1.0f / 120);
    assert(output.bird == input.bird && output.camera == input.camera);
    std::printf("PASS %d frames advanced bird and camera without a packet; loss, stop, reconnect and match reset passed\n", noPacketFramesMoved);
}
