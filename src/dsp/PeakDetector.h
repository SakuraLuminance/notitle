#pragma once
#include <vector>
#include <complex>
#include "PartialData.h"
#include "STFTConfig.h"

namespace ana {

class PeakDetector
{
public:
    PeakDetector();
    ~PeakDetector();

    std::vector<Partial> detectPeaks(
        const std::vector<std::complex<float>>& spectrum,
        const STFTConfig& config,
        double sampleRate);
};

} // namespace ana
