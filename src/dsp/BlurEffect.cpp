#include "BlurEffect.h"
#include "SIMDSupport.h"
#include <cstring>

namespace ana {

// ============================================================================
// Construction / parameters
// ============================================================================

BlurEffect::BlurEffect()
{
    reset();
}

void BlurEffect::setAttackBlur(float ms)   { attackBlurMs_  = std::clamp(ms, 0.0f, 1000.0f); }
void BlurEffect::setDecayBlur(float ms)    { decayBlurMs_   = std::clamp(ms, 0.0f, 1000.0f); }
void BlurEffect::setHarmonicBlur(float amt) { harmonicBlur_  = std::clamp(amt, 0.0f, 1.0f); }
void BlurEffect::setTopTension(float t)    { topTension_     = std::clamp(t, 0.0f, 1.0f); }
void BlurEffect::setBottomTension(float t) { bottomTension_  = std::clamp(t, 0.0f, 1.0f); }
void BlurEffect::setMix(float m)           { mix_            = std::clamp(m, 0.0f, 1.0f); }
void BlurEffect::setSampleRate(double sr)  { sampleRate_     = sr; }

void BlurEffect::reset()
{
    std::memset(prevAmplitudes_, 0, sizeof(prevAmplitudes_));
}

// ============================================================================
// Temporal blur (horizontal – along the frame / time axis)
// In a real-time SIMD context without latency, we use an exponential moving
// average (IIR) for decay blur. Attack blur is minimized as it requires latency.
// ============================================================================
void BlurEffect::applyTemporalBlur(PartialDataSIMD& data)
{
    if (decayBlurMs_ <= 0.0f) return;

    // Cache alpha — only recompute when decayBlurMs_ changes
    if (decayBlurMs_ != lastDecayBlurMs_)
    {
        const double tau = decayBlurMs_ / 1000.0;
        const double dt = data.hopSize / sampleRate_;
        cachedAlpha_ = static_cast<float>(std::exp(-dt / tau));
        lastDecayBlurMs_ = decayBlurMs_;
    }
    const float alpha = cachedAlpha_;

    // Save old prevAmplitudes_ for rising-edge detection
    // (need old values since vectorLerp overwrites in-place)
    float oldPrev[PartialDataSIMD::kMaxPartials];
    std::memcpy(oldPrev, prevAmplitudes_, sizeof(oldPrev));

    // Vectorized exponential smoothing for ALL 512 partials.
    // For active:  prev = current*(1-alpha) + prev*alpha  (trailing decay)
    // For inactive: prev = 0*(1-alpha) + prev*alpha = prev*alpha (same as *= alpha)
    SIMDKernels::vectorLerp(prevAmplitudes_, scratch_workingAmps_, prevAmplitudes_,
                            alpha, PartialDataSIMD::kMaxPartials);

    // Handle rising amplitudes (revert lerp) and re-activation of decaying partials
    for (int p = 0; p < data.maxPartials; ++p)
    {
        if (data.isActive(p))
        {
            // Ensure we don't blur up (only trailing decay)
            if (scratch_workingAmps_[p] > oldPrev[p])
                prevAmplitudes_[p] = scratch_workingAmps_[p];

            scratch_workingAmps_[p] = prevAmplitudes_[p];
        }
        else if (oldPrev[p] > 1e-5f)
        {
            // prevAmplitudes_[p] was already decayed by the vectorLerp above.
            // If still audible, re-activate this partial.
            if (prevAmplitudes_[p] > 1e-4f)
            {
                data.activeMask[p >> 5] |= (1u << (p & 31));
                data.activeCount++;
                scratch_workingAmps_[p] = prevAmplitudes_[p];
                data.amplitude[p] = prevAmplitudes_[p];
            }
        }
    }
}

// ============================================================================
// Harmonic blur (vertical – along the partial / frequency axis)
// ============================================================================
void BlurEffect::applyHarmonicBlur(PartialDataSIMD& data)
{
    if (harmonicBlur_ <= 0.0f || data.activeCount <= 1) return;

    const int baseRadius = std::max(1, static_cast<int>(harmonicBlur_ * 8.0f));

    // Gather active partial indices
    int activeIndices[PartialDataSIMD::kMaxPartials];
    int n = 0;
    for (int p = 0; p < data.maxPartials; ++p) {
        if (data.isActive(p)) {
            activeIndices[n++] = p;
        }
    }

    // Temporary buffer to store blurred results to avoid cascading effects
    float tempAmps[PartialDataSIMD::kMaxPartials] = {0.0f};

    // Build prefix sum over the compacted active amplitude list
    // scratch_prefixSum_[0] = 0; scratch_prefixSum_[i+1] = scratch_prefixSum_[i] + amps[i]
    scratch_prefixSum_[0] = 0.0f;
    for (int i = 0; i < n; ++i)
        scratch_prefixSum_[i + 1] = scratch_prefixSum_[i] + scratch_workingAmps_[activeIndices[i]];

    // Apply box blur via running sum (O(n) regardless of radius)
    for (int i = 0; i < n; ++i)
    {
        const int p = activeIndices[i];
        const float normFreq = std::min(1.0f, data.frequency[p] / 20000.0f);

        const float tensionFactor = bottomTension_ * (1.0f - normFreq) + topTension_ * normFreq;
        int effectiveRadius = std::min(static_cast<int>(baseRadius * (1.0f + tensionFactor)), n - 1);

        if (effectiveRadius <= 0)
        {
            tempAmps[p] = scratch_workingAmps_[p];
            continue;
        }

        // Running-sum box blur over the compacted active list
        const int lo = std::max(0, i - effectiveRadius);
        const int hi = std::min(n - 1, i + effectiveRadius);
        const float sum = scratch_prefixSum_[hi + 1] - scratch_prefixSum_[lo];
        const int count = hi - lo + 1;
        tempAmps[p] = sum / static_cast<float>(count);
    }

    // Write back
    for (int i = 0; i < n; ++i)
    {
        const int p = activeIndices[i];
        scratch_workingAmps_[p] = tempAmps[p];
    }
}

// ============================================================================
// Main process entry point
// ============================================================================
void BlurEffect::process(PartialDataSIMD& data)
{
    if (mix_ <= 0.0f) 
    {
        // Must still update prevAmplitudes for continuity
        for (int p = 0; p < data.maxPartials; ++p)
            prevAmplitudes_[p] = data.isActive(p) ? data.amplitude[p] : 0.0f;
        return;
    }

    // --- Extract amplitudes ---
    for (int p = 0; p < data.maxPartials; ++p)
    {
        const float a = data.isActive(p) ? data.amplitude[p] : 0.0f;
        scratch_originalAmps_[p] = a;
        scratch_workingAmps_[p]  = a;
    }

    // --- Apply blur passes ---
    if (attackBlurMs_ > 0.0f || decayBlurMs_ > 0.0f)
        applyTemporalBlur(data);

    if (harmonicBlur_ > 0.0f)
        applyHarmonicBlur(data);

    // --- Wet / dry mix and write back ---
    for (int p = 0; p < data.maxPartials; ++p)
    {
        if (data.isActive(p))
        {
            const float dry = scratch_originalAmps_[p];
            const float wet = scratch_workingAmps_[p];
            data.amplitude[p] = dry + (wet - dry) * mix_;
        }
    }
}

} // namespace ana
