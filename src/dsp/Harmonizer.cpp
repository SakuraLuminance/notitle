#include "Harmonizer.h"
#include <algorithm>
#include <cmath>

namespace ana {

//==============================================================================
Harmonizer::Harmonizer()
{
}

//==============================================================================
void Harmonizer::setAmount(float amount)
{
    amount_ = std::clamp(amount, 0.0f, 1.0f);
}

void Harmonizer::setWidth(float width)
{
    width_ = std::clamp(width, 0.0f, 1.0f);
}

void Harmonizer::setStrength(float strength)
{
    strength_ = std::clamp(strength, 0.0f, 1.0f);
}

void Harmonizer::setShift(float shift)
{
    shift_ = shift; // can be negative for downward shifts
}

void Harmonizer::setGap(float gap)
{
    gap_ = gap;
}

void Harmonizer::setShiftMode(bool useOctaves)
{
    useOctaves_ = useOctaves;
}

void Harmonizer::setSampleRate(double sr)
{
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
}

//==============================================================================
void Harmonizer::reset()
{
    // No internal state to reset — parameters are set externally.
}

//==============================================================================
int Harmonizer::calcCloneCount(float width,
                                int originalCount,
                                int maxPartials) const noexcept
{
    if (width <= 0.0f || maxPartials <= 0)
        return 0;

    const int maxClonesByWidth = std::max(1, static_cast<int>(width * 12.0f));
    const int capacity         = maxPartials - originalCount;

    return std::min(maxClonesByWidth, std::max(0, capacity));
}

//==============================================================================
void Harmonizer::process(PartialDataSIMD& data)
{
    if (amount_ <= 0.0f || data.activeCount == 0)
        return;

    applyHarmonization(data);
}

//==============================================================================
void Harmonizer::applyHarmonization(PartialDataSIMD& data)
{
    const double nyquist = sampleRate_ * 0.49;
    const float  ampThreshold = 1e-6f;

    // Snapshot original partial indices
    int origIndices[PartialDataSIMD::kMaxPartials];
    int origCount = 0;
    
    for (int i = 0; i < data.maxPartials; ++i)
    {
        if (data.isActive(i))
        {
            origIndices[origCount++] = i;
        }
    }

    if (origCount == 0)
        return;

    // Determine how many clones we can fit
    const int numClones = calcCloneCount(width_, origCount, data.maxPartials);
    if (numClones == 0)
        return;

    for (int o = 0; o < origCount; ++o)
    {
        const int p = origIndices[o];
        const float baseFreq = data.frequency[p];
        const float baseAmp  = data.amplitude[p];
        const float basePhase = data.phase[p];

        if (baseAmp <= ampThreshold || baseFreq <= 0.0f)
            continue;

        for (int ci = 1; ci <= numClones; ++ci)
        {
            // --- Compute frequency multiplier ---
            float multiplier;
            if (useOctaves_)
                multiplier = std::pow(2.0f, shift_ * static_cast<float>(ci) / 12.0f);
            else
                multiplier = 1.0f + shift_ * static_cast<float>(ci);

            // --- Add inharmonic gap offset (in Hz) ---
            const float gapOffset = gap_ * static_cast<float>(ci) * 20.0f;
            const float newFreq = baseFreq * multiplier + gapOffset;

            // --- Clamp to audible / representable range ---
            if (newFreq <= 20.0f || newFreq >= static_cast<float>(nyquist))
                continue;

            // --- Compute clone amplitude ---
            const float newAmp = baseAmp
                               * std::pow(strength_, static_cast<float>(ci))
                               * amount_;

            if (newAmp <= ampThreshold)
                continue;

            // Find an empty slot
            int freeSlot = -1;
            for (int i = 0; i < data.maxPartials; ++i)
            {
                if (!data.isActive(i))
                {
                    freeSlot = i;
                    break;
                }
            }

            if (freeSlot == -1)
            {
                // No more space
                return;
            }

            // Set clone
            data.frequency[freeSlot] = newFreq;
            data.amplitude[freeSlot] = newAmp;
            data.phase[freeSlot]     = basePhase;

            // Update mask immediately so next clone finds a different slot
            data.activeMask[freeSlot >> 5] |= (1u << (freeSlot & 31));
            data.activeCount++;
        }
    }
}

} // namespace ana
