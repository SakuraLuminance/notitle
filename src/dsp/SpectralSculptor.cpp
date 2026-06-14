#include "SpectralSculptor.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

namespace ana {

//==============================================================================
//  Internal helpers
//==============================================================================
namespace {

/** Map a normalised position [0, 1] to a frequency in Hz [20, Nyquist]. */
inline float normToFreq(float norm, float sampleRate) noexcept
{
    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const float minFreq = 20.0f;
    return minFreq + norm * (nyquist - minFreq);
}

/** Map a normalised position [0, 1] to a zero-based partial index. */
inline int normToIndex(float norm, int maxIndex) noexcept
{
    const int idx = static_cast<int>(norm * static_cast<float>(maxIndex));
    return juce::jlimit(0, maxIndex - 1, idx);
}

/** Reciprocal of sigma scaling — makes brush size feel natural. */
constexpr float kSigmaScale = 0.15f;
constexpr float kMinSigma   = 0.5f;

/** Minimum amplitude threshold for partials to be considered active. */
constexpr float kAmpThreshold = 1e-8f;

} // namespace

//==============================================================================
//  Construction / reset
//==============================================================================

SpectralSculptor::SpectralSculptor()
{
    reset();
}

void SpectralSculptor::reset()
{
    activeTool_        = Tool::Brush;
    brushSize_         = 0.1f;
    brushStrength_     = 0.5f;
    position_          = 0.5f;
    centerFrequency_   = 1000.0f;
    sourcePosition_    = 0.0f;
    warpFactor_        = 1.0f;
    warpCenter_        = 0.5f;
    fractalIterations_ = 3;
    fractalSeed_       = 0.5f;
    smoothingFactor_   = 0.1f;

    cloneBuffer_            = PartialDataSIMD();
    cloneNeedsCapture_      = false;
    prevGains_.clear();
}

//==============================================================================
//  Setters
//==============================================================================

void SpectralSculptor::setActiveTool(Tool tool) { activeTool_ = tool; }

void SpectralSculptor::setBrushSize(float normalizedSize)
{
    brushSize_ = juce::jlimit(0.01f, 1.0f, normalizedSize);
}

void SpectralSculptor::setBrushStrength(float strength)
{
    brushStrength_ = juce::jlimit(0.0f, 1.0f, strength);
}

void SpectralSculptor::setPosition(float normalizedPos)
{
    position_ = juce::jlimit(0.0f, 1.0f, normalizedPos);
}

void SpectralSculptor::setCenterFrequency(float freqHz)
{
    centerFrequency_ = juce::jlimit(20.0f, 20000.0f, freqHz);
}

void SpectralSculptor::setSourcePosition(float normPos)
{
    sourcePosition_    = juce::jlimit(0.0f, 1.0f, normPos);
    cloneNeedsCapture_ = true;
}

void SpectralSculptor::cloneToPosition(float normPos)
{
    position_          = juce::jlimit(0.0f, 1.0f, normPos);
    cloneNeedsCapture_ = true;
}

void SpectralSculptor::setWarpFactor(float factor)
{
    warpFactor_ = juce::jlimit(0.25f, 4.0f, factor);
}

void SpectralSculptor::setWarpCenter(float normPos)
{
    warpCenter_ = juce::jlimit(0.0f, 1.0f, normPos);
}

void SpectralSculptor::setFractalIterations(int iterations)
{
    fractalIterations_ = juce::jlimit(1, 5, iterations);
}

void SpectralSculptor::setFractalSeed(float seed)
{
    fractalSeed_ = seed;
}

//==============================================================================
//  Gaussian falloff
//==============================================================================

float SpectralSculptor::gaussianFalloff(int idx, int center, float sigma) const noexcept
{
    const float diff = static_cast<float>(idx - center);
    return std::exp(-0.5f * diff * diff / (sigma * sigma));
}

//==============================================================================
//  Frequency sort
//==============================================================================

void SpectralSculptor::sortPartialsByFrequency(const PartialDataSIMD& partials,
                                                std::vector<int>& order) const
{
    const int N = partials.maxPartials;
    order.clear();
    order.reserve(N);

    for (int i = 0; i < N; ++i)
        if (partials.isActive(i) && partials.amplitude[i] > kAmpThreshold)
            order.push_back(i);

    std::sort(order.begin(), order.end(),
              [&](int a, int b) noexcept
              {
                  return partials.frequency[a] < partials.frequency[b];
              });
}

//==============================================================================
//  Clone helpers
//==============================================================================

void SpectralSculptor::captureCloneSource(const PartialDataSIMD& partials)
{
    const int N = partials.maxPartials;
    std::memset(cloneBuffer_.amplitude, 0, sizeof(cloneBuffer_.amplitude));
    std::memset(cloneBuffer_.frequency, 0, sizeof(cloneBuffer_.frequency));
    std::memset(cloneBuffer_.phase,     0, sizeof(cloneBuffer_.phase));

    const int srcCenter = normToIndex(sourcePosition_, N);
    const int radius    = juce::jlimit(1, N / 2 - 1,
                                       static_cast<int>(brushSize_ * static_cast<float>(N) * 0.5f));

    for (int offset = -radius; offset <= radius; ++offset)
    {
        const int idx = srcCenter + offset;
        if (idx < 0 || idx >= N) continue;

        cloneBuffer_.amplitude[idx] = partials.amplitude[idx];
        cloneBuffer_.frequency[idx] = partials.frequency[idx];
        cloneBuffer_.phase[idx]     = partials.phase[idx];
    }
}

void SpectralSculptor::pasteCloneSource(PartialDataSIMD& partials) const
{
    const int N        = partials.maxPartials;
    const int dstCenter = normToIndex(position_, N);
    const int radius    = juce::jlimit(1, N / 2 - 1,
                                       static_cast<int>(brushSize_ * static_cast<float>(N) * 0.5f));

    for (int offset = -radius; offset <= radius; ++offset)
    {
        const int dstIdx = dstCenter + offset;
        if (dstIdx < 0 || dstIdx >= N) continue;

        // Only overwrite if the clone buffer has valid data at this offset
        // (the source offset may have been out of range during capture)
        const int srcIdx = normToIndex(sourcePosition_, N) + offset;
        if (srcIdx < 0 || srcIdx >= N) continue;

        partials.amplitude[dstIdx] = cloneBuffer_.amplitude[srcIdx];
        partials.frequency[dstIdx] = cloneBuffer_.frequency[srcIdx];
        partials.phase[dstIdx]     = cloneBuffer_.phase[srcIdx];
    }
}

//==============================================================================
//  Main processing
//==============================================================================

void SpectralSculptor::process(PartialDataSIMD& partials)
{
    if (partials.activeCount == 0 || partials.maxPartials <= 0)
        return;

    // Save a snapshot of the incoming amplitudes for smoothing
    const int N = partials.maxPartials;
    float inputAmps[512];
    std::memcpy(inputAmps, partials.amplitude, sizeof(float) * N);

    // Apply the active tool
    switch (activeTool_)
    {
        case Tool::Brush:   applyBrush(partials);   break;
        case Tool::Eraser:  applyEraser(partials);  break;
        case Tool::Smudge:  applySmudge(partials);  break;
        case Tool::Sharpen: applySharpen(partials); break;
        case Tool::Warp:    applyWarp(partials);    break;
        case Tool::Clone:   applyClone(partials);   break;
        case Tool::Mirror:  applyMirror(partials);  break;
        case Tool::Fractal: applyFractal(partials); break;
    }

    // Build gain-modulation vector: what factor did the tool apply?
    float gainMod[512];
    std::fill(gainMod, gainMod + N, 1.0f);
    for (int i = 0; i < N; ++i)
    {
        const float in = inputAmps[i];
        const float out = partials.amplitude[i];
        if (in > kAmpThreshold)
        {
            const float ratio = out / in;
            gainMod[i] = juce::jlimit(0.0f, 10.0f, ratio);
        }
        // else gainMod[i] stays 1.0f (no modulation on silent bins)
    }

    // Smooth gain modulations across frames (avoids zipper noise)
    if (static_cast<int>(prevGains_.size()) == N)
    {
        for (int i = 0; i < N; ++i)
        {
            gainMod[i] = prevGains_[i] * smoothingFactor_
                       + gainMod[i] * (1.0f - smoothingFactor_);
        }
    }

    // Apply smoothed gain to the original input amplitudes
    for (int i = 0; i < N; ++i)
    {
        partials.amplitude[i] = inputAmps[i] * gainMod[i];
        partials.amplitude[i] = juce::jmax(0.0f, partials.amplitude[i]);
    }

    // Persist gain-mod for next frame
    prevGains_.assign(gainMod, gainMod + N);

    // Update the bitmask-based active tracking
    partials.updateActiveMask();
}

//==============================================================================
//  Brush
//==============================================================================
//  Multiply amplitude by (1 + strength * gaussian falloff).
//  Positive strength → boost;  negative strength → attenuate.
//  (brushStrength_ is always ≥0; the tool selection provides the sign.)

void SpectralSculptor::applyBrush(PartialDataSIMD& partials)
{
    const int N         = partials.maxPartials;
    const int centerIdx = normToIndex(position_, N);
    const float sigma   = brushSize_ * static_cast<float>(N) * kSigmaScale + kMinSigma;

    // Brute-force iteration over all 512 slots is fine at audio rates;
    // the active-mask check quickly skips dead slots.
    for (int i = 0; i < N; ++i)
    {
        if (! partials.isActive(i))
            continue;

        const float falloff = gaussianFalloff(i, centerIdx, sigma);
        const float boost   = 1.0f + brushStrength_ * falloff;
        partials.amplitude[i] *= boost;
    }
}

//==============================================================================
//  Eraser
//==============================================================================
//  Similar to brush but always attenuates:
//    amplitude *= (1 - strength * gaussian falloff)
//  At max strength the amplitude approaches zero.

void SpectralSculptor::applyEraser(PartialDataSIMD& partials)
{
    const int N         = partials.maxPartials;
    const int centerIdx = normToIndex(position_, N);
    const float sigma   = brushSize_ * static_cast<float>(N) * kSigmaScale + kMinSigma;

    for (int i = 0; i < N; ++i)
    {
        if (! partials.isActive(i))
            continue;

        const float falloff = gaussianFalloff(i, centerIdx, sigma);
        const float atten   = 1.0f - brushStrength_ * falloff;
        partials.amplitude[i] *= juce::jmax(0.0f, atten);
    }
}

//==============================================================================
//  Smudge
//==============================================================================
//  Low-pass filter across frequency-sorted partial indices.
//  Multiple passes (controlled by brush strength) for stronger effect.

void SpectralSculptor::applySmudge(PartialDataSIMD& partials)
{
    const int N = partials.maxPartials;

    std::vector<int> order;
    sortPartialsByFrequency(partials, order);

    const int numActive = static_cast<int>(order.size());
    if (numActive < 3)
        return; // need at least two neighbours

    // Number of passes scales with strength
    const int passes = juce::jmax(1, static_cast<int>(brushStrength_ * 5.0f + 0.5f));
    const float blend = 0.3f; // blend per pass

    // Scratch buffer for one pass
    std::vector<float> scratch(static_cast<size_t>(numActive));

    for (int pass = 0; pass < passes; ++pass)
    {
        for (int i = 0; i < numActive; ++i)
        {
            const int idx = order[i];
            float neighborSum = partials.amplitude[idx];
            int   neighborCnt = 1;

            if (i > 0)
            {
                neighborSum += partials.amplitude[order[i - 1]];
                ++neighborCnt;
            }
            if (i < numActive - 1)
            {
                neighborSum += partials.amplitude[order[i + 1]];
                ++neighborCnt;
            }

            const float avg = neighborSum / static_cast<float>(neighborCnt);
            scratch[i] = partials.amplitude[idx] * (1.0f - blend) + avg * blend;
        }

        // Write back
        for (int i = 0; i < numActive; ++i)
            partials.amplitude[order[i]] = scratch[static_cast<size_t>(i)];
    }
}

//==============================================================================
//  Sharpen
//==============================================================================
//  High-pass across frequency-sorted partial indices:
//    amp[i] += enhance * (amp[i] - avg(neighbours))
//  Increases contrast between peaks and valleys.

void SpectralSculptor::applySharpen(PartialDataSIMD& partials)
{
    const int N = partials.maxPartials;

    std::vector<int> order;
    sortPartialsByFrequency(partials, order);

    const int numActive = static_cast<int>(order.size());
    if (numActive < 3)
        return;

    const float enhance = brushStrength_ * 2.0f; // [0, 2] range

    std::vector<float> scratch(static_cast<size_t>(numActive));
    for (int i = 0; i < numActive; ++i)
        scratch[static_cast<size_t>(i)] = partials.amplitude[order[i]];

    for (int i = 0; i < numActive; ++i)
    {
        int   cnt = 0;
        float sum = 0.0f;

        if (i > 0)              { sum += scratch[static_cast<size_t>(i - 1)]; ++cnt; }
        if (i < numActive - 1)  { sum += scratch[static_cast<size_t>(i + 1)]; ++cnt; }

        if (cnt == 0) continue;

        const float avg   = sum / static_cast<float>(cnt);
        const float diff  = scratch[static_cast<size_t>(i)] - avg;
        const float added = diff * enhance;

        scratch[static_cast<size_t>(i)] += added;
        scratch[static_cast<size_t>(i)]  = juce::jmax(0.0f, scratch[static_cast<size_t>(i)]);
    }

    // Write back
    for (int i = 0; i < numActive; ++i)
        partials.amplitude[order[i]] = scratch[static_cast<size_t>(i)];
}

//==============================================================================
//  Warp
//==============================================================================
//  Map partial frequencies through a non-linear function centred at warpCenter:
//      f_out = f_center * (f_in / f_center) ^ warpFactor
//  Uses a scatter approach: each original partial's energy is redistributed
//  to the nearest partial index at the warped frequency.

void SpectralSculptor::applyWarp(PartialDataSIMD& partials)
{
    const int   N       = partials.maxPartials;
    const float nyquist = static_cast<float>(partials.sampleRate) * 0.5f;
    const float minFreq = 1.0f;

    // Convert normalised warp centre to Hz
    const float centerFreq = normToFreq(warpCenter_, static_cast<float>(partials.sampleRate));
    if (centerFreq < minFreq)
        return;

    // Collect frequency-sorted active partials
    std::vector<int> order;
    sortPartialsByFrequency(partials, order);
    const int numActive = static_cast<int>(order.size());
    if (numActive < 1)
        return;

    // ---- Scatter phase ----
    // For each active partial compute its warped frequency, find the closest
    // partial slot (by frequency) and accumulate amplitude there.
    std::vector<float> newAmp(static_cast<size_t>(N), 0.0f);
    std::vector<float> weight(static_cast<size_t>(N), 0.0f);

    for (int si = 0; si < numActive; ++si)
    {
        const int srcIdx = order[si];
        const float fIn  = partials.frequency[srcIdx];
        if (fIn < minFreq) continue;

        const float ratio  = fIn / centerFreq;
        float fOut = centerFreq * std::pow(ratio, warpFactor_);
        fOut = juce::jlimit(minFreq, nyquist * 0.999f, fOut);

        // Find the closest active partial by frequency
        int bestIdx = -1;
        float bestDist = 1e10f;
        for (int si2 = 0; si2 < numActive; ++si2)
        {
            const int ti = order[si2];
            const float d = std::abs(partials.frequency[ti] - fOut);
            if (d < bestDist)
            {
                bestDist = d;
                bestIdx  = ti;
            }
        }

        if (bestIdx >= 0)
        {
            newAmp[static_cast<size_t>(bestIdx)]  += partials.amplitude[srcIdx];
            weight[static_cast<size_t>(bestIdx)]  += 1.0f;
        }
    }

    // Average accumulated amplitudes
    for (int i = 0; i < N; ++i)
    {
        if (weight[static_cast<size_t>(i)] > 0.5f)
        {
            partials.amplitude[i] = newAmp[static_cast<size_t>(i)]
                                  / weight[static_cast<size_t>(i)];
        }
        else if (partials.isActive(i))
        {
            // No warped energy landed here – silence the slot
            partials.amplitude[i] = 0.0f;
        }
    }
}

//==============================================================================
//  Clone
//==============================================================================
//  Behaves like a Photoshop clone stamp:
//    - setSourcePosition() freezes a region of the spectrum into cloneBuffer_.
//    - applyClone pastes that frozen data at the current position_.
//    - cloneToPosition() is a one-shot convenience.

void SpectralSculptor::applyClone(PartialDataSIMD& partials)
{
    // Capture source data if position changed since last process call
    if (cloneNeedsCapture_)
    {
        captureCloneSource(partials);
        cloneNeedsCapture_ = false;
        // On the capture frame we still paste (current position)
    }

    pasteCloneSource(partials);
}

//==============================================================================
//  Mirror
//==============================================================================
//  Reflect the spectrum around centreFrequency_.
//    f_out = 2 * centre - f_in
//  Frequencies that would wrap around are folded back.

void SpectralSculptor::applyMirror(PartialDataSIMD& partials)
{
    const int   N       = partials.maxPartials;
    const float nyquist = static_cast<float>(partials.sampleRate) * 0.5f;
    const float center  = juce::jlimit(20.0f, nyquist * 0.99f, centerFrequency_);

    // Collect active partials with their reflected frequency target
    struct Partial { int srcIdx; float targetFreq; };
    std::vector<Partial> src;
    src.reserve(static_cast<size_t>(N));

    for (int i = 0; i < N; ++i)
    {
        if (! partials.isActive(i)) continue;
        if (partials.frequency[i] <= 0.0f) continue;

        float fOut = 2.0f * center - partials.frequency[i];

        // Fold back into [minFreq, nyquist]
        if (fOut < 0.0f)       fOut = -fOut;
        if (fOut > nyquist)    fOut = 2.0f * nyquist - fOut;
        fOut = juce::jlimit(20.0f, nyquist * 0.999f, fOut);

        src.push_back({ i, fOut });
    }

    if (src.empty()) return;

    // Get frequency-sorted active indices for nearest-neighbour lookup
    std::vector<int> order;
    sortPartialsByFrequency(partials, order);

    // Scatter reflected amplitudes
    std::vector<float> newAmp(static_cast<size_t>(N), 0.0f);
    std::vector<float> weight(static_cast<size_t>(N), 0.0f);

    for (const auto& p : src)
    {
        // Find closest active partial to the target frequency
        int bestIdx = -1;
        float bestDist = 1e10f;
        for (int ti : order)
        {
            const float d = std::abs(partials.frequency[ti] - p.targetFreq);
            if (d < bestDist)
            {
                bestDist = d;
                bestIdx  = ti;
            }
        }

        if (bestIdx >= 0)
        {
            newAmp[static_cast<size_t>(bestIdx)]  += partials.amplitude[p.srcIdx];
            weight[static_cast<size_t>(bestIdx)]  += 1.0f;
        }
    }

    // Average and write back
    for (int i = 0; i < N; ++i)
    {
        if (weight[static_cast<size_t>(i)] > 0.5f)
        {
            partials.amplitude[i] = newAmp[static_cast<size_t>(i)]
                                  / weight[static_cast<size_t>(i)];
        }
    }
}

//==============================================================================
//  Fractal
//==============================================================================
//  Apply a recursive self-similar pattern to partial amplitudes.
//  Uses Cantor-like subdivision of the frequency axis; each iteration
//  divides the domain and applies an attenuation/boost pattern.

void SpectralSculptor::applyFractal(PartialDataSIMD& partials)
{
    const int N = partials.maxPartials;

    std::vector<int> order;
    sortPartialsByFrequency(partials, order);
    const int numActive = static_cast<int>(order.size());
    if (numActive < 2) return;

    const float fMin = partials.frequency[order.front()];
    const float fMax = partials.frequency[order.back()];
    const float fRange = fMax - fMin;
    if (fRange < 1.0f) return;

    // Each iteration applies a scaled pattern with decreasing influence.
    // The seed controls the pattern character by offsetting the division phase.
    for (int iter = 0; iter < fractalIterations_; ++iter)
    {
        const float scale     = std::pow(3.0f, static_cast<float>(iter + 1));
        const float influence = std::pow(0.5f, static_cast<float>(iter));

        float minAmp = 1e10f;
        float maxAmp = -1e10f;

        for (int i = 0; i < numActive; ++i)
        {
            const int idx   = order[i];
            const float f_norm = (partials.frequency[idx] - fMin) / fRange; // [0, 1]

            // Determine which third of the subdivided cell we are in
            const float pos  = f_norm * scale + fractalSeed_;
            const int   seg  = static_cast<int>(pos) % 3;

            float modifier = 1.0f;

            if (seg == 1)
            {
                // Middle third — attenuate
                const float attenuation = 1.0f - fractalSeed_ * 0.4f;
                modifier = 1.0f - (1.0f - attenuation) * influence * brushStrength_;
            }
            else
            {
                // Outer thirds — boost
                const float boost = 1.0f + fractalSeed_ * 0.3f;
                modifier = 1.0f + (boost - 1.0f) * influence * brushStrength_;
            }

            partials.amplitude[idx] *= juce::jmax(0.0f, modifier);
            minAmp = juce::jmin(minAmp, partials.amplitude[idx]);
            maxAmp = juce::jmax(maxAmp, partials.amplitude[idx]);
        }
    }

    // Apply threshold floor after fractal to keep signal clean
    for (int i = 0; i < N; ++i)
        if (partials.amplitude[i] < kAmpThreshold)
            partials.amplitude[i] = 0.0f;
}

} // namespace ana
