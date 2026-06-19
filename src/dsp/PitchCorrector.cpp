#include "PitchCorrector.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <juce_dsp/juce_dsp.h>

namespace ana {

//==============================================================================
// Internal helpers
//==============================================================================
namespace {

constexpr float kPi = 3.14159265358979323846f;

/** Wrap phase angle to [-pi, pi] via IEEE remainder (branchless). */
inline float princArg(float x) noexcept
{
    return std::remainderf(x, 2.0f * kPi);
}

/** Clamp to [lo, hi]. */
inline float clamp(float v, float lo, float hi) noexcept
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/** Grow a vector to at least @p needed elements (never shrinks). */
template<typename T>
void resizeIfSmaller(std::vector<T>& v, size_t needed)
{
    if (v.size() < needed)
        v.resize(needed);
}

} // anonymous namespace

//==============================================================================
// Construction / initialisation
//==============================================================================

PitchCorrector::PitchCorrector()
{
    initDSP();
    ensureScratchSizes();
}

void PitchCorrector::initDSP()
{
    fftOrder_ = static_cast<int>(std::log2(static_cast<double>(fftSize_)));
    fft_      = std::make_unique<juce::dsp::FFT>(fftOrder_);
    hopSize_  = fftSize_ / 4;   // 75 % overlap
    halfSize_ = fftSize_ / 2 + 1;

    computeWindow();
}

void PitchCorrector::computeWindow()
{
    hannWindow_.resize(static_cast<size_t>(fftSize_));
    const float invN = 1.0f / static_cast<float>(fftSize_);

    for (int i = 0; i < fftSize_; ++i)
        hannWindow_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) * invN));
}

//==============================================================================
// Scratch-buffer sizing
//==============================================================================

void PitchCorrector::ensureScratchSizes() noexcept
{
    // Sizes based on current fftSize_ and a generous upper-bound for block size.
    // We intentionally avoid shrinking — once allocated, scratch buffers stay large.
    const size_t half   = static_cast<size_t>(halfSize_);
    const size_t fft2   = static_cast<size_t>(fftSize_) * 2;
    const size_t accCap = static_cast<size_t>(fftSize_) * 4; // generous accum cap

    // --- Shared FFT / phase-vocoder / spectral scratch buffers ---
    resizeIfSmaller(scratch_accum_,        accCap);
    resizeIfSmaller(scratch_prevPhase_,    half);
    resizeIfSmaller(scratch_outPhase_,     half);
    resizeIfSmaller(scratch_mag_,          half);
    resizeIfSmaller(scratch_envelope_,     half);
    resizeIfSmaller(scratch_fineStruct_,   half);
    resizeIfSmaller(scratch_shiftedFine_,  half);
    resizeIfSmaller(scratch_frame_,        fft2);

    // --- Granular scratch buffers ---
    int grainSize = std::min(fftSize_,
                             static_cast<int>(0.04 * sampleRate_));
    if (grainSize < 256)
        grainSize = 256;

    const size_t grainS = static_cast<size_t>(grainSize);
    resizeIfSmaller(scratch_grainWin_,     grainS);
    resizeIfSmaller(scratch_grainAccum_,   accCap);
    resizeIfSmaller(scratch_weightAccum_,  accCap);
    resizeIfSmaller(scratch_grain_,        grainS);

    // Re-compute cached grain window when grainSize changes
    if (grainSize != cachedGrainSize_)
    {
        cachedGrainSize_ = grainSize;
        scratch_grainWin_.resize(grainS);
        const float invGS = 1.0f / static_cast<float>(grainSize);
        for (size_t i = 0; i < grainS; ++i)
            scratch_grainWin_[i] = 0.5f * (1.0f - std::cos(2.0f * kPi
                                        * static_cast<float>(i) * invGS));
    }

    // --- Pitch detection scratch (double-precision autocorrelation buffer) ---
    // Worst-case: sampleRate at 192 kHz → maxLag = 192000/40 = 4800
    const size_t maxLagSize = static_cast<size_t>(sampleRate_ / 40.0) + 1;
    resizeIfSmaller(scratch_lpcR_,  maxLagSize);
    resizeIfSmaller(scratch_fftPitch_, fft2);

    // --- LPC scratch buffers (order ≤ 40) ---
    const size_t lpcN = 41;
    resizeIfSmaller(scratch_aPrev_, lpcN);
    resizeIfSmaller(scratch_aCurr_, lpcN);

    // Pre-size input copy buffer for default stereo + generous block (no alloc in process())
    if (inputCopyBuffer_.getNumChannels() < 2 || inputCopyBuffer_.getNumSamples() < 8192)
        inputCopyBuffer_.setSize(2, 8192, false, false, true);
}

//==============================================================================
// Setters
//==============================================================================

void PitchCorrector::setAlgorithm(PitchAlgorithm algo) noexcept
{
    algo_ = algo;
}

void PitchCorrector::setPitchShift(float semitones) noexcept
{
    pitchShift_ = clamp(semitones, -24.0f, 24.0f);
}

void PitchCorrector::setFormantPreservation(float amount) noexcept
{
    formantPreservation_ = clamp(amount, 0.0f, 1.0f);
}

void PitchCorrector::setCorrectionAmount(float amount) noexcept
{
    correctionAmount_ = clamp(amount, 0.0f, 1.0f);
}

void PitchCorrector::prepare(int maxNumChannels, int maxBlockSize)
{
    // Pre-allocate input copy buffer for worst-case block (never alloc in process())
    inputCopyBuffer_.setSize(maxNumChannels, maxBlockSize, false, false, true);
}

void PitchCorrector::setSampleRate(double sr) noexcept
{
    sampleRate_ = (sr > 0.0) ? sr : 44100.0;
    ensureScratchSizes();
}

void PitchCorrector::setFftSize(int size) noexcept
{
    // Validate power-of-2
    if (size < 64 || size > 8192)
        return;
    if ((size & (size - 1)) != 0)
        return;

    fftSize_ = size;
    initDSP();
    ensureScratchSizes();
}

void PitchCorrector::reset()
{
    // No persistent cross-call state; each process() works afresh
    // on its input buffer.  Subclasses with held-overlap filters
    // can override.
}

//==============================================================================
// Main process dispatcher
//==============================================================================

void PitchCorrector::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    // No shift or zero wet → identity
    if (std::abs(pitchShift_) < 0.01f || correctionAmount_ < 0.01f)
        return;

    // Snapshot input so every algorithm reads clean data (pre-allocated, no-heap)
    inputCopyBuffer_.makeCopyOf(buffer, true);

    switch (algo_)
    {
        case PitchAlgorithm::Simple:        processSimple(inputCopyBuffer_, buffer);        break;
        case PitchAlgorithm::PhaseVocoder:  processPhaseVocoder(inputCopyBuffer_, buffer);  break;
        case PitchAlgorithm::Spectral:      processSpectralShift(inputCopyBuffer_, buffer);  break;
        case PitchAlgorithm::Formant:       processFormantPreserving(inputCopyBuffer_, buffer); break;
        case PitchAlgorithm::Granular:      processGranularShift(inputCopyBuffer_, buffer); break;
    }

    // Global wet/dry mix (applies to all algorithms)
    if (correctionAmount_ < 0.99f)
    {
        const float dryMix = 1.0f - correctionAmount_;
        const float wetMix = correctionAmount_;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* dry = inputCopyBuffer_.getReadPointer(ch);
            float*       out = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] = dry[i] * dryMix + out[i] * wetMix;
        }
    }
}

//==============================================================================
// Simple  —  windowed sinc interpolation
//==============================================================================

void PitchCorrector::processSimple(const juce::AudioBuffer<float>& input,
                                   juce::AudioBuffer<float>& output)
{
    const int numChannels = input.getNumChannels();
    const int numSamples  = input.getNumSamples();
    const float ratio     = std::pow(2.0f, pitchShift_ / 12.0f);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in  = input.getReadPointer(ch);
        float*       out = output.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float pos = static_cast<float>(i) / ratio;
            out[i] = sincInterp(in, numSamples, pos, ratio);
        }
    }
}

//==============================================================================
// Phase-vocoder  —  FFT-based with instantaneous-frequency estimation
//==============================================================================

void PitchCorrector::processPhaseVocoder(const juce::AudioBuffer<float>& input,
                                         juce::AudioBuffer<float>& output)
{
    const int numChannels = input.getNumChannels();
    const int numSamples  = input.getNumSamples();
    const int half        = halfSize_;
    const int hop         = hopSize_;
    const int fftSize     = fftSize_;
    const float ratio     = std::pow(2.0f, pitchShift_ / 12.0f);
    const float binToFreq = static_cast<float>(sampleRate_) / static_cast<float>(fftSize);
    const float norm      = 0.5f;  // 75% overlap Hann sum = 2.0

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in  = input.getReadPointer(ch);
        float*       out = output.getWritePointer(ch);

        // Pre-allocated scratch buffers — no heap allocation in audio thread
        const size_t accumLen = static_cast<size_t>(numSamples + fftSize);
        if (scratch_accum_.size() < accumLen)
            scratch_accum_.resize(accumLen);  // only grows if DAW delivers huge block
        std::fill(scratch_accum_.begin(), scratch_accum_.begin() + static_cast<ptrdiff_t>(accumLen), 0.0f);

        std::fill(scratch_prevPhase_.begin(), scratch_prevPhase_.begin() + half, 0.0f);
        std::fill(scratch_outPhase_.begin(),  scratch_outPhase_.begin()  + half, 0.0f);

        // FFT work buffer needs no pre-zero (vectorMul + memset overwrite fully)
        float* accum    = scratch_accum_.data();
        float* prevPhase = scratch_prevPhase_.data();
        float* outPhase  = scratch_outPhase_.data();
        float* frame     = scratch_frame_.data();

        for (int pos = 0; pos + fftSize <= numSamples; pos += hop)
        {
            // --- Analysis ---
            SIMDKernels::vectorMul(frame,
                                   in + pos,
                                   hannWindow_.data(),
                                   fftSize);
            // Zero the imaginary half
            std::memset(frame + fftSize, 0,
                        static_cast<size_t>(fftSize) * sizeof(float));

            fft_->performRealOnlyForwardTransform(frame);

            // --- Phase-vocoder frequency scaling ---
            for (int k = 0; k < half; ++k)
            {
                const float real = frame[static_cast<size_t>(2 * k)];
                const float imag = frame[static_cast<size_t>(2 * k + 1)];
                const float mag  = std::sqrt(real * real + imag * imag);
                const float phase = std::atan2(imag, real);

                // Phase difference from previous frame
                float delta = phase - prevPhase[static_cast<size_t>(k)];

                // Expected phase advance for stationary sinusoid at bin k
                const float expected = 2.0f * kPi * static_cast<float>(hop * k)
                                       / static_cast<float>(fftSize);
                delta = princArg(delta - expected);

                // Instantaneous frequency (Hz)
                const float instFreq = (static_cast<float>(k)
                                        + delta / (2.0f * kPi)) * binToFreq;

                // New frequency after pitch shift
                const float newFreq = instFreq * ratio;

                // Accumulate synthesis phase
                outPhase[static_cast<size_t>(k)] += 2.0f * kPi * newFreq
                                                     * static_cast<float>(hop)
                                                     / static_cast<float>(sampleRate_);

                prevPhase[static_cast<size_t>(k)] = phase;

                // Write synthesised bin
                frame[static_cast<size_t>(2 * k)]     = mag * std::cos(outPhase[static_cast<size_t>(k)]);
                frame[static_cast<size_t>(2 * k + 1)] = mag * std::sin(outPhase[static_cast<size_t>(k)]);
            }

            // --- Synthesis ---
            fft_->performRealOnlyInverseTransform(frame);

            // Overlap-add
            for (int i = 0; i < fftSize; ++i)
                accum[static_cast<size_t>(pos + i)] += frame[static_cast<size_t>(i)];
        }

        // Normalise and write
        for (int i = 0; i < numSamples; ++i)
            out[i] = accum[static_cast<size_t>(i)] * norm;
    }
}

//==============================================================================
// Spectral shift  —  envelope / fine-structure separation
//==============================================================================

void PitchCorrector::processSpectralShift(const juce::AudioBuffer<float>& input,
                                          juce::AudioBuffer<float>& output)
{
    const int numChannels = input.getNumChannels();
    const int numSamples  = input.getNumSamples();
    const int half        = halfSize_;
    const int hop         = hopSize_;
    const int fftSize     = fftSize_;
    const float ratio     = std::pow(2.0f, pitchShift_ / 12.0f);
    const float binToFreq = static_cast<float>(sampleRate_) / static_cast<float>(fftSize);
    const float norm      = 0.5f;
    const int smoothWidth = std::max(3, half / 20);  // narrow smoothing
    const float fp       = formantPreservation_;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in  = input.getReadPointer(ch);
        float*       out = output.getWritePointer(ch);

        // Pre-allocated scratch buffers — no heap allocation in audio thread
        const size_t accumLen = static_cast<size_t>(numSamples + fftSize);
        if (scratch_accum_.size() < accumLen)
            scratch_accum_.resize(accumLen);
        std::fill(scratch_accum_.begin(), scratch_accum_.begin() + static_cast<ptrdiff_t>(accumLen), 0.0f);

        std::fill(scratch_prevPhase_.begin(), scratch_prevPhase_.begin() + half, 0.0f);
        std::fill(scratch_outPhase_.begin(),  scratch_outPhase_.begin()  + half, 0.0f);

        float* accum       = scratch_accum_.data();
        float* prevPhase   = scratch_prevPhase_.data();
        float* outPhase    = scratch_outPhase_.data();
        float* mag         = scratch_mag_.data();
        float* envelope    = scratch_envelope_.data();
        float* fineStruct  = scratch_fineStruct_.data();
        float* shiftedFine = scratch_shiftedFine_.data();
        float* frame       = scratch_frame_.data();

        for (int pos = 0; pos + fftSize <= numSamples; pos += hop)
        {
            // --- Analysis ---
            SIMDKernels::vectorMul(frame,
                                   in + pos,
                                   hannWindow_.data(),
                                   fftSize);
            std::memset(frame + fftSize, 0,
                        static_cast<size_t>(fftSize) * sizeof(float));
            fft_->performRealOnlyForwardTransform(frame);

            // Magnitude + phase tracking
            for (int k = 0; k < half; ++k)
            {
                const float real = frame[static_cast<size_t>(2 * k)];
                const float imag = frame[static_cast<size_t>(2 * k + 1)];
                mag[static_cast<size_t>(k)] = std::sqrt(real * real + imag * imag);

                const float phase = std::atan2(imag, real);
                float delta = phase - prevPhase[static_cast<size_t>(k)];
                const float expected = 2.0f * kPi * static_cast<float>(hop * k)
                                       / static_cast<float>(fftSize);
                delta = princArg(delta - expected);
                const float instFreq = (static_cast<float>(k)
                                        + delta / (2.0f * kPi)) * binToFreq;
                const float newFreq = instFreq * ratio;
                outPhase[static_cast<size_t>(k)] += 2.0f * kPi * newFreq
                                                     * static_cast<float>(hop)
                                                     / static_cast<float>(sampleRate_);
                prevPhase[static_cast<size_t>(k)] = phase;
            }

            // --- Envelope extraction (spectral smoothing) ---
            spectralSmooth(mag, half, envelope, smoothWidth);

            // --- Fine structure = magnitude / envelope ---
            for (int k = 0; k < half; ++k)
            {
                const float env = envelope[static_cast<size_t>(k)];
                fineStruct[static_cast<size_t>(k)] =
                    (env > 1e-10f) ? mag[static_cast<size_t>(k)] / env : 0.0f;
            }

            // --- Shift fine structure in frequency domain ---
            std::memset(shiftedFine, 0, static_cast<size_t>(half) * sizeof(float));
            for (int k = 0; k < half; ++k)
            {
                const float srcBin = static_cast<float>(k) / ratio;
                if (srcBin >= static_cast<float>(half))
                    continue;
                const int   srcInt = static_cast<int>(srcBin);
                const float frac   = srcBin - static_cast<float>(srcInt);

                if (srcInt + 1 < half)
                    shiftedFine[static_cast<size_t>(k)] =
                        fineStruct[static_cast<size_t>(srcInt)] * (1.0f - frac)
                        + fineStruct[static_cast<size_t>(srcInt + 1)] * frac;
                else
                    shiftedFine[static_cast<size_t>(k)] =
                        fineStruct[static_cast<size_t>(srcInt)];
            }

            // --- Reconstruct ---
            for (int k = 0; k < half; ++k)
            {
                // Blend between shifted and original fine structure
                const float blendedFine =
                    shiftedFine[static_cast<size_t>(k)] * fp
                    + fineStruct[static_cast<size_t>(k)] * (1.0f - fp);

                const float finalMag = envelope[static_cast<size_t>(k)] * blendedFine;

                const float outPh = outPhase[static_cast<size_t>(k)];
                frame[static_cast<size_t>(2 * k)]     = finalMag * std::cos(outPh);
                frame[static_cast<size_t>(2 * k + 1)] = finalMag * std::sin(outPh);
            }

            // --- Synthesis ---
            fft_->performRealOnlyInverseTransform(frame);
            for (int i = 0; i < fftSize; ++i)
                accum[static_cast<size_t>(pos + i)] += frame[static_cast<size_t>(i)];
        }

        for (int i = 0; i < numSamples; ++i)
            out[i] = accum[static_cast<size_t>(i)] * norm;
    }
}

//==============================================================================
// Formant preserving  —  LPC-based envelope with wider smoothing
//==============================================================================

void PitchCorrector::processFormantPreserving(const juce::AudioBuffer<float>& input,
                                              juce::AudioBuffer<float>& output)
{
    const int numChannels = input.getNumChannels();
    const int numSamples  = input.getNumSamples();
    const int half        = halfSize_;
    const int hop         = hopSize_;
    const int fftSize     = fftSize_;
    const float ratio     = std::pow(2.0f, pitchShift_ / 12.0f);
    const float binToFreq = static_cast<float>(sampleRate_) / static_cast<float>(fftSize);
    const float norm      = 0.5f;

    // Wider smoothing for stronger formant preservation
    const int smoothWidth  = std::max(5, half / 8);
    const float fpBlend   = formantPreservation_;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in  = input.getReadPointer(ch);
        float*       out = output.getWritePointer(ch);

        // Pre-allocated scratch buffers — no heap allocation in audio thread
        const size_t accumLen = static_cast<size_t>(numSamples + fftSize);
        if (scratch_accum_.size() < accumLen)
            scratch_accum_.resize(accumLen);
        std::fill(scratch_accum_.begin(), scratch_accum_.begin() + static_cast<ptrdiff_t>(accumLen), 0.0f);

        std::fill(scratch_prevPhase_.begin(), scratch_prevPhase_.begin() + half, 0.0f);
        std::fill(scratch_outPhase_.begin(),  scratch_outPhase_.begin()  + half, 0.0f);

        float* accum       = scratch_accum_.data();
        float* prevPhase   = scratch_prevPhase_.data();
        float* outPhase    = scratch_outPhase_.data();
        float* mag         = scratch_mag_.data();
        float* envelope    = scratch_envelope_.data();
        float* fineStruct  = scratch_fineStruct_.data();
        float* shiftedFine = scratch_shiftedFine_.data();
        float* frame       = scratch_frame_.data();

        for (int pos = 0; pos + fftSize <= numSamples; pos += hop)
        {
            // --- Analysis window + FFT ---
            SIMDKernels::vectorMul(frame, in + pos, hannWindow_.data(), fftSize);
            std::memset(frame + fftSize, 0,
                        static_cast<size_t>(fftSize) * sizeof(float));
            fft_->performRealOnlyForwardTransform(frame);

            // Magnitude + phase
            for (int k = 0; k < half; ++k)
            {
                const float real = frame[static_cast<size_t>(2 * k)];
                const float imag = frame[static_cast<size_t>(2 * k + 1)];
                mag[static_cast<size_t>(k)] = std::sqrt(real * real + imag * imag);

                const float phase = std::atan2(imag, real);
                float delta = phase - prevPhase[static_cast<size_t>(k)];
                const float expected = 2.0f * kPi * static_cast<float>(hop * k)
                                       / static_cast<float>(fftSize);
                delta = princArg(delta - expected);
                const float instFreq = (static_cast<float>(k)
                                        + delta / (2.0f * kPi)) * binToFreq;
                const float newFreq = instFreq * ratio;
                outPhase[static_cast<size_t>(k)] += 2.0f * kPi * newFreq
                                                     * static_cast<float>(hop)
                                                     / static_cast<float>(sampleRate_);
                prevPhase[static_cast<size_t>(k)] = phase;
            }

            // --- Formant envelope via wide spectral smoothing ---
            spectralSmooth(mag, half, envelope, smoothWidth);

            // --- Fine structure ---
            for (int k = 0; k < half; ++k)
            {
                const float env = envelope[static_cast<size_t>(k)];
                fineStruct[static_cast<size_t>(k)] =
                    (env > 1e-10f) ? mag[static_cast<size_t>(k)] / env : 0.0f;
            }

            // --- Shift fine structure ---
            std::memset(shiftedFine, 0, static_cast<size_t>(half) * sizeof(float));
            for (int k = 0; k < half; ++k)
            {
                const float srcBin = static_cast<float>(k) / ratio;
                if (srcBin >= static_cast<float>(half))
                    continue;
                const int   srcInt = static_cast<int>(srcBin);
                const float frac   = srcBin - static_cast<float>(srcInt);

                if (srcInt + 1 < half)
                    shiftedFine[static_cast<size_t>(k)] =
                        fineStruct[static_cast<size_t>(srcInt)] * (1.0f - frac)
                        + fineStruct[static_cast<size_t>(srcInt + 1)] * frac;
                else
                    shiftedFine[static_cast<size_t>(k)] =
                        fineStruct[static_cast<size_t>(srcInt)];
            }

            // --- Reconstruct with formant-blend ---
            for (int k = 0; k < half; ++k)
            {
                const float blFine = shiftedFine[static_cast<size_t>(k)] * fpBlend
                                     + fineStruct[static_cast<size_t>(k)] * (1.0f - fpBlend);
                const float finalMag = envelope[static_cast<size_t>(k)] * blFine;

                const float ph = outPhase[static_cast<size_t>(k)];
                frame[static_cast<size_t>(2 * k)]     = finalMag * std::cos(ph);
                frame[static_cast<size_t>(2 * k + 1)] = finalMag * std::sin(ph);
            }

            fft_->performRealOnlyInverseTransform(frame);
            for (int i = 0; i < fftSize; ++i)
                accum[static_cast<size_t>(pos + i)] += frame[static_cast<size_t>(i)];
        }

        for (int i = 0; i < numSamples; ++i)
            out[i] = accum[static_cast<size_t>(i)] * norm;
    }
}

//==============================================================================
// Granular  —  grain-based pitch shifting
//==============================================================================

void PitchCorrector::processGranularShift(const juce::AudioBuffer<float>& input,
                                          juce::AudioBuffer<float>& output)
{
    const int numChannels = input.getNumChannels();
    const int numSamples  = input.getNumSamples();
    const float ratio     = std::pow(2.0f, pitchShift_ / 12.0f);

    // Grain size (~40 ms at sample rate, capped at fftSize).
    // scratch_grainWin_ is pre-computed by ensureScratchSizes().
    const int grainSize = cachedGrainSize_;
    const float* grainWin = scratch_grainWin_.data();
    const int grainHop    = grainSize / 4;                          // 75% overlap

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in  = input.getReadPointer(ch);
        float*       out = output.getWritePointer(ch);

        const size_t outLen = static_cast<size_t>(numSamples + grainSize);
        if (scratch_grainAccum_.size() < outLen)
            scratch_grainAccum_.resize(outLen);
        if (scratch_weightAccum_.size() < outLen)
            scratch_weightAccum_.resize(outLen);
        std::fill(scratch_grainAccum_.begin(), scratch_grainAccum_.begin() + static_cast<ptrdiff_t>(outLen), 0.0f);
        std::fill(scratch_weightAccum_.begin(), scratch_weightAccum_.begin() + static_cast<ptrdiff_t>(outLen), 0.0f);

        float* grainAccum   = scratch_grainAccum_.data();
        float* weightAccum  = scratch_weightAccum_.data();
        float* grain        = scratch_grain_.data();

        int srcPos = 0;
        while (srcPos < numSamples)
        {
            // Output position for this grain (duration compensated)
            const int dstPos = static_cast<int>(static_cast<float>(srcPos) / ratio);
            if (dstPos >= numSamples + grainSize)
                break;

            // Extract and window grain from input (use scratch_grain_, zero first)
            std::memset(grain, 0, static_cast<size_t>(grainSize) * sizeof(float));
            for (int i = 0; i < grainSize; ++i)
            {
                const int readIdx = srcPos + i;
                if (readIdx < numSamples)
                    grain[static_cast<size_t>(i)] =
                        in[readIdx] * grainWin[static_cast<size_t>(i)];
            }

            // Resample grain — read at ratio to change pitch
            const int resampledLen = static_cast<int>(static_cast<float>(grainSize) / ratio);

            for (int i = 0; i < resampledLen; ++i)
            {
                const float readPos = static_cast<float>(i) * ratio;
                const int   ri      = static_cast<int>(readPos);
                const float frac    = readPos - static_cast<float>(ri);

                float val;
                if (ri + 1 < grainSize)
                    val = grain[static_cast<size_t>(ri)] * (1.0f - frac)
                          + grain[static_cast<size_t>(ri + 1)] * frac;
                else if (ri < grainSize)
                    val = grain[static_cast<size_t>(ri)];
                else
                    val = 0.0f;

                const int writeIdx = dstPos + i;
                if (writeIdx >= 0 && static_cast<size_t>(writeIdx) < outLen)
                {
                    grainAccum[static_cast<size_t>(writeIdx)]  += val;
                    weightAccum[static_cast<size_t>(writeIdx)] += 1.0f;
                }
            }

            srcPos += grainHop;
        }

        // Normalise by grain count and copy
        for (int i = 0; i < numSamples; ++i)
        {
            const float w = weightAccum[static_cast<size_t>(i)];
            out[i] = (w > 0.01f) ? grainAccum[static_cast<size_t>(i)] / w : 0.0f;
        }
    }
}

//==============================================================================
// Pitch detection  —  autocorrelation with parabolic interpolation
//==============================================================================

float PitchCorrector::detectPitch(const std::vector<float>& audio,
                                  double sampleRate)
{
    const int n = static_cast<int>(audio.size());
    if (n < 100)
        return 0.0f;

    // Autocorrelation (uses pre-allocated scratch buffer — no heap alloc)
    const int maxLag = std::min(static_cast<int>(sampleRate / 40.0),    // 40 Hz floor
                                n / 2);
    const int minLag = std::max(2, static_cast<int>(sampleRate / 2000.0));  // 2 kHz ceiling

    if (scratch_lpcR_.size() < static_cast<size_t>(maxLag + 1))
        scratch_lpcR_.resize(static_cast<size_t>(maxLag + 1));
    double* r = scratch_lpcR_.data();

    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i < n - lag; ++i)
            sum += static_cast<double>(audio[static_cast<size_t>(i)])
                 * static_cast<double>(audio[static_cast<size_t>(i + lag)]);
        r[static_cast<size_t>(lag)] = sum;
    }

    // Find peak
    int    bestLag = minLag;
    double bestVal = r[static_cast<size_t>(minLag)];

    for (int lag = minLag + 1; lag <= maxLag; ++lag)
    {
        const double v = r[static_cast<size_t>(lag)];
        if (v > bestVal)
        {
            bestVal = v;
            bestLag = lag;
        }
    }

    // Confidence check — peak must be at least 10 % of autocorrelation at 0
    if (bestVal < r[0] * 0.1)
        return 0.0f;

    // Parabolic interpolation around the peak
    if (bestLag > 0 && bestLag < maxLag)
    {
        const double y0 = r[static_cast<size_t>(bestLag - 1)];
        const double y1 = r[static_cast<size_t>(bestLag)];
        const double y2 = r[static_cast<size_t>(bestLag + 1)];
        const double a  = 0.5 * (y0 + y2) - y1;
        const double b  = 0.5 * (y2 - y0);
        if (std::abs(a) > 1e-15)
            bestLag = bestLag - static_cast<int>(b / (2.0 * a));
    }

    if (bestLag < 1)
        return 0.0f;

    const float freq = static_cast<float>(sampleRate) / static_cast<float>(bestLag);
    // MIDI note: A4 = 69 = 440 Hz
    const float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);

    return clamp(midi, 0.0f, 127.0f);
}

//==============================================================================
// Helpers
//==============================================================================

float PitchCorrector::sincInterp(const float* in, int len,
                                 float pos, float ratio)
{
    const int halfLen = 8;  // 8 lobes each side
    const int i0      = static_cast<int>(std::floor(pos));
    const float frac  = pos - static_cast<float>(i0);

    // Anti-aliasing cutoff: when ratio > 1 (pitch up) we need to low-pass
    const float cutoff = (ratio > 1.0f) ? 1.0f / ratio : 1.0f;

    double result = 0.0;
    double sumW   = 0.0;

    for (int j = -halfLen + 1; j <= halfLen; ++j)
    {
        const int idx = i0 + j;
        if (idx < 0 || idx >= len)
            continue;

        const float d    = static_cast<float>(j) - frac;
        const float absD = std::abs(d);

        if (absD >= static_cast<float>(halfLen))
            continue;

        // Blackman window
        const float win = 0.42f
                        + 0.5f * std::cos(kPi * absD / static_cast<float>(halfLen))
                        + 0.08f * std::cos(2.0f * kPi * absD / static_cast<float>(halfLen));

        // Sinc with anti-alias cutoff
        const float sinc = (absD < 1e-8f)
                               ? cutoff
                               : std::sin(kPi * d * cutoff) / (kPi * d);

        const float w = sinc * win;
        result += static_cast<double>(in[idx] * w);
        sumW   += static_cast<double>(w);
    }

    return (sumW > 0.0) ? static_cast<float>(result / sumW) : 0.0f;
}

void PitchCorrector::spectralSmooth(const float* mag, int halfSize,
                                    float* envelope, int smoothWidth)
{
    if (halfSize <= 0 || smoothWidth <= 0)
    {
        if (halfSize > 0)
            std::memcpy(envelope, mag, static_cast<size_t>(halfSize) * sizeof(float));
        return;
    }

    // O(n) two-pass box filter → triangular smoothing.
    // Two identical box filters cascaded = triangle kernel.
    // Edge handling at boundaries naturally produces shorter tails.
    // Pass 1: box-filter mag → envelope (stored as temp).
    {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < halfSize; ++k)
        {
            const int inIdx = k + smoothWidth;
            if (inIdx < halfSize) { sum += static_cast<double>(mag[inIdx]); ++n; }
            const int outIdx = k - smoothWidth - 1;
            if (outIdx >= 0) { sum -= static_cast<double>(mag[outIdx]); --n; }
            envelope[k] = static_cast<float>(sum / std::max(1, n));
        }
    }
    // Pass 2: box-filter envelope (temp) → final envelope (in-place).
    {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < halfSize; ++k)
        {
            const int inIdx = k + smoothWidth;
            if (inIdx < halfSize) { sum += static_cast<double>(envelope[inIdx]); ++n; }
            const int outIdx = k - smoothWidth - 1;
            if (outIdx >= 0) { sum -= static_cast<double>(envelope[outIdx]); --n; }
            envelope[k] = static_cast<float>(sum / std::max(1, n));
        }
    }
}

void PitchCorrector::lpcAutocorrelation(const float* x, int n, int order,
                                        float* coeffs, float& gain)
{
    if (n <= order || order < 1)
    {
        for (int i = 0; i <= order; ++i)
            coeffs[i] = (i == 0) ? 1.0f : 0.0f;
        gain = 1.0f;
        return;
    }

    // --- Autocorrelation (double precision, pre-allocated scratch) ---
    if (scratch_lpcR_.size() < static_cast<size_t>(order + 1))
        scratch_lpcR_.resize(static_cast<size_t>(order + 1));
    double* r = scratch_lpcR_.data();
    // Zero the scratch buffer (only first-order+1 needed)
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

    // --- Levinson-Durbin recursion (pre-allocated scratch) ---
    float* aPrev = scratch_aPrev_.data();
    float* aCurr = scratch_aCurr_.data();
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

} // namespace ana
