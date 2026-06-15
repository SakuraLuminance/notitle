#include "MultibandProcessor.h"
#include <algorithm>
#include <cmath>

namespace ana {

void MultibandProcessor::setNumBands(int numBands)
{
    if (numBands < 0)
        numBands = 0;

    bands_.resize(static_cast<size_t>(numBands));

    // Assign default evenly-spaced ranges
    const int n = static_cast<int>(bands_.size());
    for (int i = 0; i < n; ++i)
    {
        bands_[static_cast<size_t>(i)].lowHz  = (20000.0f / n) * i;
        bands_[static_cast<size_t>(i)].highHz = (20000.0f / n) * (i + 1);
        bands_[static_cast<size_t>(i)].gain    = 1.0f;
        bands_[static_cast<size_t>(i)].bypassed = false;
    }
}

void MultibandProcessor::setBandRange(int bandIndex, float lowHz, float highHz)
{
    if (bandIndex < 0 || static_cast<size_t>(bandIndex) >= bands_.size())
        return;

    if (lowHz > highHz)
        std::swap(lowHz, highHz);

    bands_[static_cast<size_t>(bandIndex)].lowHz  = std::max(0.0f, lowHz);
    bands_[static_cast<size_t>(bandIndex)].highHz = std::max(0.0f, highHz);
}

void MultibandProcessor::setBandGain(int bandIndex, float gain)
{
    if (bandIndex < 0 || static_cast<size_t>(bandIndex) >= bands_.size())
        return;

    bands_[static_cast<size_t>(bandIndex)].gain = gain;
}

void MultibandProcessor::bypassBand(int bandIndex, bool bypass)
{
    if (bandIndex < 0 || static_cast<size_t>(bandIndex) >= bands_.size())
        return;

    bands_[static_cast<size_t>(bandIndex)].bypassed = bypass;
}

void MultibandProcessor::process(PartialDataSIMD& partials)
{
    const int numBands = static_cast<int>(bands_.size());
    if (numBands == 0)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float freq = partials.frequency[i];

        // Find the first band this partial's frequency falls into
        int bandIdx = -1;
        for (int b = 0; b < numBands; ++b)
        {
            if (freq >= bands_[static_cast<size_t>(b)].lowHz &&
                freq <  bands_[static_cast<size_t>(b)].highHz)
            {
                bandIdx = b;
                break;
            }
        }

        // If no band matched or the band is bypassed, pass through unchanged
        if (bandIdx < 0 || bands_[static_cast<size_t>(bandIdx)].bypassed)
            continue;

        partials.amplitude[i] *= bands_[static_cast<size_t>(bandIdx)].gain;
    }
}

int MultibandProcessor::getNumBands() const
{
    return static_cast<int>(bands_.size());
}

const FrequencyBand& MultibandProcessor::getBand(int index) const
{
    static const FrequencyBand defaultBand;

    if (index < 0 || static_cast<size_t>(index) >= bands_.size())
        return defaultBand;

    return bands_[static_cast<size_t>(index)];
}

} // namespace ana
