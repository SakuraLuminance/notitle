#include "PhasePropagation.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace ana {

PhasePropagation::PhasePropagation()
{
}

PhasePropagation::~PhasePropagation()
{
}

void PhasePropagation::propagatePhases(
    PartialData& partialData,
    const STFTConfig& config)
{
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    const double hopSizeDouble = static_cast<double>(config.hopSize);
    const double sampleRateDouble = static_cast<double>(partialData.sampleRate);

    // For each frame after the first, accumulate phase
    for (size_t frameIdx = 1; frameIdx < partialData.frames.size(); ++frameIdx)
    {
        auto& currentFrame = partialData.frames[frameIdx];
        const auto& prevFrame = partialData.frames[frameIdx - 1];

        for (auto& partial : currentFrame.partials)
        {
            // Find nearest FFT bin
            double binF = static_cast<double>(partial.frequency) * static_cast<double>(config.fftSize) / sampleRateDouble;
            int bin = static_cast<int>(std::round(binF));
            double delta = binF - static_cast<double>(bin);

            // Interpolate phase linearly
            double phaseK = static_cast<double>(partial.phase);
            double phaseK1 = static_cast<double>(partial.phase);  // simplified: use same phase

            if (bin >= 0 && bin < config.fftSize / 2)
            {
                partial.phase = static_cast<float>((1.0 - delta) * phaseK + delta * phaseK1);
            }

            // Accumulate phase across frames
            double phaseIncrement = twoPi * static_cast<double>(partial.frequency) * hopSizeDouble / sampleRateDouble;
            
            double newPhase = static_cast<double>(partial.phase) + phaseIncrement;

            // Wrap phase to [-pi, pi] (safe for arbitrarily large values)
            partial.phase = static_cast<float>(std::remainder(newPhase, twoPi));
        }
    }
}

} // namespace ana
