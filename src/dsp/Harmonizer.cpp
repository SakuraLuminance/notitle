#include "Harmonizer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_BitScanForward)
#endif

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
    recomputeTables();
}

void Harmonizer::setShift(float shift)
{
    shift_ = shift; // can be negative for downward shifts
    recomputeTables();
}

void Harmonizer::setShiftMode(bool useOctaves)
{
    useOctaves_ = useOctaves;
    recomputeTables();
}

void Harmonizer::setGap(float gap)
{
    gap_ = gap;
}

void Harmonizer::setSampleRate(double sr)
{
    sampleRate_ = sr > 0.0 ? sr : 44100.0;
}

//==============================================================================
void Harmonizer::recomputeTables()
{
    for (int ci = 1; ci <= 12; ++ci)
    {
        const float fCi = static_cast<float>(ci);
        if (useOctaves_)
            shiftRatios_[ci] = std::pow(2.0f, shift_ * fCi / 12.0f);
        else
            shiftRatios_[ci] = 1.0f + shift_ * fCi;
        strengthPow_[ci] = std::pow(strength_, fCi);
    }
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
            // --- Look up precomputed frequency multiplier ---
            const float multiplier = shiftRatios_[ci];

            // --- Add inharmonic gap offset (in Hz) ---
            const float gapOffset = gap_ * static_cast<float>(ci) * 20.0f;
            const float newFreq = baseFreq * multiplier + gapOffset;

            // --- Clamp to audible / representable range ---
            if (newFreq <= 20.0f || newFreq >= static_cast<float>(nyquist))
                continue;

            // --- Look up precomputed clone amplitude ---
            const float newAmp = baseAmp * strengthPow_[ci] * amount_;

            if (newAmp <= ampThreshold)
                continue;

            // --- Find an empty slot via bitmap + CTZ (O(1)) ---
            uint32_t fullMask = 0;
            for (int w = 0; w < 16; ++w)
                fullMask |= data.activeMask[w];
            const uint32_t freeMask = ~fullMask;

            int freeSlot = -1;
            if (freeMask != 0)
            {
#if defined(_MSC_VER)
                unsigned long idx = 0;
                _BitScanForward(&idx, freeMask);
                freeSlot = static_cast<int>(idx);
#else
                freeSlot = static_cast<int>(__builtin_ctz(freeMask));
#endif
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
