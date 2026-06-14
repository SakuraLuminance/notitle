#include "NeuralUpsampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ana {

//==============================================================================
//  Internal helpers (anonymous namespace)
//==============================================================================
namespace {

/** Clamp a value to [lo, hi]. */
template <typename T>
constexpr T clamp(T val, T lo, T hi) noexcept
{
    return (val < lo) ? lo : (val > hi) ? hi : val;
}

/** Compute the Kaiser window value for index k with half-length M and beta. */
inline double kaiserWindow(int k, int M, double beta) noexcept
{
    if (k < -M || k > M)
        return 0.0;

    const double arg = static_cast<double>(k) / static_cast<double>(M);
    const double t = 1.0 - arg * arg;
    // Safe: t is in [0, 1] for k in [-M, M]
    const double sqrtT = std::sqrt(std::max(t, 0.0));
    // Modified Bessel I0 via truncated series (enough for beta <= 12)
    const double betaSqrtT = beta * sqrtT;

    // Compute I0(x) using the series expansion
    auto i0 = [](double x) -> double
    {
        double sum = 1.0;
        double term = 1.0;
        for (int n = 1; n <= 25; ++n)
        {
            term *= (x / (2.0 * n)) * (x / (2.0 * n));
            sum += term;
            if (term < 1e-12 * sum)
                break;
        }
        return sum;
    };

    return i0(betaSqrtT) / i0(beta);
}

/** Write a zero-stuffed frame for convolution.
    Fills dest[0..destLen-1] with input samples spaced M apart.
    Used by polyphase interpolation. */
inline void zeroStuffFrame(const float* src, int srcLen,
                           float* dest, int destLen, int M) noexcept
{
    std::memset(dest, 0, static_cast<size_t>(destLen) * sizeof(float));
    for (int i = 0; i < srcLen; ++i)
    {
        const int pos = i * M;
        if (pos < destLen)
            dest[pos] = src[i];
    }
}

/** Simple 1-pole high-pass filter coefficient for noise shaping. */
constexpr double kNoiseShaperHP = 0.85;

/** Minimum amplitude threshold for partial activity. */
constexpr float kPartialAmpThresh = 1e-8f;

} // namespace

//==============================================================================
//  Construction / reset
//==============================================================================

NeuralUpsampler::NeuralUpsampler()
{
    reset();
}

void NeuralUpsampler::reset()
{
    inputAudio_.clear();
    inputSampleRate_     = 44100.0;
    inputBitDepth_       = 16;
    quality_             = 3;
    targetSampleRate_    = 44100.0;
    bandwidth_           = 0.9f;
    harmonicEnhancement_ = 0.3f;
    noiseReduction_      = 0.3f;
    transientPreservation_ = 0.7f;

    scratch_.clear();
    scratch2_.clear();
    windowBuf_.clear();
    filterBuf_.clear();
    olaBuffer_.clear();
    olaWritePos_ = 0;

    fft_.reset();
    fftSize_ = 2048;

    cachedFilter_.clear();
    cachedFilterInputSr_  = 0.0;
    cachedFilterOutputSr_ = 0.0;
    cachedFilterQuality_  = -1;
}

//==============================================================================
//  Setters
//==============================================================================

void NeuralUpsampler::setInput(const std::vector<float>& audio,
                                double sampleRate, int bitDepth)
{
    inputAudio_       = audio;
    inputSampleRate_  = sampleRate;
    inputBitDepth_    = juce::jlimit(8, 32, bitDepth);
}

void NeuralUpsampler::setQuality(int quality)
{
    quality_ = juce::jlimit(1, 5, quality);
}

void NeuralUpsampler::setTargetSampleRate(double sr)
{
    targetSampleRate_ = std::max(8000.0, sr);
}

void NeuralUpsampler::setBandwidth(float factor)
{
    bandwidth_ = clamp(factor, 0.0f, 1.0f);
}

void NeuralUpsampler::setHarmonicEnhancement(float amount)
{
    harmonicEnhancement_ = clamp(amount, 0.0f, 1.0f);
}

void NeuralUpsampler::setNoiseReduction(float amount)
{
    noiseReduction_ = clamp(amount, 0.0f, 1.0f);
}

void NeuralUpsampler::setTransientPreservation(float amount)
{
    transientPreservation_ = clamp(amount, 0.0f, 1.0f);
}

double NeuralUpsampler::getOutputSampleRate() const
{
    return targetSampleRate_;
}

int NeuralUpsampler::getOutputBitDepth() const
{
    return inputBitDepth_;
}

//==============================================================================
//  designKaiserSinc — lowpass prototype filter
//==============================================================================

std::vector<float> NeuralUpsampler::designKaiserSinc(int halfLength,
                                                      double cutoff,
                                                      double beta)
{
    const int numTaps = halfLength * 2 + 1;
    std::vector<float> filter(static_cast<size_t>(numTaps));
    double sum = 0.0;

    for (int i = 0; i < numTaps; ++i)
    {
        const int k = i - halfLength;          // symmetric around centre
        const double w = kaiserWindow(k, halfLength, beta);

        // Sinc: sin(pi * cutoff * k) / (pi * k), with L'Hospital at k=0
        double sinc;
        if (k == 0)
            sinc = 2.0 * cutoff;               // limit of sin(pi*cutoff*k)/(pi*k)
        else
            sinc = std::sin(juce::MathConstants<double>::pi * cutoff * k)
                 / (juce::MathConstants<double>::pi * k);

        filter[static_cast<size_t>(i)] = static_cast<float>(sinc * w);
        sum += sinc * w;
    }

    // Normalise to unity gain at DC
    if (sum > 0.0)
    {
        const float invSum = static_cast<float>(1.0 / sum);
        for (auto& f : filter)
            f *= invSum;
    }

    return filter;
}

//==============================================================================
//  sincInterpolate — band-limited sample-rate conversion
//==============================================================================

std::vector<float> NeuralUpsampler::sincInterpolate(
    const std::vector<float>& input, double inputSr, double outputSr)
{
    const int inputLen = static_cast<int>(input.size());
    if (inputLen == 0)
        return {};

    // No conversion needed
    if (std::abs(outputSr - inputSr) < 1.0)
        return input;

    const double ratio = outputSr / inputSr;
    const int outputLen = static_cast<int>(std::ceil(static_cast<double>(inputLen) * ratio)) + 4;

    // Determine kernel half-length based on quality
    // quality 1 → 8 taps, quality 5 → 64 taps
    static constexpr int kHalfLengths[5] = { 8, 16, 32, 48, 64 };
    const int halfLen = kHalfLengths[juce::jlimit(0, 4, quality_ - 1)];

    // Kaiser beta: higher quality = higher beta (wider main lobe, lower sidelobes)
    static constexpr double kKaiserBetas[5] = { 3.0, 4.5, 6.0, 7.5, 9.0 };
    const double kaiserBeta = kKaiserBetas[juce::jlimit(0, 4, quality_ - 1)];

    // Cutoff frequency: we allow up to min(inputSr, outputSr) * bandwidth_ / 2
    const double cutoffRatio = 0.5 * std::min(1.0, ratio) * bandwidth_;

    // Ensure the buffer is large enough
    scratch_.resize(static_cast<size_t>(outputLen), 0.0f);

    // For each output sample, convolve with the windowed sinc kernel
    for (int n = 0; n < outputLen; ++n)
    {
        // Continuous input position
        const double inPos = static_cast<double>(n) / ratio;
        const int centreIdx = static_cast<int>(std::floor(inPos));
        const double frac = inPos - static_cast<double>(centreIdx);

        double sum = 0.0;
        double wsum = 0.0;

        for (int k = -halfLen; k <= halfLen; ++k)
        {
            const int sampleIdx = centreIdx + k;
            if (sampleIdx < 0 || sampleIdx >= inputLen)
                continue;

            // Evaluate sinc at (frac - k)
            const double t = frac - static_cast<double>(k);
            const double arg = juce::MathConstants<double>::pi * cutoffRatio * t;
            double sincVal;
            if (std::abs(t) < 1e-8)
                sincVal = 2.0 * cutoffRatio;  // limit as t→0
            else
                sincVal = std::sin(arg) / arg;

            // Kaiser window
            const double win = kaiserWindow(static_cast<int>(std::floor(t * halfLen + 0.5)),
                                             halfLen, kaiserBeta);
            // Actually evaluate window at continuous position:
            // normalised to [-halfLen, halfLen]
            const double tNorm = t * static_cast<double>(halfLen);
            const int tInt = static_cast<int>(std::floor(tNorm + 0.5));
            const double windowVal = kaiserWindow(tInt, halfLen, kaiserBeta);

            sum += static_cast<double>(input[static_cast<size_t>(sampleIdx)])
                 * sincVal * windowVal;
            wsum += windowVal;
        }

        if (wsum > 0.0)
            scratch_[static_cast<size_t>(n)] = static_cast<float>(sum / wsum);
        else
            scratch_[static_cast<size_t>(n)] = 0.0f;
    }

    // Trim to exact expected length
    std::vector<float> result(outputLen);
    std::memcpy(result.data(), scratch_.data(),
                static_cast<size_t>(outputLen) * sizeof(float));
    return result;
}

//==============================================================================
//  computeSpectralEnvelope — cepstral smoothing
//==============================================================================

void NeuralUpsampler::computeSpectralEnvelope(
    const std::vector<float>& magSpectrum,
    int cepstrumOrder,
    std::vector<float>& envelope) const
{
    const int halfSize = static_cast<int>(magSpectrum.size());
    if (halfSize == 0)
        return;

    envelope.resize(static_cast<size_t>(halfSize));

    // 1. Log magnitude spectrum
    scratch_.resize(static_cast<size_t>(halfSize));
    for (int i = 0; i < halfSize; ++i)
    {
        const float val = magSpectrum[static_cast<size_t>(i)];
        scratch_[static_cast<size_t>(i)] = std::log(std::max(val, 1e-10f));
    }

    // 2. RFFT to get cepstrum (treat log-magnitude as real signal)
    const int cepstrumFFTSize = juce::nextPowerOfTwo(halfSize * 2);
    scratch2_.resize(static_cast<size_t>(cepstrumFFTSize), 0.0f);
    std::memcpy(scratch2_.data(), scratch_.data(),
                static_cast<size_t>(halfSize) * sizeof(float));

    juce::dsp::FFT fftCep(juce::nextPowerOfTwo(cepstrumFFTSize / 2)); // log2
    fftCep.performRealOnlyForwardTransform(scratch2_.data());

    // 3. Truncate cepstrum (low-time liftering)
    // Keep first cepstrumOrder coefficients, zero the rest
    const int truncateIdx = juce::jlimit(2, cepstrumFFTSize / 2, cepstrumOrder);
    for (int i = truncateIdx; i < cepstrumFFTSize / 2; ++i)
    {
        scratch2_[static_cast<size_t>(i * 2)]     = 0.0f;     // real
        scratch2_[static_cast<size_t>(i * 2 + 1)] = 0.0f;     // imag
    }

    // 4. Inverse FFT (reuse forward by conjugating symmetric bins)
    fftCep.performRealOnlyInverseTransform(scratch2_.data());

    // 5. Exponentiate to get envelope
    for (int i = 0; i < halfSize; ++i)
    {
        const float val = scratch2_[static_cast<size_t>(i)]
                         / static_cast<float>(cepstrumFFTSize);
        envelope[static_cast<size_t>(i)] = std::exp(std::max(val, -20.0f));
    }
}

//==============================================================================
//  estimateF0 — normalized autocorrelation
//==============================================================================

float NeuralUpsampler::estimateF0(const std::vector<float>& audio,
                                   double sampleRate) const
{
    const int len = static_cast<int>(audio.size());
    if (len < 100)
        return 0.0f;

    // Limits for F0 search (30 Hz to 1500 Hz)
    const int minLag = static_cast<int>(sampleRate / 1500.0 + 0.5);
    const int maxLag = static_cast<int>(sampleRate / 30.0 + 0.5);
    const int searchLen = std::min(len / 2, maxLag);

    if (searchLen <= minLag)
        return 0.0f;

    // Compute RMS for normalisation
    double rms = 0.0;
    for (int i = 0; i < len; ++i)
        rms += static_cast<double>(audio[static_cast<size_t>(i)])
             * static_cast<double>(audio[static_cast<size_t>(i)]);
    rms = std::sqrt(rms / static_cast<double>(len));
    if (rms < 1e-6)
        return 0.0f;

    // Normalised autocorrelation
    double bestCorr = 0.0;
    int bestLag = 0;

    for (int lag = minLag; lag < searchLen; ++lag)
    {
        double corr = 0.0;
        double normA = 0.0;
        double normB = 0.0;

        for (int i = 0; i < len - lag; ++i)
        {
            const double a = static_cast<double>(audio[static_cast<size_t>(i)]);
            const double b = static_cast<double>(audio[static_cast<size_t>(i + lag)]);
            corr += a * b;
            normA += a * a;
            normB += b * b;
        }

        const double denom = std::sqrt(std::max(normA * normB, 1e-10));
        const double r = corr / denom;

        if (r > bestCorr)
        {
            bestCorr = r;
            bestLag = lag;
        }
    }

    if (bestLag > 0 && bestCorr > 0.3)
        return static_cast<float>(sampleRate / static_cast<double>(bestLag));

    return 0.0f;
}

//==============================================================================
//  computeSpectralSlope
//==============================================================================

float NeuralUpsampler::computeSpectralSlope(
    const std::vector<float>& magSpectrum,
    int startBin, int endBin) const
{
    const int nBins = endBin - startBin;
    if (nBins < 2)
        return 0.0f;

    // Linear regression on log-magnitude vs bin index
    double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
    for (int i = startBin; i < endBin; ++i)
    {
        const double x = static_cast<double>(i);
        const double y = std::log(std::max(
            static_cast<double>(magSpectrum[static_cast<size_t>(i)]), 1e-10));
        sumX  += x;
        sumY  += y;
        sumXX += x * x;
        sumXY += x * y;
    }

    const double n = static_cast<double>(nBins);
    const double denom = n * sumXX - sumX * sumX;
    if (std::abs(denom) < 1e-10)
        return 0.0f;

    return static_cast<float>((n * sumXY - sumX * sumY) / denom);
}

//==============================================================================
//  lpcExtrapolate — autocorrelation LPC with Levinson-Durbin
//==============================================================================

std::vector<float> NeuralUpsampler::lpcExtrapolate(
    const std::vector<float>& lowBand, int numSamples, int order)
{
    const int inputLen = static_cast<int>(lowBand.size());
    if (inputLen < order + 2 || numSamples <= 0)
        return {};

    order = juce::jlimit(2, std::min(inputLen / 2, 64), order);

    // --- Step 1: Autocorrelation (biased estimate) ---
    std::vector<double> R(static_cast<size_t>(order) + 1, 0.0);
    for (int i = 0; i <= order; ++i)
    {
        double sum = 0.0;
        for (int n = 0; n < inputLen - i; ++n)
            sum += static_cast<double>(lowBand[static_cast<size_t>(n)])
                 * static_cast<double>(lowBand[static_cast<size_t>(n + i)]);
        R[static_cast<size_t>(i)] = sum / static_cast<double>(inputLen);
    }

    // --- Step 2: Levinson-Durbin recursion ---
    std::vector<double> a(static_cast<size_t>(order) + 1, 0.0);
    a[0] = 1.0;
    std::vector<double> aPrev(static_cast<size_t>(order) + 1, 0.0);
    double E = R[0];

    for (int i = 1; i <= order; ++i)
    {
        // Reflection coefficient
        double k = R[static_cast<size_t>(i)];
        for (int j = 1; j < i; ++j)
            k -= a[static_cast<size_t>(j)] * R[static_cast<size_t>(i - j)];
        k /= E;

        // Update filter coefficients
        aPrev = a;
        a[static_cast<size_t>(i)] = k;
        for (int j = 1; j < i; ++j)
            a[static_cast<size_t>(j)] = aPrev[static_cast<size_t>(j)]
                                      - k * aPrev[static_cast<size_t>(i - j)];

        E *= (1.0 - k * k);
        if (E < 1e-12)
            break;
    }

    // --- Step 3: Forward prediction ---
    std::vector<float> result(static_cast<size_t>(numSamples), 0.0f);

    // Copy the last 'order' samples as the starting state
    for (int i = 0; i < numSamples; ++i)
    {
        double pred = 0.0;
        for (int j = 1; j <= order; ++j)
        {
            int sampleIdx = inputLen - 1 + i - j;
            double sampleVal;
            if (sampleIdx >= inputLen)
                sampleVal = static_cast<double>(result[static_cast<size_t>(sampleIdx - inputLen)]);
            else if (sampleIdx >= 0)
                sampleVal = static_cast<double>(lowBand[static_cast<size_t>(sampleIdx)]);
            else
                continue;

            pred -= a[static_cast<size_t>(j)] * sampleVal;
        }

        // Add a small noise term scaled by residual energy
        const double noise = (static_cast<double>(std::rand()) / static_cast<double>(RAND_MAX) - 0.5)
                           * 2.0 * std::sqrt(std::max(E, 1e-10)) * 0.05;
        result[static_cast<size_t>(i)] = static_cast<float>(pred + noise);
    }

    return result;
}

//==============================================================================
//  enhanceHarmonics — F0-driven harmonic regeneration
//==============================================================================

void NeuralUpsampler::enhanceHarmonics(std::vector<float>& audio,
                                        double sampleRate)
{
    if (audio.empty() || harmonicEnhancement_ < 0.01f)
        return;

    // Estimate F0
    const float f0 = estimateF0(audio, sampleRate);
    if (f0 < 20.0f)
        return;

    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const int nHarmonics = static_cast<int>(nyquist / f0);
    if (nHarmonics < 2)
        return;

    const int len = static_cast<int>(audio.size());
    const float mix = harmonicEnhancement_;

    // Generate harmonics using sine wave summation
    // Use a simple additive synthesis approach
    scratch_.resize(static_cast<size_t>(len), 0.0f);

    // Harmonic decay factor (higher harmonics have lower amplitude)
    // Quality influences how many harmonics we generate
    const int maxHarmonics = juce::jlimit(2, nHarmonics, 5 * quality_);
    const float decay = 1.0f / static_cast<float>(maxHarmonics);

    for (int h = 1; h <= maxHarmonics; ++h)
    {
        const float freq = f0 * static_cast<float>(h);
        if (freq >= nyquist)
            break;

        // Amplitude follows a 1/h decay with slight random variation
        const float amp = decay / static_cast<float>(h);

        // Phase offset to reduce comb-filtering artefacts
        const float phaseOffset = 0.0f; // 2pi * random per harmonic

        for (int n = 0; n < len; ++n)
        {
            const double t = static_cast<double>(n) / sampleRate;
            const double val = static_cast<double>(amp)
                             * std::sin(juce::MathConstants<double>::twoPi
                                      * static_cast<double>(freq) * t);
            scratch_[static_cast<size_t>(n)] += static_cast<float>(val);
        }
    }

    // Mix harmonic content with original
    // Use envelope-following to avoid over-amplifying quiet sections
    for (int n = 0; n < len; ++n)
    {
        const float orig = audio[static_cast<size_t>(n)];
        const float harm = scratch_[static_cast<size_t>(n)];
        audio[static_cast<size_t>(n)] = orig * (1.0f - mix) + harm * mix;
    }
}

//==============================================================================
//  applyNoiseShaping — error-feedback noise shaper
//==============================================================================

void NeuralUpsampler::applyNoiseShaping(std::vector<float>& audio,
                                         int targetBitDepth)
{
    if (audio.empty())
        return;

    // Higher bit depth = less need for noise shaping
    // Only shape when reducing below 24 bits
    if (targetBitDepth >= 24)
        return;

    // Calculate quantisation step for target bit depth
    // Assume full-scale range is [-1, 1]
    const int nLevels = 1 << targetBitDepth;
    const float step = 2.0f / static_cast<float>(nLevels);
    const float halfStep = step * 0.5f;

    const int len = static_cast<int>(audio.size());

    // First-order high-pass noise shaping
    // Error feedback: shape[s] = error[s-1] * kNoiseShaperHP
    float prevError = 0.0f;

    for (int i = 0; i < len; ++i)
    {
        const float input = audio[static_cast<size_t>(i)];

        // Add shaped noise from previous error
        const float shaped = input + static_cast<float>(kNoiseShaperHP) * prevError;

        // Quantize
        const float quantised = std::round(shaped / step) * step;

        // Compute error with noise reduction applied
        const float error = shaped - quantised;
        prevError = error * (1.0f - noiseReduction_ * 0.5f);

        audio[static_cast<size_t>(i)] = quantised;
    }
}

//==============================================================================
//  spectralUpsample — frequency-domain high-frequency reconstruction
//==============================================================================

std::vector<float> NeuralUpsampler::spectralUpsample(
    const std::vector<float>& input, double inputSr, double outputSr)
{
    const int inputLen = static_cast<int>(input.size());
    if (inputLen < 64)
        return {};

    const double ratio = outputSr / inputSr;

    // FFT size depends on quality
    static constexpr int kFFTSizes[5] = { 512, 1024, 2048, 2048, 4096 };
    const int fftSize = kFFTSizes[juce::jlimit(0, 4, quality_ - 1)];
    const int hopSize = fftSize / 4;                            // 75% overlap
    const int halfSize = fftSize / 2;

    // Ensure FFT is allocated
    if (!fft_ || fftSize_ != fftSize)
    {
        fft_ = std::make_unique<juce::dsp::FFT>(juce::nextPowerOfTwo(fftSize / 2));
        fftSize_ = fftSize;
    }

    // Pre-compute window
    windowBuf_.resize(static_cast<size_t>(fftSize));
    juce::dsp::WindowingFunction<float> winFunc(
        static_cast<size_t>(fftSize),
        juce::dsp::WindowingFunction<float>::WindowingMethod::hann);
    winFunc.multiplyWithWindowingTable(windowBuf_.data(), fftSize);

    // Cepstrum order for envelope extraction — quality dependent
    // Lower quality = smoother (fewer coefficients)
    static constexpr int kCepstrumOrders[5] = { 8, 12, 20, 30, 50 };
    const int cepstrumOrder = kCepstrumOrders[juce::jlimit(0, 4, quality_ - 1)];

    // How many HF bins to extrapolate
    // At ratio=1 (no upsampling), we still add harmonic enhancement
    // At ratio>1, we extend spectrum by ratio
    // We never extend beyond the FFT Nyquist bin
    const int knownBins = halfSize;  // We use the full known spectrum
    const int extrapolatedBins = 0;  // All bins are "known" since we FFT the input

    // --- STFT processing ---
    // Scratch buffers
    scratch_.resize(static_cast<size_t>(fftSize));
    scratch2_.resize(static_cast<size_t>(fftSize));

    // Magnitude/phase for current frame
    std::vector<float> mag(static_cast<size_t>(halfSize));
    std::vector<float> phase(static_cast<size_t>(halfSize));
    std::vector<float> envelope(static_cast<size_t>(halfSize));
    std::vector<float> magScaled(static_cast<size_t>(halfSize));

    // Output buffer (will be upsampled at the end)
    const int outputLen = static_cast<int>(std::ceil(
        static_cast<double>(inputLen) * ratio)) + fftSize;
    std::vector<float> output(static_cast<size_t>(outputLen), 0.0f);

    // Analysis: process each frame of the input
    const int nFrames = std::max(1, (inputLen - fftSize) / hopSize + 1);

    for (int frame = 0; frame < nFrames; ++frame)
    {
        const int frameStart = frame * hopSize;

        // 1. Window the frame
        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = frameStart + i;
            const float sample = (idx < inputLen)
                ? input[static_cast<size_t>(idx)]
                : 0.0f;
            scratch_[static_cast<size_t>(i)] = sample * windowBuf_[static_cast<size_t>(i)];
        }

        // 2. FFT
        std::memcpy(scratch2_.data(), scratch_.data(),
                    static_cast<size_t>(fftSize) * sizeof(float));
        fft_->performRealOnlyForwardTransform(scratch2_.data());

        // 3. Extract magnitude and phase
        // JUCE packs as [r0, rN/2, r1, i1, r2, i2, ...]
        // Bin 0 (DC) = scratch[0], Bin Nyquist = scratch[1]
        // Bins 1..half-1: scratch[2*i], scratch[2*i+1]
        mag[0] = std::abs(scratch2_[0]);       // DC
        phase[0] = (scratch2_[0] >= 0.0f) ? 0.0f : juce::MathConstants<float>::pi;

        for (int i = 1; i < halfSize; ++i)
        {
            const float re = scratch2_[static_cast<size_t>(2 * i)];
            const float im = scratch2_[static_cast<size_t>(2 * i + 1)];
            mag[static_cast<size_t>(i)] = std::sqrt(re * re + im * im);
            phase[static_cast<size_t>(i)] = std::atan2(im, re);
        }

        // 4. Spectral envelope estimation
        computeSpectralEnvelope(mag, cepstrumOrder, envelope);

        // 5. Compute spectral slope and extrapolate to higher bins
        //    (only if we have room in the FFT — for upsampling we're using
        //     the original sample rate's FFT, so we fill in missing HF)
        const int slopeStart = halfSize - halfSize / 4;
        const int slopeEnd = halfSize - 2;
        const float slope = computeSpectralSlope(mag, slopeStart, slopeEnd);

        // 6. Magnitude scaling: apply envelope and slope-based extrapolation
        //    For bins that exist, blend original with envelope
        for (int i = 0; i < halfSize; ++i)
        {
            const float envVal = envelope[static_cast<size_t>(i)];
            const float magVal = mag[static_cast<size_t>(i)];

            // Envelope-weighted: trust the envelope more for noisy bins
            // Use transient preservation to decide how much to smooth
            const float envWeight = 0.3f * (1.0f - transientPreservation_);
            const float origWeight = 1.0f - envWeight;

            // If noise reduction is active, attenuate bins below envelope
            float noiseGate = 1.0f;
            if (noiseReduction_ > 0.0f && envVal > 0.0f)
            {
                const float ratioInEnv = magVal / envVal;
                if (ratioInEnv < 0.1f)
                    noiseGate = 0.0f + noiseReduction_ * ratioInEnv * 10.0f;
            }

            magScaled[static_cast<size_t>(i)] = (magVal * origWeight + envVal * envWeight) * noiseGate;
        }

        // 7. Harmonic regeneration on the spectral magnitude
        //    Identify harmonic peaks from the F0 and boost them
        if (harmonicEnhancement_ > 0.01f)
        {
            const float f0 = estimateF0(input, inputSr);
            if (f0 > 20.0f)
            {
                const float binWidth = static_cast<float>(inputSr) / static_cast<float>(fftSize);
                const int maxHarmonic = static_cast<int>(static_cast<float>(halfSize) * 0.5f);
                const float hAmount = harmonicEnhancement_ * 0.2f;

                for (int h = 1; h <= maxHarmonic; ++h)
                {
                    const float hz = f0 * static_cast<float>(h);
                    const int bin = static_cast<int>(hz / binWidth + 0.5f);
                    if (bin >= halfSize)
                        break;

                    // Boost bins near harmonic locations
                    for (int nb = -1; nb <= 1; ++nb)
                    {
                        const int binIdx = bin + nb;
                        if (binIdx > 0 && binIdx < halfSize)
                        {
                            const float boost = 1.0f + hAmount * (1.0f - static_cast<float>(std::abs(nb)) * 0.5f);

                            // Only boost if the existing magnitude is not already dominant
                            const float existing = magScaled[static_cast<size_t>(binIdx)];
                            const float envVal = envelope[static_cast<size_t>(binIdx)];
                            if (existing < envVal * 1.5f)
                                magScaled[static_cast<size_t>(binIdx)] = existing * boost;
                        }
                    }
                }
            }
        }

        // 8. ISTFT: reconstruct spectrum and inverse transform
        // DC bin
        scratch2_[0] = magScaled[0];
        scratch2_[1] = 0.0f;  // Nyquist (keep clean)

        for (int i = 1; i < halfSize; ++i)
        {
            const float cosP = std::cos(phase[static_cast<size_t>(i)]);
            const float sinP = std::sin(phase[static_cast<size_t>(i)]);
            const float m = magScaled[static_cast<size_t>(i)];
            scratch2_[static_cast<size_t>(2 * i)]     = m * cosP;
            scratch2_[static_cast<size_t>(2 * i + 1)] = m * sinP;
        }

        // 9. Inverse FFT
        fft_->performRealOnlyInverseTransform(scratch2_.data());

        // 10. Overlap-add to output at target sample rate
        //     Since we're processing at input SR, we map output position
        const double outStart = static_cast<double>(frameStart) * ratio;
        const int outStartIdx = static_cast<int>(outStart);

        for (int i = 0; i < fftSize; ++i)
        {
            const int outIdx = outStartIdx + i;
            if (outIdx >= 0 && outIdx < outputLen)
            {
                // Window again for OLA (Hann satisfies COLA with 75% overlap)
                const float winVal = windowBuf_[static_cast<size_t>(i)];
                output[static_cast<size_t>(outIdx)] += scratch2_[static_cast<size_t>(i)] * winVal;
            }
        }
    }

    // Normalise OLA output (Hann + 75% overlap = gain of hopSize/fftSize * N)
    const float olaGain = 1.0f / static_cast<float>(fftSize) * 2.0f;
    for (auto& s : output)
        s *= olaGain;

    // Trim trailing zeros
    int lastNonZero = outputLen - 1;
    while (lastNonZero > 0 && std::abs(output[static_cast<size_t>(lastNonZero)]) < 1e-8f)
        --lastNonZero;

    output.resize(static_cast<size_t>(lastNonZero + 1));
    return output;
}

//==============================================================================
//  process — standard time-domain upsampling
//==============================================================================

std::vector<float> NeuralUpsampler::process()
{
    if (inputAudio_.empty())
        return {};

    // Step 1: Band-limited sample rate conversion
    auto upsampled = sincInterpolate(inputAudio_, inputSampleRate_, targetSampleRate_);

    // Step 2: Spectral upsampling for high-frequency reconstruction
    // Only apply when the output sample rate is higher than input
    if (targetSampleRate_ > inputSampleRate_ * 1.1 && bandwidth_ > 0.1f)
    {
        auto enhanced = spectralUpsample(inputAudio_, inputSampleRate_, targetSampleRate_);

        // Blend: keep the sinc-interpolated low frequencies, add spectral HF
        // The spectral upsampler output is at the target SR with potentially
        // better HF content. We lowpass the sinc result and highpass the spectral result.
        if (!enhanced.empty())
        {
            // Simple blend: spectral output provides HF extension
            // Cross-fade around the original Nyquist frequency
            const double crossoverHz = inputSampleRate_ * 0.45;
            const double blendWidth = inputSampleRate_ * 0.1;

            // Ensure same length
            const int commonLen = std::min(static_cast<int>(upsampled.size()),
                                           static_cast<int>(enhanced.size()));
            upsampled.resize(static_cast<size_t>(commonLen));
            enhanced.resize(static_cast<size_t>(commonLen));

            // Simple crossover filter: DC-through blend
            // Below crossover: trust sincInterpolate
            // Above crossover: trust spectralUpsample
            // This is done per-sample in frequency domain... for time domain,
            // we do a complementary crossfade in frequency domain via a simple
            // weighted sum where spectral contribution increases with frequency.
            //
            // Practical approach: apply a gentle crossfade in time domain
            // using a soft knee around the crossover
            const float blend = bandwidth_ * 0.5f;
            for (int i = 0; i < commonLen; ++i)
            {
                // Blend increases linearly from 0 to blend over the output range
                // Higher indices correspond to higher frequencies in an FFT sense,
                // but in time domain this is a crude approximation.
                // Better: blend entire spectral output at a fixed ratio
                const float spectralMix = blend;
                upsampled[static_cast<size_t>(i)] =
                    upsampled[static_cast<size_t>(i)] * (1.0f - spectralMix)
                  + enhanced[static_cast<size_t>(i)] * spectralMix;
            }
        }
    }

    // Step 3: Harmonic enhancement
    if (harmonicEnhancement_ > 0.01f)
        enhanceHarmonics(upsampled, targetSampleRate_);

    // Step 4: Noise shaping for bit depth reduction
    if (noiseReduction_ > 0.01f)
        applyNoiseShaping(upsampled, inputBitDepth_);

    return upsampled;
}

//==============================================================================
//  process(PartialDataSIMD&) — spectral upsampling on partial data
//==============================================================================

void NeuralUpsampler::process(PartialDataSIMD& partials)
{
    if (partials.activeCount == 0)
        return;

    const float nyquist = static_cast<float>(partials.sampleRate) * 0.5f;

    // Build frequency-sorted active partial list
    std::vector<int> activeIdxs;
    activeIdxs.reserve(static_cast<size_t>(partials.activeCount));

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (partials.isActive(i))
            activeIdxs.push_back(i);
    }

    if (activeIdxs.empty())
        return;

    // Sort by frequency
    std::sort(activeIdxs.begin(), activeIdxs.end(),
        [&partials](int a, int b) {
            return partials.frequency[a] < partials.frequency[b];
        });

    const int nActive = static_cast<int>(activeIdxs.size());

    // --- Step 1: Spectral envelope estimation from partial amplitudes ---
    // Build a pseudo-spectrum from the partial data, then extract envelope
    constexpr int kEnvSize = 256;
    std::vector<float> pseudoSpec(static_cast<size_t>(kEnvSize), 0.0f);
    std::vector<float> env(static_cast<size_t>(kEnvSize), 0.0f);

    const float binWidth = nyquist / static_cast<float>(kEnvSize);

    for (int i = 0; i < nActive; ++i)
    {
        const int idx = activeIdxs[static_cast<size_t>(i)];
        const float freq = partials.frequency[idx];
        const float amp  = partials.amplitude[idx];
        const int bin = juce::jlimit(0, kEnvSize - 1,
            static_cast<int>(freq / binWidth));
        pseudoSpec[static_cast<size_t>(bin)] = std::max(
            pseudoSpec[static_cast<size_t>(bin)], amp);
    }

    // Smooth the pseudo-spectrum with a 3-point moving average
    std::vector<float> smoothed(static_cast<size_t>(kEnvSize), 0.0f);
    for (int i = 1; i < kEnvSize - 1; ++i)
    {
        smoothed[static_cast<size_t>(i)] =
            (pseudoSpec[static_cast<size_t>(i - 1)]
           + pseudoSpec[static_cast<size_t>(i)]
           + pseudoSpec[static_cast<size_t>(i + 1)]) * 0.333f;
    }
    smoothed[0] = pseudoSpec[0];
    smoothed[static_cast<size_t>(kEnvSize - 1)] = pseudoSpec[static_cast<size_t>(kEnvSize - 1)];

    // Cepstral-like envelope: keep peaks
    const int cepstrumOrder = 8 + quality_ * 4;
    computeSpectralEnvelope(smoothed, cepstrumOrder, env);

    // --- Step 2: Interpolate partial amplitudes based on envelope ---
    for (int i = 0; i < nActive; ++i)
    {
        const int idx = activeIdxs[static_cast<size_t>(i)];
        const float freq = partials.frequency[idx];
        const int bin = juce::jlimit(0, kEnvSize - 1,
            static_cast<int>(freq / binWidth));

        // Smooth the amplitude toward the envelope
        const float envVal = env[static_cast<size_t>(bin)];
        const float origAmp = partials.amplitude[idx];
        const float blend = 0.3f * (1.0f - transientPreservation_);

        // Only adjust if envelope suggests a significant difference
        if (envVal > kPartialAmpThresh && origAmp > kPartialAmpThresh)
        {
            const float ratio = origAmp / envVal;
            if (ratio > 2.0f)
            {
                // Partial is too strong — reduce toward envelope
                partials.amplitude[idx] = origAmp * (1.0f - blend)
                                        + envVal * blend;
            }
            else if (ratio < 0.5f && noiseReduction_ > 0.0f)
            {
                // Partial is too weak — likely noise, reduce further
                const float nr = noiseReduction_;
                partials.amplitude[idx] *= (1.0f - nr * 0.5f);
            }
        }
    }

    // --- Step 3: Generate additional partials at higher frequencies ---
    // Find the highest-frequency active partial
    float highestFreq = 0.0f;
    for (int i = 0; i < nActive; ++i)
    {
        const int idx = activeIdxs[static_cast<size_t>(i)];
        highestFreq = std::max(highestFreq, partials.frequency[idx]);
    }

    // If we have room and the highest partial is below Nyquist, generate filler
    if (highestFreq < nyquist * 0.8f && harmonicEnhancement_ > 0.01f)
    {
        // Use the spectral slope to estimate amplitudes for new partials
        const int slopeStart = kEnvSize * 3 / 4;
        const int slopeEnd = kEnvSize - 2;
        const float slope = computeSpectralSlope(env, slopeStart, slopeEnd);

        // How many new partials to add (quality-dependent)
        const int newPartials = quality_ * 2;

        // Find available slots (inactive indices)
        std::vector<int> freeSlots;
        for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        {
            if (!partials.isActive(i))
                freeSlots.push_back(i);
        }

        if (!freeSlots.empty())
        {
            const int toAdd = std::min(newPartials, static_cast<int>(freeSlots.size()));

            for (int p = 0; p < toAdd; ++p)
            {
                // Frequency: uniformly spaced from highestFreq to Nyquist * bandwidth_
                const float t = static_cast<float>(p + 1) / static_cast<float>(toAdd + 1);
                const float newFreq = highestFreq + (nyquist * bandwidth_ - highestFreq) * t;

                // Amplitude: extrapolated from spectral envelope using slope
                const int bin = std::min(kEnvSize - 1,
                    static_cast<int>(newFreq / binWidth));
                const float binCentre = (static_cast<float>(bin) + 0.5f) * binWidth;

                // Extrapolate amplitude using slope
                const float lastEnv = env[static_cast<size_t>(std::min(kEnvSize - 1, bin - 1))];
                const float extrapAmp = lastEnv * std::exp(
                    slope * (newFreq - binCentre));

                const int slotIdx = freeSlots[static_cast<size_t>(p)];
                partials.frequency[slotIdx] = newFreq;
                partials.amplitude[slotIdx] = std::max(0.0f,
                    std::min(1.0f, extrapAmp * harmonicEnhancement_ * 0.3f));
                partials.phase[slotIdx] = juce::MathConstants<float>::twoPi
                                        * (static_cast<float>(std::rand())
                                         / static_cast<float>(RAND_MAX));
            }

            // Update active mask after adding partials
            partials.updateActiveMask();
        }
    }

    // --- Step 4: Noise reduction on low-amplitude partials ---
    if (noiseReduction_ > 0.01f)
    {
        for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
        {
            if (!partials.isActive(i))
                continue;

            const float amp = partials.amplitude[i];

            // If amplitude is very low and frequency is high, it's likely noise
            if (amp < 0.01f * (1.0f + noiseReduction_ * 3.0f))
            {
                // Reduce or remove
                if (noiseReduction_ > 0.5f)
                {
                    partials.amplitude[i] = 0.0f;
                }
                else
                {
                    partials.amplitude[i] *= (1.0f - noiseReduction_);
                }
            }
        }
        partials.updateActiveMask();
    }
}

} // namespace ana
