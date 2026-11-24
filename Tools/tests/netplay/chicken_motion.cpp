#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "MiniGame/AChickenFireSpectatorMotion.h"

struct Frame { float bird, camera; unsigned state; };

int main()
{
    ChickenSpectator::Playback<Frame> playback;
    Frame input{}, output{}, last{};
    unsigned serial = 0;
    unsigned match = 1;
    auto tick = [&](float dt) {
        float alpha = 0;
        // Production decodes and renders into the same frame.
        output = input;
        return playback.Render(output, serial, match, dt, output, alpha,
            [](Frame& shown, const Frame& start, float t) {
                if (shown.state != start.state) return;
                shown.bird = start.bird + (shown.bird - start.bird) * t;
                shown.camera = start.camera + (shown.camera - start.camera) * t;
            });
    };
    // A posted motion sample must start moving on the same render, for both
    // old 30 Hz sources and faster peers. Stop at the last received pose.
    for (const int framesPerPacket : {4, 2})
    {
        ++match; ++serial; input = {0, 0}; tick(1.0f / 120);
        for (int i = 1; i < framesPerPacket; ++i) tick(1.0f / 120);
        ++serial; input = {1, 2}; tick(1.0f / 120);
        if (!(output.bird > 0 && output.camera > 0))
        {
            std::printf("FAIL source %d Hz: first moving snapshot produced no motion\n", 120 / framesPerPacket);
            return 1;
        }
        for (int i = 1; i < framesPerPacket; ++i) tick(1.0f / 120);
        assert(output.bird == 1 && output.camera == 2);
        ++serial; input = {20, 40, 1}; tick(1.0f / 120);
        assert(output.state == 1 && output.bird == 20 && output.camera == 40);
    }
    ++match;
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
