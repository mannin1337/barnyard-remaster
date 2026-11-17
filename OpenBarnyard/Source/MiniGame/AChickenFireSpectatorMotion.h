#pragma once
#include <cmath>

namespace ChickenSpectator
{
// CPV1 contains no sender timestamp. Keep two send intervals in hand and play
// the validated samples on the receiver's clock. Never predict past a packet.
template<class Frame> class Playback
{
    static constexpr int CAPACITY = 6;
    static constexpr double DELAY = 2.0 / 30.0;
    struct Sample { Frame frame; double time; };
    Sample samples[CAPACITY]{};
    int head = 0, count = 0;
    unsigned lastSequence = 0, lastMatch = 0;
    double clock = 0;

    int At(int offset) const { return (head + offset) % CAPACITY; }
    void Pop() { head = At(1); --count; }

public:
    template<class Blend>
    bool Render(const Frame& incoming, unsigned sequence, unsigned match, float dt,
        Frame& shown, float& alpha, Blend blend)
    {
        if (std::isfinite(dt) && dt > 0) clock += dt;
        const bool fresh = !count || sequence != lastSequence || match != lastMatch;
        if (fresh)
        {
            if (count && (match != lastMatch || clock - samples[At(count-1)].time > 0.2))
                count = 0;
            if (count == CAPACITY) Pop();
            samples[At(count)] = {incoming, clock};
            ++count;
            lastSequence = sequence;
            lastMatch = match;
        }
        const double renderAt = clock - DELAY;
        while (count > 1 && samples[At(1)].time <= renderAt) Pop();
        const Sample& first = samples[head];
        shown = first.frame;
        alpha = 0;
        if (count > 1 && renderAt > first.time)
        {
            const Sample& next = samples[At(1)];
            alpha = static_cast<float>((renderAt - first.time) / (next.time - first.time));
            blend(shown, next.frame, alpha);
        }
        return fresh;
    }
};
}
