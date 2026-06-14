#pragma once
#include <vector>
#include <cstdint>

namespace ana {

struct Partial
{
    float frequency = 0.0f;  // Hz
    float amplitude = 0.0f;  // 0.0 - 1.0
    float phase     = 0.0f;  // radians
};

struct PartialFrame
{
    std::vector<Partial> partials;
    double timestamp = 0.0;  // seconds
};

struct PartialData
{
    std::vector<PartialFrame> frames;
    int maxPartials = 512;     // max partials per frame
    double sampleRate = 44100.0;
    double hopSize    = 512.0;
};

} // namespace ana
