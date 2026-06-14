#include "SpectralMorpher.h"
#include "SIMDSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ana {

//==============================================================================
// Anonymous namespace: constants and internal helpers
//==============================================================================
namespace {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

/** Clamp value to [lo, hi]. */
inline float clamp(float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** Sanitize a float: replace NaN/Inf with zero. */
inline float sanitise(float v) noexcept
{
    return std::isfinite(v) ? v : 0.0f;
}

/** Compute the shortest angular difference between two phases, normalised
    to [-Pi, Pi]. */
inline float phaseDiff(float from, float to) noexcept
{
    return std::remainder(to - from, kTwoPi);
}

/** Wrap a phase angle to [-Pi, Pi]. */
inline float wrapPhase(float p) noexcept
{
    return std::remainder(p, kTwoPi);
}

/** Sanitise a full PartialDataSIMD: zero any NaN/Inf values across all
    three arrays. */
inline void sanitiseOutput(PartialDataSIMD& data) noexcept
{
    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        data.frequency[i] = sanitise(data.frequency[i]);
        data.amplitude[i] = sanitise(data.amplitude[i]);
        data.phase[i]     = sanitise(data.phase[i]);
    }
}

} // namespace

//==============================================================================
// morphLinear — A -> B SIMD-accelerated linear interpolation
//==============================================================================

void SpectralMorpher::morphLinear(PartialDataSIMD& output,
                                  const PartialDataSIMD& a,
                                  const PartialDataSIMD& b,
                                  const float t)
{
    const float tClamped = clamp(t, 0.0f, 1.0f);

    // All three arrays use SIMDKernels::vectorLerp
    SIMDKernels::vectorLerp(output.frequency,
                             a.frequency,
                             b.frequency,
                             tClamped,
                             PartialDataSIMD::kMaxPartials);

    SIMDKernels::vectorLerp(output.amplitude,
                             a.amplitude,
                             b.amplitude,
                             tClamped,
                             PartialDataSIMD::kMaxPartials);

    SIMDKernels::vectorLerp(output.phase,
                             a.phase,
                             b.phase,
                             tClamped,
                             PartialDataSIMD::kMaxPartials);

    // Metadata
    output.sampleRate  = a.sampleRate;
    output.hopSize     = a.hopSize;
    output.maxPartials = a.maxPartials;

    // NaN/Inf safety
    sanitiseOutput(output);

    output.updateActiveMask();
}

//==============================================================================
// morphWeighted — amplitude-weighted morph
//==============================================================================

void SpectralMorpher::morphWeighted(PartialDataSIMD& output,
                                    const PartialDataSIMD& a,
                                    const PartialDataSIMD& b,
                                    const float t)
{
    const float tClamped = clamp(t, 0.0f, 1.0f);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        // Effective morph factor: higher total amplitude -> faster transition
        const float ampA   = sanitise(a.amplitude[i]);
        const float ampB   = sanitise(b.amplitude[i]);
        const float avgAmp = (ampA + ampB) * 0.5f;
        const float tEff   = clamp(tClamped * avgAmp, 0.0f, 1.0f);

        output.frequency[i] = sanitise(
            a.frequency[i] + (b.frequency[i] - a.frequency[i]) * tEff);

        output.amplitude[i] = sanitise(
            ampA + (ampB - ampA) * tEff);

        output.phase[i] = sanitise(
            a.phase[i] + (b.phase[i] - a.phase[i]) * tEff);
    }

    // Metadata
    output.sampleRate  = a.sampleRate;
    output.hopSize     = a.hopSize;
    output.maxPartials = a.maxPartials;

    // Backstop NaN/Inf safety
    sanitiseOutput(output);

    output.updateActiveMask();
}

//==============================================================================
// morphMulti — weighted sum of N source spectra
//==============================================================================

void SpectralMorpher::morphMulti(PartialDataSIMD& output,
                                 const std::vector<PartialDataSIMD>& sources,
                                 const std::vector<float>& weights)
{
    const int numSources = static_cast<int>(
        std::min(sources.size(), weights.size()));

    // Handle degenerate cases
    if (numSources <= 0)
    {
        std::memset(output.frequency, 0, sizeof(output.frequency));
        std::memset(output.amplitude, 0, sizeof(output.amplitude));
        std::memset(output.phase, 0, sizeof(output.phase));
        output.sampleRate  = 44100.0;
        output.hopSize     = 512.0;
        output.maxPartials = PartialDataSIMD::kMaxPartials;
        output.updateActiveMask();
        return;
    }

    // Compute total weight; clamp to 1.0
    float totalWeight = 0.0f;
    for (int j = 0; j < numSources; ++j)
        totalWeight += sanitise(weights[static_cast<size_t>(j)]);

    const float weightNorm = totalWeight > 1.0f ? 1.0f / totalWeight : 1.0f;

    // Zero output
    std::memset(output.frequency, 0, sizeof(output.frequency));
    std::memset(output.amplitude, 0, sizeof(output.amplitude));
    std::memset(output.phase, 0, sizeof(output.phase));

    // Accumulate weighted sum per partial
    for (int j = 0; j < numSources; ++j)
    {
        const float w = sanitise(weights[static_cast<size_t>(j)]) * weightNorm;
        if (w < 1e-9f)
            continue;

        const auto& src = sources[static_cast<size_t>(j)];

        for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        {
            output.frequency[i] += sanitise(src.frequency[i]) * w;
            output.amplitude[i] += sanitise(src.amplitude[i]) * w;
            output.phase[i]     += sanitise(src.phase[i]) * w;
        }
    }

    // Wrap accumulated phase back to [-Pi, Pi]
    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        output.phase[i] = wrapPhase(output.phase[i]);

    // Metadata from the first source
    output.sampleRate  = sources[0].sampleRate;
    output.hopSize     = sources[0].hopSize;
    output.maxPartials = sources[0].maxPartials;

    // Backstop NaN/Inf safety
    sanitiseOutput(output);

    output.updateActiveMask();
}

} // namespace ana
