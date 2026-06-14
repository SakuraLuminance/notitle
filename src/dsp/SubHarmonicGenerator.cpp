#include "SubHarmonicGenerator.h"
#include <cmath>
#include <algorithm>

namespace ana {

// ===========================================================================
// Construction
// ===========================================================================

SubHarmonicGenerator::SubHarmonicGenerator()
{
}

// ===========================================================================
// Configuration
// ===========================================================================

void SubHarmonicGenerator::setConfig(const SubHarmonicConfig& config)
{
    config_ = config;
}

void SubHarmonicGenerator::setMode(SubHarmonicMode mode)
{
    config_.mode = mode;
}

void SubHarmonicGenerator::setSubLevel(int index, float level)
{
    level = std::clamp(level, 0.0f, 1.0f);

    switch (index)
    {
        case 0:  config_.level1 = level; break;
        case 1:  config_.level2 = level; break;
        case 2:  config_.level3 = level; break;
        default: break;
    }
}

void SubHarmonicGenerator::setSampleRate(double sr)
{
    sampleRate_ = sr;
}

// ===========================================================================
// Processing
// ===========================================================================

int SubHarmonicGenerator::generate(float fundamentalFreq,
                                    float* outFrequencies,
                                    float* outAmplitudes,
                                    int maxOutput)
{
    if (maxOutput < 1 || fundamentalFreq <= 0.0f)
        return 0;

    // Sub-harmonic 1 is always one octave below the fundamental
    const float sub1Freq = fundamentalFreq * 0.5f;
    int count = 0;

    if (config_.level1 > 0.0f)
    {
        outFrequencies[count] = sub1Freq;
        outAmplitudes[count]  = std::clamp(config_.level1, 0.0f, 1.0f);
        ++count;
    }

    if (config_.mode == SubHarmonicMode::Below)
    {
        // --- Below mode: integer octave divisions ---------------------------
        // sub1 = fundamental * 0.5   (1 octave down)
        // sub2 = fundamental * 0.25  (2 octaves down)
        // sub3 = fundamental * 0.125 (3 octaves down)

        if (count < maxOutput && config_.level2 > 0.0f)
        {
            outFrequencies[count] = fundamentalFreq * 0.25f;
            outAmplitudes[count]  = std::clamp(config_.level2, 0.0f, 1.0f);
            ++count;
        }

        if (count < maxOutput && config_.level3 > 0.0f)
        {
            outFrequencies[count] = fundamentalFreq * 0.125f;
            outAmplitudes[count]  = std::clamp(config_.level3, 0.0f, 1.0f);
            ++count;
        }
    }
    else
    {
        // --- Around mode: sub-1 harmonic stack ------------------------------
        // sub1 = fundamental * 0.5   (1 octave down)
        // sub2 = sub1 * 3.0          (3rd harmonic of sub-1)
        // sub3 = sub1 * 5.0          (5th harmonic of sub-1)

        if (count < maxOutput && config_.level2 > 0.0f)
        {
            outFrequencies[count] = sub1Freq * 3.0f;
            outAmplitudes[count]  = std::clamp(config_.level2, 0.0f, 1.0f);
            ++count;
        }

        if (count < maxOutput && config_.level3 > 0.0f)
        {
            outFrequencies[count] = sub1Freq * 5.0f;
            outAmplitudes[count]  = std::clamp(config_.level3, 0.0f, 1.0f);
            ++count;
        }
    }

    return count;
}

void SubHarmonicGenerator::processInPlace(float* frequencies,
                                           float* amplitudes,
                                           int* activeCount,
                                           float fundamentalFreq)
{
    const int currentCount = *activeCount;

    // Reserve room for up to 3 sub-harmonics at the end of the arrays
    static constexpr int kMaxSubs = 3;

    float subFreqs[kMaxSubs];
    float subAmps[kMaxSubs];

    const int numSubs = generate(fundamentalFreq,
                                  subFreqs,
                                  subAmps,
                                  kMaxSubs);

    for (int i = 0; i < numSubs; ++i)
    {
        frequencies[currentCount + i] = subFreqs[i];
        amplitudes[currentCount + i]  = subAmps[i];
    }

    *activeCount = currentCount + numSubs;
}

} // namespace ana
