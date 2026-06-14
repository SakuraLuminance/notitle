#pragma once

namespace ana {

struct STFTConfig
{
    int fftSize = 2048;
    int hopSize = 512;

    enum class WindowType
    {
        Hann,
        BlackmanHarris,
        Hamming
    };

    WindowType windowType      = WindowType::Hann;
    float      peakThresholdDB = -60.0f;
    int        maxPartials     = 512;
};

} // namespace ana
