#include "PrismEffect.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

namespace ana {

//==============================================================================
PrismEffect::PrismEffect()
{
}

//==============================================================================
// Parameter setters
void PrismEffect::setAmount(float a)      { amount = clamp01(a); }
void PrismEffect::setFeedback(float f)    { feedback = clamp01(f); }
void PrismEffect::setMix(float m)         { mix = clamp01(m); }
void PrismEffect::setMode(PrismMode m)    { mode = m; }
void PrismEffect::setMaxShift(float hz)   { maxShift = std::max(0.0f, hz); }
void PrismEffect::setCenterFreq(float hz) { centerFreq = std::max(0.0f, hz); }

//==============================================================================
// Parameter getters
float PrismEffect::getAmount() const      { return amount; }
float PrismEffect::getFeedback() const    { return feedback; }
float PrismEffect::getMix() const         { return mix; }
PrismMode PrismEffect::getMode() const    { return mode; }
float PrismEffect::getMaxShift() const    { return maxShift; }
float PrismEffect::getCenterFreq() const  { return centerFreq; }

//==============================================================================
void PrismEffect::reset()
{
    hasFeedback_ = false;
}

//==============================================================================
float PrismEffect::clamp01(float v)
{
    return std::max(0.0f, std::min(1.0f, v));
}

bool PrismEffect::isValid(float v)
{
    return std::isfinite(v) && !std::isnan(v);
}

//==============================================================================
void PrismEffect::process(PartialDataSIMD& data)
{
    if (data.activeCount == 0)
        return;

    // Apply feedback: blend stored previous output into current input
    if (feedback > 0.0f && hasFeedback_)
        applyFeedback(data);

    // Only snapshot original if mix blending is needed (avoids unnecessary copy)
    PartialDataSIMD original;
    const bool needMix = (mix < 1.0f);

    if (needMix)
    {
        original = data;
    }

    // Apply the selected mode
    switch (mode)
    {
        case PrismMode::FrequencyShift:   applyFrequencyShift(data); break;
        case PrismMode::SpectralBlur:     applySpectralBlur(data);   break;
        case PrismMode::HarmonicRotation: applyHarmonicRotation(data); break;
        case PrismMode::SpectralMirror:   applySpectralMirror(data); break;
        case PrismMode::CombSweep:        applyCombSweep(data);      break;
    }

    // Store output for feedback on next call
    feedbackBuffer = data;
    hasFeedback_ = true;

    // Apply wet/dry mix
    if (needMix)
        applyMix(original, data);
}

//==============================================================================
// --- Feedback ---
void PrismEffect::applyFeedback(PartialDataSIMD& data)
{
    const float dryFB = 1.0f - feedback;

    for (int p = 0; p < data.maxPartials; ++p)
    {
        if (data.isActive(p) && feedbackBuffer.isActive(p))
        {
            data.frequency[p] = data.frequency[p] * dryFB + feedbackBuffer.frequency[p] * feedback;
            data.amplitude[p] = data.amplitude[p] * dryFB + feedbackBuffer.amplitude[p] * feedback;
            data.phase[p]     = data.phase[p]     * dryFB + feedbackBuffer.phase[p]     * feedback;
        }
    }
}

//==============================================================================
// --- Mix ---
void PrismEffect::applyMix(const PartialDataSIMD& original, PartialDataSIMD& processed)
{
    const float dryMix = 1.0f - mix;

    for (int p = 0; p < processed.maxPartials; ++p)
    {
        if (processed.isActive(p) && original.isActive(p))
        {
            processed.frequency[p] = original.frequency[p] * dryMix + processed.frequency[p] * mix;
            processed.amplitude[p] = original.amplitude[p] * dryMix + processed.amplitude[p] * mix;
            processed.phase[p]     = original.phase[p]     * dryMix + processed.phase[p]     * mix;
        }
    }
}

//==============================================================================
// --- FrequencyShift ---
//
// Shift all partial frequencies by amount * maxShift. Positive offset moves
// frequencies upward; negative would move downward (amount is always 0..1 so
// shift is always positive in this implementation).
//
void PrismEffect::applyFrequencyShift(PartialDataSIMD& data)
{
    const float shift = amount * maxShift;

    for (int p = 0; p < data.maxPartials; ++p)
    {
        if (data.isActive(p))
        {
            data.frequency[p] += shift;
            if (data.frequency[p] < 0.0f)
                data.frequency[p] = 0.0f;
        }
    }
}

//==============================================================================
// --- SpectralBlur ---
//
// Apply a 3-point moving-average blur to amplitudes across neighboring
// partials. At the frame boundaries, use a 2-point average. The amount
// parameter blends between the original and fully-blurred amplitudes.
//
void PrismEffect::applySpectralBlur(PartialDataSIMD& data)
{
    if (data.activeCount < 2) return;

    int activeIndices[PartialDataSIMD::kMaxPartials];
    int n = 0;
    for (int p = 0; p < data.maxPartials; ++p) {
        if (data.isActive(p)) {
            activeIndices[n++] = p;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        if (i == 0)
        {
            scratchAmps_[i] = (data.amplitude[activeIndices[0]] + data.amplitude[activeIndices[1]]) * 0.5f;
        }
        else if (i == n - 1)
        {
            scratchAmps_[i] = (data.amplitude[activeIndices[n - 2]] + data.amplitude[activeIndices[n - 1]]) * 0.5f;
        }
        else
        {
            scratchAmps_[i] = (data.amplitude[activeIndices[i - 1]] +
                               data.amplitude[activeIndices[i]] +
                               data.amplitude[activeIndices[i + 1]]) / 3.0f;
        }
    }

    const float dryAmount = 1.0f - amount;
    for (int i = 0; i < n; ++i)
    {
        int p = activeIndices[i];
        float origAmp = data.amplitude[p];
        data.amplitude[p] = origAmp * dryAmount + scratchAmps_[i] * amount;
    }
}

//==============================================================================
// --- HarmonicRotation ---
//
// Sort partials in each frame by frequency (ascending), then rotate the
// partial assignments so that each partial takes on the characteristics of
// its neighbor. The Nth partial moves to position N+1 (wrapping around).
// Amount blends between original and rotated data.
//
void PrismEffect::applyHarmonicRotation(PartialDataSIMD& data)
{
    if (data.activeCount < 2) return;

    int activeIndices[PartialDataSIMD::kMaxPartials];
    int n = 0;
    for (int p = 0; p < data.maxPartials; ++p) {
        if (data.isActive(p)) {
            activeIndices[n++] = p;
        }
    }

    // Sort by frequency
    std::sort(activeIndices, activeIndices + n, [&data](int a, int b) {
        return data.frequency[a] < data.frequency[b];
    });

    // Scratch buffers to hold original values
    float origFreq[PartialDataSIMD::kMaxPartials];
    float origAmp[PartialDataSIMD::kMaxPartials];
    float origPhase[PartialDataSIMD::kMaxPartials];

    for (int i = 0; i < n; ++i) {
        int p = activeIndices[i];
        origFreq[i] = data.frequency[p];
        origAmp[i]  = data.amplitude[p];
        origPhase[i]= data.phase[p];
    }

    const float dryAmount = 1.0f - amount;
    for (int i = 0; i < n; ++i)
    {
        const int srcIdx = (i + 1) % n;
        int p = activeIndices[i];

        // Assign and blend
        data.frequency[p] = origFreq[i] * dryAmount + origFreq[srcIdx] * amount;
        data.amplitude[p] = origAmp[i]  * dryAmount + origAmp[srcIdx]  * amount;
        data.phase[p]     = origPhase[i]* dryAmount + origPhase[srcIdx]* amount;
    }
}

//==============================================================================
// --- SpectralMirror ---
//
// Mirror the frequency of each partial around centerFreq:
//   f' = centerFreq + (centerFreq - f) = 2*centerFreq - f
//
// The amount parameter blends linearly:
//   f' = lerp(f, 2*centerFreq - f, amount)
//
void PrismEffect::applySpectralMirror(PartialDataSIMD& data)
{
    for (int p = 0; p < data.maxPartials; ++p)
    {
        if (data.isActive(p))
        {
            float mirroredFreq = 2.0f * centerFreq - data.frequency[p];
            data.frequency[p] = data.frequency[p] * (1.0f - amount) + mirroredFreq * amount;

            if (data.frequency[p] < 0.0f)
                data.frequency[p] = 0.0f;
        }
    }
}

//==============================================================================
// --- CombSweep ---
//
// Apply a comb-filter amplitude pattern where notch spacing sweeps from
// high (minimal effect) to low (maximum combing) as amount increases.
//
//   sweepFreq = lerp(kCombSweepMax, kCombSweepMin, amount)
//   amplitude *= 0.5 + 0.5 * cos(2*pi * frequency / sweepFreq)
//
// This creates gentle notches at intervals of sweepFreq Hz.
//
void PrismEffect::applyCombSweep(PartialDataSIMD& data)
{
    const float sweepFreq = kCombSweepMax * (1.0f - amount) + kCombSweepMin * amount;
    const float twoPi = 2.0f * juce::MathConstants<float>::pi;

    for (int p = 0; p < data.maxPartials; ++p)
    {
        if (data.isActive(p))
        {
            if (data.frequency[p] < 1.0f || sweepFreq < 1.0f)
                continue;

            float comb = 0.5f + 0.5f * std::cos(twoPi * data.frequency[p] / sweepFreq);
            data.amplitude[p] *= comb;
        }
    }
}

} // namespace ana
