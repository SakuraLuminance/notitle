#include "FMEngine.h"

#include <algorithm>

namespace ana {

// ============================================================================
// Parameter setters
// ============================================================================

void FMEngine::setFMIndex(float index)
{
    fmIndex_ = std::max(0.0f, index);
}

void FMEngine::setAMDepth(float depth)
{
    amDepth_ = std::clamp(depth, 0.0f, 1.0f);
}

// ============================================================================
// Frequency Modulation (FM)
// ============================================================================
// For each active partial:
//   frequency[i] += modulator.amplitude[i] * modIndex * modulator.frequency[i]
// Clamped to valid audible range [20 Hz, Nyquist].
// ============================================================================

void FMEngine::processFM(PartialDataSIMD& partials,
                         const PartialDataSIMD& modulator,
                         float modIndex)
{
    const float nyquist = static_cast<float>(partials.sampleRate * 0.5);
    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float delta = modulator.amplitude[i] * modIndex * modulator.frequency[i];
        partials.frequency[i] = std::clamp(partials.frequency[i] + delta, 20.0f, nyquist);
    }
}

// ============================================================================
// Amplitude Modulation (AM)
// ============================================================================
// For each active partial:
//   amplitude[i] *= (1.0f + modulator.amplitude[i] * amDepth)
// Clamped to [0, 1].
// ============================================================================

void FMEngine::processAM(PartialDataSIMD& partials,
                         const PartialDataSIMD& modulator,
                         float modDepth)
{
    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float mod = 1.0f + modulator.amplitude[i] * modDepth;
        partials.amplitude[i] = std::clamp(partials.amplitude[i] * mod, 0.0f, 1.0f);
    }
}

} // namespace ana
