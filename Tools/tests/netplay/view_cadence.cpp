#include "Player/ACoopMinigameTiming.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

int main()
{
    int failures = 0;
    for (const float fps : {30.0f, 60.0f, 120.0f, 144.0f})
    {
        float untilNext = 0.0f;
        int framesSent = 0;
        for (int frame = 0; frame < int(10 * fps); ++frame)
            framesSent += CoopMini::ViewSendDue(untilNext, 1.0f / fps, 1.0f / 60.0f);
        const int expected = fps < 60.0f ? int(fps * 10.0f) : 600;
        const bool pass = std::abs(framesSent - expected) <= 1;
        std::printf("fast views %.0f FPS: %d snapshots in 10s, expected %d %s\n",
                    fps, framesSent, expected, pass ? "PASS" : "FAIL");
        failures += !pass;
    }
    for (const float fps : {30.0f, 60.0f, 144.0f, 59.94f})
    {
        float wait = 0.0f;
        int sent = 0;
        const int frames = int(10.0f * fps);
        for (int i = 0; i < frames; ++i)
            if (CoopMini::ViewSendDue(wait, 1.0f / fps)) ++sent;
        const bool pass = std::abs(sent - 300) <= 1;
        std::printf("fps=%.2f packets=%d expected=300 %s\n", fps, sent, pass ? "PASS" : "FAIL");
        failures += !pass;
    }
    float wait = 0.0f, time = 0.0f;
    int sent = 0;
    for (int i = 0; i < 600; ++i)
    {
        const float dt = (i % 2) ? 0.022f : 0.0113333333f;
        time += dt;
        sent += CoopMini::ViewSendDue(wait, dt);
    }
    const bool jitter = std::abs(sent - int(time * 30.0f)) <= 1;
    std::printf("jitter time=%.3f packets=%d %s\n", time, sent, jitter ? "PASS" : "FAIL");
    failures += !jitter;
    wait = 0.0f;
    const bool resumed = CoopMini::ViewSendDue(wait, 2.0f);
    int burst = 0;
    for (int i = 0; i < 10; ++i) burst += CoopMini::ViewSendDue(wait, 0.0f);
    const bool bounded = resumed && !burst;
    std::printf("long stall single-send=%d burst=%d %s\n", resumed, burst, bounded ? "PASS" : "FAIL");
    failures += !bounded;
    return failures ? 1 : 0;
}
