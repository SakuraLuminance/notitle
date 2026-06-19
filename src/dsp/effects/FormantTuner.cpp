#include "FormantTuner.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ana {

//==============================================================================
// Internal helpers
//==============================================================================
namespace {

constexpr float kPi = 3.14159265358979323846f;

/** Clamp value to [lo, hi]. */
inline float clamp(float v, float lo, float hi) noexcept
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/** Linear interpolation. */
inline float lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/** Grow a vector to at least @p needed elements (never shrinks). */
template<typename T>
void resizeIfSmaller(std::vector<T>& v, size_t needed)
{
    if (v.size() < needed)
        v.resize(needed);
}

// Vowel formant data: F1-F5 frequencies (Hz) — same source as FormantFilterBank
static const float kVowelFreq[5][5] = {
    { 800.0f, 1200.0f, 2500.0f, 3500.0f, 4500.0f },   // A (father)
    { 400.0f, 2000.0f, 2800.0f, 3600.0f, 4200.0f },   // E (bed)
    { 300.0f, 2500.0f, 3200.0f, 3800.0f, 4500.0f },   // I (beet)
    { 400.0f,  800.0f, 2500.0f, 3000.0f, 4000.0f },   // O (boat)
    { 350.0f,  600.0f, 2500.0f, 3200.0f, 3800.0f }    // U (boot)
};

static const float kVowelBw[5][5] = {
    { 80.0f,  90.0f,  120.0f,  150.0f,  200.0f },
    { 60.0f, 100.0f,  120.0f,  150.0f,  180.0f },
    { 50.0f,  80.0f,  100.0f,  130.0f,  150.0f },
    { 70.0f,  80.0f,  100.0f,  130.0f,  180.0f },
    { 60.0f,  70.0f,  100.0f,  120.0f,  150.0f }
};

} // anonymous namespace

//==============================================================================
// Construction / destruction
//==============================================================================

FormantTuner::FormantTuner()
{
    fft_ = std::make_unique<juce::dsp::FFT>(kFftOrder);
}

//==============================================================================
// EffectBase interface
//==============================================================================

void FormantTuner::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate_  = spec.sampleRate;
    numChannels_ = static_cast<int>(spec.numChannels);

    const int maxBlock = static_cast<int>(spec.maximumBlockSize);

    // Allocate scratch buffers (never shrink — safe for worst-case)
    const size_t orderP1 = static_cast<size_t>(kLpcOrder) + 1;
    const size_t halfP1  = static_cast<size_t>(kFftSize / 2) + 1;

    resizeIfSmaller(scratchLpcCoeffs_,  orderP1);
    resizeIfSmaller(scratchLpcWarped_,  orderP1);
    resizeIfSmaller(scratchLpcVowel_,   orderP1);
    resizeIfSmaller(scratchResidual_,   static_cast<size_t>(maxBlock));

    resizeIfSmaller(scratchFftBuf_,     static_cast<size_t>(kFftSize));
    resizeIfSmaller(scratchLpcR_,       orderP1);
    resizeIfSmaller(scratchAprev_,      orderP1);
    resizeIfSmaller(scratchAcurr_,      orderP1);
    resizeIfSmaller(scratchPowSpec_,    halfP1);
    resizeIfSmaller(scratchPowWarped_,  halfP1);

    // Dry buffer for wet/dry blend
    dryBuffer_.setSize(numChannels_, maxBlock, false, false, true);
}

void FormantTuner::reset()
{
    // No stateful memory per-channel — LPC analysis is frame-based.
    // The dry buffer will be overwritten each process() call.
}

void FormantTuner::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    // Snapshot parameters once per block (message thread vs audio thread safety)
    const float mix            = mix_;
    const float gender         = gender_;
    const float formantShift   = formantShift_;
    const float formantSpread  = formantSpread_;
    const int   vowelTarget    = vowelTarget_;

    // Quick exit if mix is zero (no wet signal)
    if (mix <= 0.0f)
        return;

    // Snapshot dry input
    dryBuffer_.makeCopyOf(buffer, true);

    const int numCh = juce::jmin(numChannels_, buffer.getNumChannels());

    // Compute effective frequency-warp factor α
    //   α = 2^(-shift/12) * spread * genderScale
    //   genderScale: 0 (male) → 1.15 (compress → longer tract), 
    //                0.5 → 1.0 (neutral), 
    //                1 (female) → 0.85 (stretch → shorter tract)
    const float genderScale = 1.15f - gender * 0.30f;
    const float alpha = std::pow(2.0f, -formantShift / 12.0f)
                      * formantSpread
                      * genderScale;

    // Determine whether vowel morphing is active
    const bool useVowel = (vowelTarget >= 1 && vowelTarget <= kNumVowels);

    // Pre-compute vowel LPC coefficients once per process call
    // (same target vowel for all channels)
    float vowelGain = 1.0f;
    if (useVowel)
        vowelFormantToLpc(vowelTarget, kLpcOrder,
                          scratchLpcVowel_.data(), vowelGain);

    // Per-channel processing
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* in  = buffer.getReadPointer(ch);
        float*       out = buffer.getWritePointer(ch);

        //----------------------------------------------------------------------
        // 1. LPC analysis: autocorrelation + Levinson-Durbin
        //----------------------------------------------------------------------
        float* const a = scratchLpcCoeffs_.data();
        float gain = 1.0f;
        performLpcAnalysis(in, numSamples, kLpcOrder, a, gain);

        // Guard against NaN in autocorrelation or gain
        if (!std::isfinite(static_cast<double>(scratchLpcR_[0])) || !std::isfinite(gain))
        {
            for (int i = 0; i <= kLpcOrder; ++i)
                a[i] = (i == 0) ? 1.0f : 0.0f;
            gain = 1e-5f;
            continue;
        }

        // Safety: if analysis produced garbage, skip this channel
        if (!std::isfinite(a[0]) || a[0] == 0.0f)
            continue;

        //----------------------------------------------------------------------
        // 2. LPC coefficient warping / morphing
        //----------------------------------------------------------------------
        float* const aFinal = scratchLpcWarped_.data();

        if (useVowel)
        {
            // Morph between warped source-LPC and target vowel LPC
            const float* const aVowel = scratchLpcVowel_.data();

            // Warp the source LPC first
            warpLpcCoeffs(a, kLpcOrder, alpha, aFinal);

            // Then blend toward the vowel target
            // (morph amount is fixed — full morph toward vowel shape)
            for (int i = 0; i <= kLpcOrder; ++i)
                aFinal[i] = lerp(aFinal[i], aVowel[i], 0.8f);
        }
        else
        {
            // Pure warping (no vowel target)
            warpLpcCoeffs(a, kLpcOrder, alpha, aFinal);
        }

        //----------------------------------------------------------------------
        // 3. LPC inverse filter: residual e[n] = x[n] - Σ a[k]·x[n-k]
        //----------------------------------------------------------------------
        float* const residual = scratchResidual_.data();
        inverseLpcFilter(in, residual, numSamples, a, kLpcOrder);

        //----------------------------------------------------------------------
        // 4. LPC synthesis filter with warped coefficients:
        //    y[n] = e[n] + Σ a'[k]·y[n-k]
        //----------------------------------------------------------------------
        synthesisLpcFilter(residual, out, numSamples, aFinal, kLpcOrder);
    }

    //----------------------------------------------------------------------
    // 5. Dry/wet blend
    //----------------------------------------------------------------------
    if (mix < 1.0f)
    {
        const float dryMix = 1.0f - mix;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* dry = dryBuffer_.getReadPointer(ch);
            float*       wet = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] = dry[i] * dryMix + wet[i] * mix;
        }
    }
}

//==============================================================================
// LPC analysis: autocorrelation + Levinson-Durbin
//
// Based on the same algorithm as PitchCorrector::lpcAutocorrelation.
// Coefficients are stored with a[0] = 1.0.
//==============================================================================

void FormantTuner::performLpcAnalysis(const float* x, int n, int order,
                                      float* coeffs, float& gain)
{
    if (n <= order || order < 1)
    {
        for (int i = 0; i <= order; ++i)
            coeffs[i] = (i == 0) ? 1.0f : 0.0f;
        gain = 1.0f;
        return;
    }

    //------------------------------------------------------------------
    // Autocorrelation (double precision)
    //------------------------------------------------------------------
    double* const r = scratchLpcR_.data();
    std::memset(r, 0, static_cast<size_t>(order + 1) * sizeof(double));

    for (int k = 0; k <= order; ++k)
    {
        double sum = 0.0;
        for (int i = 0; i < n - k; ++i)
            sum += static_cast<double>(x[i]) * static_cast<double>(x[i + k]);
        r[static_cast<size_t>(k)] = sum;
    }

    // Guard against silence
    if (r[0] < 1e-10)
    {
        for (int i = 0; i <= order; ++i)
            coeffs[i] = (i == 0) ? 1.0f : 0.0f;
        gain = 1e-5f;
        return;
    }

    //------------------------------------------------------------------
    // Levinson-Durbin recursion
    //------------------------------------------------------------------
    float* const aPrev = scratchAprev_.data();
    float* const aCurr = scratchAcurr_.data();
    std::memset(aPrev, 0, static_cast<size_t>(order + 1) * sizeof(float));
    std::memset(aCurr, 0, static_cast<size_t>(order + 1) * sizeof(float));
    aPrev[0] = 1.0f;
    aCurr[0] = 1.0f;

    double err = r[0];

    for (int i = 1; i <= order; ++i)
    {
        // Reflection coefficient k_i
        double rc = r[static_cast<size_t>(i)];
        for (int j = 1; j < i; ++j)
            rc += static_cast<double>(aPrev[static_cast<size_t>(j)])
                * r[static_cast<size_t>(i - j)];
        rc /= -err;

        if (!std::isfinite(rc) || std::abs(rc) >= 1.0)
            break;

        aCurr[static_cast<size_t>(i)] = static_cast<float>(rc);

        // Update lower-order coefficients
        for (int j = 1; j < i; ++j)
            aCurr[static_cast<size_t>(j)] =
                aPrev[static_cast<size_t>(j)]
                + static_cast<float>(rc) * aPrev[static_cast<size_t>(i - j)];

        err *= (1.0 - rc * rc);

        if (err < 1e-10)
            break;

        // Copy for next iteration
        for (int j = 1; j <= i; ++j)
            aPrev[static_cast<size_t>(j)] = aCurr[static_cast<size_t>(j)];
    }

    // Copy result (aPrev[0] = 1.0f)
    std::memcpy(coeffs, aPrev,
                static_cast<size_t>(order + 1) * sizeof(float));
    gain = static_cast<float>(std::sqrt(err));
}

//==============================================================================
// Frequency-domain LPC warping
//
// Given original LPC coefficients a[0..order] (a[0]=1), warps the spectral
// envelope by factor α so that A_new(z) ≈ A(z^α).  The algorithm:
//   1.  FFT the LPC coefficients → A[k] (zero-padded to FFT size)
//   2.  Power spectrum P[k] = 1/|A[k]|²
//   3.  Warp: P_warped[k] = P[round(k·α)]  (α < 1 → stretch → formants up)
//   4.  Autocorrelation R[t] = (1/N) Σ P_warped[k]·cos(2πkt/N)
//   5.  Levinson-Durbin on R[0..order] → warped coefficients
//==============================================================================

void FormantTuner::warpLpcCoeffs(const float* srcCoeffs, int order,
                                 float alpha, float* dstCoeffs)
{
    // No warping needed (or very close to identity)
    if (std::abs(alpha - 1.0f) < 0.001f)
    {
        std::memcpy(dstCoeffs, srcCoeffs,
                    static_cast<size_t>(order + 1) * sizeof(float));
        return;
    }

    const int N     = kFftSize;
    const int halfN = N / 2;

    //------------------------------------------------------------------
    // 1. Zero-padded FFT of LPC coefficients
    //------------------------------------------------------------------
    float* const fftBuf = scratchFftBuf_.data();
    std::memset(fftBuf, 0, static_cast<size_t>(N) * sizeof(float));
    std::memcpy(fftBuf, srcCoeffs,
                static_cast<size_t>(order + 1) * sizeof(float));

    fft_->performRealOnlyForwardTransform(fftBuf);

    //------------------------------------------------------------------
    // 2. Power spectrum P[k] = 1 / |A[k]|²  for k = 0 .. N/2
    //------------------------------------------------------------------
    double* const P = scratchPowSpec_.data();

    // DC bin (k=0): real part in fftBuf[0], no imaginary component
    {
        const double re = static_cast<double>(fftBuf[0]);
        P[0] = 1.0 / (std::max)(1e-30, re * re);
    }

    // Bins 1 .. N/2-1: real in fftBuf[2k], imag in fftBuf[2k+1]
    for (int k = 1; k < halfN; ++k)
    {
        const double re = static_cast<double>(fftBuf[static_cast<size_t>(2 * k)]);
        const double im = static_cast<double>(fftBuf[static_cast<size_t>(2 * k + 1)]);
        P[static_cast<size_t>(k)] = 1.0 / (std::max)(1e-30, re * re + im * im);
    }

    // Nyquist bin (k=N/2): real part in fftBuf[1]
    {
        const double re = static_cast<double>(fftBuf[1]);
        P[static_cast<size_t>(halfN)] = 1.0 / (std::max)(1e-30, re * re);
    }

    //------------------------------------------------------------------
    // 3. Warp frequency axis: P_warped[k] = P[round(k · α)]
    //
    //    α = 2^(-shift/12) * spread * genderScale
    //      < 1  →  stretch (formants move to higher frequencies)
    //      > 1  →  compress (formants move to lower frequencies)
    //------------------------------------------------------------------
    double* const Pw = scratchPowWarped_.data();

    for (int k = 0; k <= halfN; ++k)
    {
        const double srcIdx = static_cast<double>(k) * static_cast<double>(alpha);
        const int srcInt = static_cast<int>(srcIdx);
        const double frac = srcIdx - static_cast<double>(srcInt);

        if (srcInt >= halfN)
        {
            // Beyond Nyquist — hold last value
            Pw[static_cast<size_t>(k)] = P[static_cast<size_t>(halfN)];
        }
        else if (srcInt + 1 > halfN)
        {
            Pw[static_cast<size_t>(k)] = P[static_cast<size_t>(srcInt)];
        }
        else
        {
            // Linear interpolation between adjacent bins
            const double p0 = P[static_cast<size_t>(srcInt)];
            const double p1 = P[static_cast<size_t>(srcInt + 1)];
            Pw[static_cast<size_t>(k)] = p0 + (p1 - p0) * frac;
        }
    }

    //------------------------------------------------------------------
    // 4. Autocorrelation from warped power spectrum
    //
    //    R[t] = (1/N) · (Pw[0] + Pw[N/2]·cos(πt)
    //            + 2·Σ_{k=1}^{N/2-1} Pw[k]·cos(2πkt/N))
    //------------------------------------------------------------------
    double* const R = scratchLpcR_.data();
    const double invN = 1.0 / static_cast<double>(N);

    for (int t = 0; t <= order; ++t)
    {
        double sum = Pw[0]
                   + Pw[static_cast<size_t>(halfN)]
                   * std::cos(juce::MathConstants<double>::pi * t);

        for (int k = 1; k < halfN; ++k)
        {
            const double angle = 2.0 * juce::MathConstants<double>::pi
                               * static_cast<double>(k) * static_cast<double>(t)
                               * invN;
            sum += 2.0 * Pw[static_cast<size_t>(k)] * std::cos(angle);
        }

        R[static_cast<size_t>(t)] = sum * invN;
    }

    //------------------------------------------------------------------
    // 5. Levinson-Durbin on R[0..order] → warped LPC coefficients
    //------------------------------------------------------------------
    // Guard against degenerate autocorrelation
    if (R[0] < 1e-30)
    {
        std::memcpy(dstCoeffs, srcCoeffs,
                    static_cast<size_t>(order + 1) * sizeof(float));
        return;
    }

    float* const aPrev = scratchAprev_.data();
    float* const aCurr = scratchAcurr_.data();
    std::memset(aPrev, 0, static_cast<size_t>(order + 1) * sizeof(float));
    std::memset(aCurr, 0, static_cast<size_t>(order + 1) * sizeof(float));
    aPrev[0] = 1.0f;
    aCurr[0] = 1.0f;

    double err = R[0];

    for (int i = 1; i <= order; ++i)
    {
        double rc = R[static_cast<size_t>(i)];
        for (int j = 1; j < i; ++j)
            rc += static_cast<double>(aPrev[static_cast<size_t>(j)])
                * R[static_cast<size_t>(i - j)];
        rc /= -err;

        if (!std::isfinite(rc) || std::abs(rc) >= 1.0)
            break;

        aCurr[static_cast<size_t>(i)] = static_cast<float>(rc);

        for (int j = 1; j < i; ++j)
            aCurr[static_cast<size_t>(j)] =
                aPrev[static_cast<size_t>(j)]
                + static_cast<float>(rc) * aPrev[static_cast<size_t>(i - j)];

        err *= (1.0 - rc * rc);

        if (err < 1e-10)
            break;

        for (int j = 1; j <= i; ++j)
            aPrev[static_cast<size_t>(j)] = aCurr[static_cast<size_t>(j)];
    }

    std::memcpy(dstCoeffs, aPrev,
                static_cast<size_t>(order + 1) * sizeof(float));
}

//==============================================================================
// Vowel formant → LPC coefficients
//
// Converts the 5-formant frequency/bandwidth data for a target vowel into
// LPC coefficients.  Each formant contributes a complex-conjugate pole pair:
//
//     H_i(z) = 1 / (1 - 2·r_i·cos(θ_i)·z⁻¹ + r_i²·z⁻²)
//
// where r_i = exp(-π·bw_i/fs) and θ_i = 2π·f_i/fs.
//
// The overall polynomial A(z) = Π H_i(z)⁻¹ is obtained by convolving the
// second-order sections in sequence.  Remaining coefficients (beyond order
// 2·kNumVowels = 10) are zeroed — poles at the origin contribute nothing.
//==============================================================================

void FormantTuner::vowelFormantToLpc(int vowelIdx, int order,
                                     float* coeffs, float& gain)
{
    const int v = vowelIdx - 1;  // convert 1-based enum to 0-based index
    if (v < 0 || v >= kNumVowels)
    {
        // Invalid — return flat (all-pass) coefficients
        for (int i = 0; i <= order; ++i)
            coeffs[i] = (i == 0) ? 1.0f : 0.0f;
        gain = 1.0f;
        return;
    }

    // Start with A(z) = 1 (order 0)
    std::memset(coeffs, 0, static_cast<size_t>(order + 1) * sizeof(float));
    coeffs[0] = 1.0f;
    int currentOrder = 0;

    for (int f = 0; f < kNumVowels; ++f)
    {
        const float freq = kVowelFreq[v][f];
        const float bw   = kVowelBw[v][f];

        // Pole radius and angle from formant frequency & bandwidth
        const double r = std::exp(-kPi * static_cast<double>(bw)
                                        / sampleRate_);
        const double theta = 2.0 * kPi * static_cast<double>(freq)
                                   / sampleRate_;

        // Second-order denominator: 1 + b1·z⁻¹ + b2·z⁻²
        // b1 = -2·r·cos(θ),  b2 = r²
        const double b1 = -2.0 * r * std::cos(theta);
        const double b2 = r * r;

        // Convolve current polynomial with this second-order section
        // A_new[k] = A[k] + A[k-1]·b1 + A[k-2]·b2
        const int newOrder = std::min(currentOrder + 2, order);

        // Work backwards to avoid overwriting
        for (int k = newOrder; k >= 0; --k)
        {
            double ak = static_cast<double>(coeffs[static_cast<size_t>(k)]);
            if (k >= 1 && k - 1 <= currentOrder)
                ak += static_cast<double>(coeffs[static_cast<size_t>(k - 1)]) * b1;
            if (k >= 2 && k - 2 <= currentOrder)
                ak += static_cast<double>(coeffs[static_cast<size_t>(k - 2)]) * b2;
            coeffs[static_cast<size_t>(k)] = static_cast<float>(ak);
        }

        currentOrder = newOrder;
    }

    // Zero out remaining coefficients (order > 2·kNumVowels)
    for (int i = currentOrder + 1; i <= order; ++i)
        coeffs[static_cast<size_t>(i)] = 0.0f;

    gain = 1.0f;
}

//==============================================================================
// LPC inverse filter  —  analysis
//
//   e[n] = x[n] - Σ_{k=1}^{order} a[k] · x[n-k]
//
// Uses a ring-buffer-like access pattern via direct memory indexing.
//==============================================================================

void FormantTuner::inverseLpcFilter(const float* x, float* residual,
                                    int n, const float* coeffs, int order)
{
    // Direct-form FIR-like inverse filter
    // For the first `order` samples, the history implicitly contains zeros
    // (assumes signal starts from rest).
    for (int i = 0; i < n; ++i)
    {
        double sum = 0.0;
        for (int k = 1; k <= order && k <= i; ++k)
            sum += static_cast<double>(coeffs[static_cast<size_t>(k)])
                 * static_cast<double>(x[static_cast<size_t>(i - k)]);

        residual[static_cast<size_t>(i)] =
            static_cast<float>(static_cast<double>(x[static_cast<size_t>(i)]) - sum);
    }
}

//==============================================================================
// LPC synthesis filter  —  all-pole reconstruction
//
//   y[n] = residual[n] + Σ_{k=1}^{order} a'[k] · y[n-k]
//
// Writes result directly into @p output.
//==============================================================================

void FormantTuner::synthesisLpcFilter(const float* residual, float* output,
                                      int n, const float* coeffs, int order)
{
    for (int i = 0; i < n; ++i)
    {
        double sum = static_cast<double>(residual[static_cast<size_t>(i)]);
        for (int k = 1; k <= order && k <= i; ++k)
            sum += static_cast<double>(coeffs[static_cast<size_t>(k)])
                 * static_cast<double>(output[static_cast<size_t>(i - k)]);

        output[static_cast<size_t>(i)] = static_cast<float>(sum);
    }
}

//==============================================================================
// Setters
//==============================================================================

void FormantTuner::setFormantShift(float semitones)
{
    formantShift_ = clamp(semitones, -12.0f, 12.0f);
}

void FormantTuner::setFormantSpread(float factor)
{
    formantSpread_ = clamp(factor, 0.5f, 2.0f);
}

void FormantTuner::setGender(float amount)
{
    gender_ = clamp(amount, 0.0f, 1.0f);
}

void FormantTuner::setVowelTarget(int idx)
{
    vowelTarget_ = juce::jlimit(0, kNumVowels, idx);
}

void FormantTuner::setMix(float wet)
{
    mix_ = clamp(wet, 0.0f, 1.0f);
}

//==============================================================================
// State persistence
//==============================================================================

juce::ValueTree FormantTuner::getState() const
{
    juce::ValueTree tree("FormantTuner");
    tree.setProperty("formantShift",  formantShift_,  nullptr);
    tree.setProperty("formantSpread", formantSpread_, nullptr);
    tree.setProperty("gender",        gender_,        nullptr);
    tree.setProperty("vowelTarget",   vowelTarget_,   nullptr);
    tree.setProperty("mix",           mix_,           nullptr);
    return tree;
}

void FormantTuner::setState(const juce::ValueTree& state)
{
    setFormantShift(state.getProperty("formantShift", 0.0f));
    setFormantSpread(state.getProperty("formantSpread", 1.0f));
    setGender(state.getProperty("gender", 0.5f));
    setVowelTarget(static_cast<int>(state.getProperty("vowelTarget", 0)));
    setMix(state.getProperty("mix", 1.0f));
}

} // namespace ana
