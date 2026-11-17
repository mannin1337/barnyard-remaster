#pragma once
#include <cmath>
#include <cstdint>

namespace GolfGround
{
// Position relative to the gopher (1/128 world units). Heading has 32 steps.
// Each native slot has two animations: left/right trail or surface/submerge.
struct Piece
{
    std::int16_t x{}, y{}, z{};
    std::uint8_t flags{}, phase{};

    unsigned Type() const { return flags & 3u; } // zero means hidden
    bool SecondClip() const { return (flags & 4u) != 0; }
    float Heading() const { return float(flags >> 3) * (6.283185307f / 32.0f); }
    float Progress() const { return phase / 255.0f; }
};
static_assert(sizeof(Piece) == 8);

inline Piece Encode(float x, float y, float z, float heading, unsigned type, bool second, float progress)
{
    if (!type || type > 3 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::fabs(x) > 255.99f || std::fabs(y) > 255.99f || std::fabs(z) > 255.99f ||
        !std::isfinite(heading) || std::fabs(heading) > 6.284f || !std::isfinite(progress)) return {};
    const unsigned yaw = unsigned(std::lround(heading * (32.0f / 6.283185307f))) & 31u;
    const float phase = progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress;
    return {std::int16_t(std::lround(x * 128.0f)), std::int16_t(std::lround(y * 128.0f)),
            std::int16_t(std::lround(z * 128.0f)), std::uint8_t((yaw << 3) | (second ? 4u : 0u) | type),
            std::uint8_t(std::lround(phase * 255.0f))};
}
}
