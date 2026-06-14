#pragma once
#include <vector>
#include <cstdint>

namespace ana {

struct AudioFileData
{
    std::vector<float> samples;       // mono, 32-bit float
    double sampleRate      = 44100.0;
    int    numChannels     = 1;       // should be 1 after conversion
    double durationSeconds = 0.0;
};

} // namespace ana
