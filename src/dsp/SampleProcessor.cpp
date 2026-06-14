#include "SampleProcessor.h"
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

/** Note names for display. */
const char* noteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

} // anonymous namespace

//==============================================================================
// Construction / initialisation
//==============================================================================

SampleProcessor::SampleProcessor()
{
    const int fftOrder = static_cast<int>(std::log2(static_cast<double>(fftSize_)));
    fft_ = std::make_unique<juce::dsp::FFT>(fftOrder);
    hopSize_ = fftSize_ / 4;  // 75 % overlap

    // Pre-compute Hann window
    hannWindow_.resize(static_cast<size_t>(fftSize_));
    const float invN = 1.0f / static_cast<float>(fftSize_);
    for (int i = 0; i < fftSize_; ++i)
        hannWindow_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) * invN));

    // Pre-size scratch buffers
    const size_t half = static_cast<size_t>(fftSize_ / 2 + 1);
    const size_t fft2 = static_cast<size_t>(fftSize_) * 2;

    resizeIfSmaller(scratch_frame_,       fft2);
    resizeIfSmaller(scratch_prevPhase_,   half);
    resizeIfSmaller(scratch_outPhase_,    half);
    resizeIfSmaller(scratch_mag_,         half);
    resizeIfSmaller(scratch_envelope_,    half);
    resizeIfSmaller(scratch_fineStruct_,  half);
    resizeIfSmaller(scratch_shiftedFine_, half);

    // YIN buffer: maxLag = sampleRate / 40 ≈ 1102 at 44.1 kHz
    resizeIfSmaller(scratch_yinDiff_, static_cast<size_t>(sampleRate_ / 40.0) + 1);
}

//==============================================================================
// MIDI note conversion
//==============================================================================

float SampleProcessor::midiNoteToFrequency(int note, float fineTuneCents)
{
    return 440.0f * std::pow(2.0f,
        (static_cast<float>(note) - 69.0f + fineTuneCents / 100.0f) / 12.0f);
}

//==============================================================================
// Getters / Setters
//==============================================================================

void SampleProcessor::setRootNote(int midiNote)
{
    rootNote_ = static_cast<int>(clamp(static_cast<float>(midiNote), 0.0f, 127.0f));
}

int SampleProcessor::getRootNote() const { return rootNote_; }

void SampleProcessor::setRootFineTune(float cents)
{
    rootFineTune_ = clamp(cents, -50.0f, 50.0f);
}

float SampleProcessor::getRootFineTune() const { return rootFineTune_; }

void SampleProcessor::setSampleRate(double sr)
{
    if (sr <= 0.0)
        return;
    sampleRate_ = sr;

    // Re-size YIN buffer
    resizeIfSmaller(scratch_yinDiff_, static_cast<size_t>(sampleRate_ / 40.0) + 1);
}

void SampleProcessor::setFftSize(int size)
{
    if (size < 64 || size > 8192)
        return;
    if ((size & (size - 1)) != 0)
        return;

    fftSize_ = size;
    hopSize_ = fftSize_ / 4;

    // Re-create FFT
    const int fftOrder = static_cast<int>(std::log2(static_cast<double>(fftSize_)));
    fft_ = std::make_unique<juce::dsp::FFT>(fftOrder);

    // Recompute window
    hannWindow_.resize(static_cast<size_t>(fftSize_));
    const float invN = 1.0f / static_cast<float>(fftSize_);
    for (int i = 0; i < fftSize_; ++i)
        hannWindow_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) * invN));

    // Re-size scratch
    const size_t half = static_cast<size_t>(fftSize_ / 2 + 1);
    const size_t fft2 = static_cast<size_t>(fftSize_) * 2;

    resizeIfSmaller(scratch_frame_,       fft2);
    resizeIfSmaller(scratch_prevPhase_,   half);
    resizeIfSmaller(scratch_outPhase_,    half);
    resizeIfSmaller(scratch_mag_,         half);
    resizeIfSmaller(scratch_envelope_,    half);
    resizeIfSmaller(scratch_fineStruct_,  half);
    resizeIfSmaller(scratch_shiftedFine_, half);
}

void SampleProcessor::reset()
{
    lastResult_ = PitchDetectionResult{};
}

//==============================================================================
// getPitchDisplayText
//==============================================================================

juce::String SampleProcessor::getPitchDisplayText(float freqHz) const
{
    if (freqHz <= 0.0f)
        return juce::String("---");

    const float midiFloat = 69.0f + 12.0f * std::log2(freqHz / 440.0f);
    const int   noteNum   = static_cast<int>(std::round(midiFloat));
    const float cents     = (midiFloat - static_cast<float>(noteNum)) * 100.0f;

    const int octave       = (noteNum / 12) - 1;
    const int noteInOctave = noteNum % 12;

    juce::String result;
    result << noteNames[noteInOctave] << octave;

    const int roundedCents = static_cast<int>(std::round(cents));
    if (std::abs(roundedCents) >= 1)
        result << " (" << (cents > 0 ? "+" : "") << roundedCents << " cents)";
    else
        result << " (0 cents)";

    return result;
}

//==============================================================================
// YIN Pitch Detection  (per frame, individual YIN instances)
//==============================================================================

float SampleProcessor::yinPitchDetection(const float* audio, int n,
                                          double sampleRate,
                                          float& confidence,
                                          float threshold) const
{
    const int maxLag = std::min(static_cast<int>(sampleRate / 40.0), n / 2);
    const int minLag = std::max(2, static_cast<int>(sampleRate / 2000.0));

    if (maxLag <= minLag)
    {
        confidence = 0.0f;
        return 0.0f;
    }

    // Ensure scratch buffer is large enough
    const size_t bufSize = static_cast<size_t>(maxLag + 1);
    if (scratch_yinDiff_.size() < bufSize)
        scratch_yinDiff_.resize(bufSize);
    double* diff = scratch_yinDiff_.data();

    // --- Step 1: Difference function  d(τ) = Σ (x[i] - x[i+τ])² ---
    // Compute d(0) for energy reference
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
        energy += static_cast<double>(audio[i]) * static_cast<double>(audio[i]);

    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double sum = 0.0;
        const int limit = n - tau;
        for (int i = 0; i < limit; ++i)
        {
            const double d = static_cast<double>(audio[i])
                           - static_cast<double>(audio[i + tau]);
            sum += d * d;
        }
        diff[tau] = sum;
    }
    diff[0] = 0.0;

    // --- Step 2: Cumulative mean normalized difference ---
    double runningSum = 0.0;
    double bestNormVal = 1.0;
    int    bestLag = minLag;
    bool   thresholdFound = false;

    for (int tau = 1; tau <= maxLag; ++tau)
    {
        runningSum += diff[tau];
        double normVal = diff[tau];
        if (runningSum > 0.0)
            normVal = normVal * static_cast<double>(tau) / runningSum;

        // YIN absolute threshold: find FIRST minimum below threshold
        if (!thresholdFound && tau >= minLag && normVal < threshold)
        {
            bestLag = tau;
            bestNormVal = normVal;
            thresholdFound = true;
        }

        // Track global minimum as fallback
        if (!thresholdFound && normVal < bestNormVal)
        {
            bestNormVal = normVal;
            bestLag = tau;
        }
    }

    // If no minimum found (e.g. silence), fall back
    if (bestNormVal >= 0.99)
    {
        confidence = 0.0f;
        return 0.0f;
    }

    // --- Step 3: Parabolic interpolation around the minimum ---
    // We interpolate the raw difference function (not normalized)
    // for sub-sample accuracy in the frequency estimate.
    double fracLag = static_cast<double>(bestLag);
    if (bestLag > 1 && bestLag < maxLag)
    {
        const double y0 = diff[bestLag - 1];
        const double y1 = diff[bestLag];
        const double y2 = diff[bestLag + 1];
        const double a  = 0.5 * (y0 + y2) - y1;
        const double b  = 0.5 * (y2 - y0);
        if (std::abs(a) > 1e-15)
        {
            const double shift = -b / (2.0 * a);
            if (std::abs(shift) < 1.0)
                fracLag += shift;
        }
    }

    if (fracLag < 1.0)
    {
        confidence = 0.0f;
        return 0.0f;
    }

    const float freq = static_cast<float>(sampleRate) / static_cast<float>(fracLag);

    // Confidence estimate based on normalized difference minimum
    // 0.0 = perfect periodic, 1.0 = no pitch
    confidence = static_cast<float>(clamp(1.0 - bestNormVal, 0.0, 1.0));

    // Reduce confidence for very quiet signals
    if (energy < 1e-6)
        confidence = 0.0f;

    return freq;
}

//==============================================================================
// analyzePitchFrames
//==============================================================================

void SampleProcessor::analyzePitchFrames(const std::vector<float>& audio,
                                          double sampleRate,
                                          std::vector<float>& pitchCurve,
                                          std::vector<float>& confidenceCurve) const
{
    const int n = static_cast<int>(audio.size());
    const int frameSize = fftSize_;
    const int hopSize   = hopSize_;

    pitchCurve.clear();
    confidenceCurve.clear();

    if (n < 64)
        return;

    if (n < frameSize)
    {
        float conf;
        float freq = yinPitchDetection(audio.data(), n, sampleRate, conf);
        pitchCurve.push_back(freq);
        confidenceCurve.push_back(conf);
        return;
    }

    for (int pos = 0; pos + frameSize <= n; pos += hopSize)
    {
        float conf;
        float freq = yinPitchDetection(audio.data() + pos, frameSize,
                                        sampleRate, conf);
        pitchCurve.push_back(freq);
        confidenceCurve.push_back(conf);
    }
}

//==============================================================================
// medianFilter
//==============================================================================

void SampleProcessor::medianFilter(std::vector<float>& data, int windowSize)
{
    const int n = static_cast<int>(data.size());
    if (n < windowSize || windowSize < 3 || windowSize % 2 == 0)
        return;

    const int halfWindow = windowSize / 2;
    std::vector<float> result(static_cast<size_t>(n));
    std::vector<float> window(static_cast<size_t>(windowSize));

    for (int i = 0; i < n; ++i)
    {
        int count = 0;
        for (int j = -halfWindow; j <= halfWindow; ++j)
        {
            const int idx = i + j;
            if (idx >= 0 && idx < n)
                window[static_cast<size_t>(count++)] = data[static_cast<size_t>(idx)];
        }

        // nth_element for O(n) median selection
        if (count > 0)
        {
            const int mid = count / 2;
            std::nth_element(window.begin(),
                             window.begin() + mid,
                             window.begin() + count);
            result[static_cast<size_t>(i)] = window[static_cast<size_t>(mid)];
        }
        else
        {
            result[static_cast<size_t>(i)] = data[static_cast<size_t>(i)];
        }
    }

    data = std::move(result);
}

//==============================================================================
// detectPitch
//==============================================================================

PitchDetectionResult SampleProcessor::detectPitch(const std::vector<float>& audio,
                                                   double sampleRate)
{
    PitchDetectionResult result;

    if (audio.empty() || sampleRate <= 0.0)
        return result;

    // Frame-based pitch analysis
    analyzePitchFrames(audio, sampleRate, result.pitchCurve, result.confidenceCurve);

    result.numFrames = static_cast<int>(result.pitchCurve.size());
    if (result.numFrames == 0)
        return result;

    // Median filter the pitch curve to remove outliers
    medianFilter(result.pitchCurve, 5);

    // Compute average confidence (weighted by pitch validity)
    double totalConf = 0.0;
    int validFrames = 0;
    for (int i = 0; i < result.numFrames; ++i)
    {
        if (result.pitchCurve[static_cast<size_t>(i)] > 0.0f)
        {
            totalConf += static_cast<double>(result.confidenceCurve[static_cast<size_t>(i)]);
            ++validFrames;
        }
    }

    if (validFrames == 0)
        return result;

    result.confidence = static_cast<float>(totalConf / static_cast<double>(validFrames));

    // Find median frequency among valid frames (robust to outliers)
    std::vector<float> validPitches;
    validPitches.reserve(static_cast<size_t>(validFrames));
    for (int i = 0; i < result.numFrames; ++i)
    {
        if (result.pitchCurve[static_cast<size_t>(i)] > 0.0f)
            validPitches.push_back(result.pitchCurve[static_cast<size_t>(i)]);
    }

    if (validPitches.empty())
        return result;

    std::nth_element(validPitches.begin(),
                     validPitches.begin() + validPitches.size() / 2,
                     validPitches.end());
    const float medianFreq = validPitches[validPitches.size() / 2];
    result.detectedFreq = medianFreq;

    // Convert to MIDI note
    const float midiFloat = 69.0f + 12.0f * std::log2(medianFreq / 440.0f);
    result.detectedMidiNote = static_cast<int>(std::round(midiFloat));
    result.detectedCents = (midiFloat - static_cast<float>(result.detectedMidiNote)) * 100.0f;

    // Keep a copy
    lastResult_ = result;

    return result;
}

//==============================================================================
// detectRootNote
//==============================================================================

int SampleProcessor::detectRootNote(const std::vector<float>& audio,
                                     double sampleRate)
{
    return detectPitch(audio, sampleRate).detectedMidiNote;
}

//==============================================================================
// spectralSmooth  —  triangular via two-pass box filter
//==============================================================================

void SampleProcessor::spectralSmooth(const float* mag, int halfSize,
                                      float* envelope, int smoothWidth)
{
    if (halfSize <= 0 || smoothWidth <= 0)
    {
        if (halfSize > 0)
            std::memcpy(envelope, mag, static_cast<size_t>(halfSize) * sizeof(float));
        return;
    }

    // Pass 1: box-filter
    {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < halfSize; ++k)
        {
            const int inIdx = k + smoothWidth;
            if (inIdx < halfSize) { sum += static_cast<double>(mag[inIdx]); ++n; }
            const int outIdx = k - smoothWidth - 1;
            if (outIdx >= 0)   { sum -= static_cast<double>(mag[outIdx]); --n; }
            envelope[k] = static_cast<float>(sum / std::max(1, n));
        }
    }
    // Pass 2: box-filter again → triangular
    {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < halfSize; ++k)
        {
            const int inIdx = k + smoothWidth;
            if (inIdx < halfSize) { sum += static_cast<double>(envelope[inIdx]); ++n; }
            const int outIdx = k - smoothWidth - 1;
            if (outIdx >= 0)   { sum -= static_cast<double>(envelope[outIdx]); --n; }
            envelope[k] = static_cast<float>(sum / std::max(1, n));
        }
    }
}

//==============================================================================
// flattenVocoder  —  standard phase vocoder with per-frame ratios
//==============================================================================

void SampleProcessor::flattenVocoder(const float* input, float* output,
                                      int numSamples, double sr,
                                      const float* ratios, int numRatios,
                                      int hopSize, int fftSize)
{
    if (numSamples == 0 || numRatios == 0)
        return;

    const int half = fftSize / 2 + 1;
    const float binToFreq = static_cast<float>(sr) / static_cast<float>(fftSize);
    const float norm      = 0.5f;  // 75% overlap Hann sum ≈ 2.0

    // Resize accum if needed
    const size_t accumLen = static_cast<size_t>(numSamples + fftSize);
    resizeIfSmaller(scratch_accum_, accumLen);
    std::fill(scratch_accum_.begin(), scratch_accum_.begin() + static_cast<ptrdiff_t>(accumLen), 0.0f);
    std::fill(scratch_prevPhase_.begin(), scratch_prevPhase_.begin() + half, 0.0f);
    std::fill(scratch_outPhase_.begin(),  scratch_outPhase_.begin()  + half, 0.0f);

    float* accum     = scratch_accum_.data();
    float* prevPhase = scratch_prevPhase_.data();
    float* outPhase  = scratch_outPhase_.data();
    float* frame     = scratch_frame_.data();
    const float* hann = hannWindow_.data();

    int frameIdx = 0;
    for (int pos = 0; pos + fftSize <= numSamples; pos += hopSize)
    {
        // Ratio for this frame (clamped to safe range)
        const float ratio = clamp(ratios[std::min(frameIdx, numRatios - 1)],
                                   0.125f, 8.0f);

        // --- Analysis ---
        SIMDKernels::vectorMul(frame, input + pos, hann, fftSize);
        std::memset(frame + fftSize, 0,
                    static_cast<size_t>(fftSize) * sizeof(float));
        fft_->performRealOnlyForwardTransform(frame);

        // --- Phase vocoder frequency scaling ---
        for (int k = 0; k < half; ++k)
        {
            const float real = frame[static_cast<size_t>(2 * k)];
            const float imag = frame[static_cast<size_t>(2 * k + 1)];
            const float mag  = std::sqrt(real * real + imag * imag);
            const float phase = std::atan2(imag, real);

            // Phase difference from previous frame
            float delta = phase - prevPhase[static_cast<size_t>(k)];

            // Expected phase advance for stationary sinusoid at bin k
            const float expected = 2.0f * kPi * static_cast<float>(hopSize * k)
                                   / static_cast<float>(fftSize);
            delta = princArg(delta - expected);

            // Instantaneous frequency (Hz)
            const float instFreq = (static_cast<float>(k)
                                    + delta / (2.0f * kPi)) * binToFreq;

            // New frequency after correction
            const float newFreq = instFreq * ratio;

            // Accumulate synthesis phase
            outPhase[static_cast<size_t>(k)] += 2.0f * kPi * newFreq
                                                 * static_cast<float>(hopSize)
                                                 / static_cast<float>(sr);

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

        ++frameIdx;
    }

    // Normalise and write
    for (int i = 0; i < numSamples; ++i)
        output[i] = accum[static_cast<size_t>(i)] * norm;
}

//==============================================================================
// flattenFormant  —  formant-preserving spectral shift
//==============================================================================

void SampleProcessor::flattenFormant(const float* input, float* output,
                                      int numSamples, double sr,
                                      const float* ratios, int numRatios,
                                      int hopSize, int fftSize)
{
    if (numSamples == 0 || numRatios == 0)
        return;

    const int half = fftSize / 2 + 1;
    const float binToFreq = static_cast<float>(sr) / static_cast<float>(fftSize);
    const float norm      = 0.5f;
    const int smoothWidth = std::max(3, half / 20);

    const size_t accumLen = static_cast<size_t>(numSamples + fftSize);
    resizeIfSmaller(scratch_accum_, accumLen);
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
    const float* hann  = hannWindow_.data();

    int frameIdx = 0;
    for (int pos = 0; pos + fftSize <= numSamples; pos += hopSize)
    {
        const float ratio = clamp(ratios[std::min(frameIdx, numRatios - 1)],
                                   0.125f, 8.0f);

        // --- Analysis ---
        SIMDKernels::vectorMul(frame, input + pos, hann, fftSize);
        std::memset(frame + fftSize, 0,
                    static_cast<size_t>(fftSize) * sizeof(float));
        fft_->performRealOnlyForwardTransform(frame);

        // Magnitude + phase tracking
        for (int k = 0; k < half; ++k)
        {
            const float real = frame[static_cast<size_t>(2 * k)];
            const float imag = frame[static_cast<size_t>(2 * k + 1)];
            mag[static_cast<size_t>(k)] = std::sqrt(real * real + imag * imag);

            float delta = std::atan2(imag, real) - prevPhase[static_cast<size_t>(k)];
            const float expected = 2.0f * kPi * static_cast<float>(hopSize * k)
                                   / static_cast<float>(fftSize);
            delta = princArg(delta - expected);

            const float instFreq = (static_cast<float>(k)
                                    + delta / (2.0f * kPi)) * binToFreq;
            const float newFreq = instFreq * ratio;

            outPhase[static_cast<size_t>(k)] += 2.0f * kPi * newFreq
                                                 * static_cast<float>(hopSize)
                                                 / static_cast<float>(sr);
            prevPhase[static_cast<size_t>(k)] = std::atan2(imag, real);
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
            const float finalMag = envelope[static_cast<size_t>(k)]
                                 * shiftedFine[static_cast<size_t>(k)];

            const float ph = outPhase[static_cast<size_t>(k)];
            frame[static_cast<size_t>(2 * k)]     = finalMag * std::cos(ph);
            frame[static_cast<size_t>(2 * k + 1)] = finalMag * std::sin(ph);
        }

        // --- Synthesis ---
        fft_->performRealOnlyInverseTransform(frame);
        for (int i = 0; i < fftSize; ++i)
            accum[static_cast<size_t>(pos + i)] += frame[static_cast<size_t>(i)];

        ++frameIdx;
    }

    for (int i = 0; i < numSamples; ++i)
        output[i] = accum[static_cast<size_t>(i)] * norm;
}

//==============================================================================
// flattenPitch  —  public API
//==============================================================================

std::vector<float> SampleProcessor::flattenPitch(const std::vector<float>& input,
                                                   double sampleRate,
                                                   int targetNote,
                                                   float smoothness,
                                                   bool preserveFormants)
{
    if (input.empty() || sampleRate <= 0.0)
        return {};

    // 1. Run pitch detection
    PitchDetectionResult detection = detectPitch(input, sampleRate);

    const int numFrames = detection.numFrames;
    if (numFrames == 0 || detection.detectedFreq <= 0.0f)
    {
        // No pitch detected — return input unchanged
        return input;
    }

    // Stall the const-correct FFT detour: ensure scratch buffers are sized
    // for the current fftSize_ (they already are from ctor / setFftSize).

    // 2. Determine target frequency
    if (targetNote < 0 || targetNote > 127)
        targetNote = detection.detectedMidiNote;

    const float targetFreq = midiNoteToFrequency(targetNote);

    // 3. Build per-frame correction ratios
    //    Pad ratio array to handle trailing frames (vocoder may run beyond
    //    pitch curve length).
    const int numVocoderFrames = (static_cast<int>(input.size()) + hopSize_) / hopSize_;
    std::vector<float> ratios(static_cast<size_t>(numVocoderFrames), 1.0f);

    for (int i = 0; i < numFrames; ++i)
    {
        const float detectedFreq = detection.pitchCurve[static_cast<size_t>(i)];
        if (detectedFreq > 0.0f && detection.confidenceCurve[static_cast<size_t>(i)] > 0.1f)
        {
            ratios[static_cast<size_t>(i)] = targetFreq / detectedFreq;
        }
        // else: ratio stays 1.0 (pass-through for unvoiced / low-confidence regions)
    }

    // 4. Apply smoothness: one-pole lowpass on ratio sequence
    if (smoothness > 0.01f)
    {
        // Smoothness 0 → pole ~0.5 (fast),  Smoothness 1 → pole ~0.97 (slow)
        const float pole = 1.0f - std::pow(0.01f, 1.0f / (1.0f + 20.0f * smoothness));
        float smoothed = ratios[0];
        for (int i = 0; i < numVocoderFrames; ++i)
        {
            smoothed = smoothed * (1.0f - pole) + ratios[static_cast<size_t>(i)] * pole;
            ratios[static_cast<size_t>(i)] = smoothed;
        }
    }

    // 5. Allocate output buffer
    std::vector<float> output(input.size());

    // 6. Apply correction
    if (preserveFormants)
    {
        flattenFormant(input.data(), output.data(),
                       static_cast<int>(input.size()), sampleRate,
                       ratios.data(), numVocoderFrames,
                       hopSize_, fftSize_);
    }
    else
    {
        flattenVocoder(input.data(), output.data(),
                       static_cast<int>(input.size()), sampleRate,
                       ratios.data(), numVocoderFrames,
                       hopSize_, fftSize_);
    }

    return output;
}

} // namespace ana
