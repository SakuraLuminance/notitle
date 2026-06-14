#include "NeuralStyleTransfer.h"
#include <algorithm>
#include <cstring>
#include <complex>

namespace ana {

//==============================================================================
// Construction
//==============================================================================

NeuralStyleTransfer::NeuralStyleTransfer()
{
    const int order = static_cast<int>(std::log2(static_cast<float>(fftSize_)));
    fft_ = std::make_unique<juce::dsp::FFT>(order);

    // Pre-compute Hann window
    windowTable_.resize(static_cast<size_t>(fftSize_));
    for (int i = 0; i < fftSize_; ++i)
    {
        windowTable_[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize_ - 1)));
    }
}

//==============================================================================
// Parameter setters
//==============================================================================

void NeuralStyleTransfer::setStrength(float amount)
{
    strength_ = std::max(0.0f, std::min(1.0f, amount));
}

void NeuralStyleTransfer::setPreserveTransients(bool preserve)
{
    preserveTransients_ = preserve;
}

void NeuralStyleTransfer::setIterations(int count)
{
    iterations_ = std::max(1, std::min(10, count));
}

void NeuralStyleTransfer::setSpectralSmoothness(float amount)
{
    spectralSmoothness_ = std::max(0.0f, std::min(1.0f, amount));
}

void NeuralStyleTransfer::setTemporalSmoothness(float amount)
{
    temporalSmoothness_ = std::max(0.0f, std::min(1.0f, amount));
}

void NeuralStyleTransfer::setSampleRate(double sr)
{
    sampleRate_ = std::max(8000.0, sr);
    hopSize_ = std::max(64, fftSize_ / 4);
}

void NeuralStyleTransfer::setFftSize(int size)
{
    // Round up to nearest power of two
    int powerOf2 = 2;
    while (powerOf2 < size)
        powerOf2 <<= 1;
    powerOf2 = std::min(powerOf2, 8192);

    fftSize_ = powerOf2;
    hopSize_ = fftSize_ / 4;

    const int order = static_cast<int>(std::log2(static_cast<float>(fftSize_)));
    fft_ = std::make_unique<juce::dsp::FFT>(order);

    // Recompute window
    windowTable_.resize(static_cast<size_t>(fftSize_));
    for (int i = 0; i < fftSize_; ++i)
    {
        windowTable_[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize_ - 1)));
    }
}

//==============================================================================
// Audio input
//==============================================================================

void NeuralStyleTransfer::setContent(const std::vector<float>& audio, double sampleRate)
{
    contentAudio_       = audio;
    contentSampleRate_  = sampleRate;
    contentAnalyzed_    = false;
}

void NeuralStyleTransfer::setStyle(const std::vector<float>& audio, double sampleRate)
{
    styleAudio_         = audio;
    styleSampleRate_    = sampleRate;
    styleAnalyzed_      = false;
}

//==============================================================================
// Mel scale helpers
//==============================================================================

float NeuralStyleTransfer::hzToMel(float hz)
{
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float NeuralStyleTransfer::melToHz(float mel)
{
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

//==============================================================================
// Mel filterbank construction
//==============================================================================

void NeuralStyleTransfer::buildMelFilterbank(
    int numBins, int numMelBands,
    std::vector<std::vector<float>>& filterbank)
{
    filterbank.clear();
    filterbank.resize(static_cast<size_t>(numMelBands));
    for (auto& fb : filterbank)
        fb.assign(static_cast<size_t>(numBins), 0.0f);

    const float hzMax   = static_cast<float>(sampleRate_) * 0.5f;
    const float melMax  = hzToMel(hzMax);
    const float melMin  = hzToMel(0.0f);

    // (numMelBands + 2) evenly-spaced points in mel scale
    const size_t numPoints = static_cast<size_t>(numMelBands) + 2;
    std::vector<float> melPoints(numPoints);
    for (size_t i = 0; i < numPoints; ++i)
    {
        melPoints[i] = melMin
            + (static_cast<float>(i) / static_cast<float>(numMelBands + 1))
            * (melMax - melMin);
    }

    // Hz -> FFT bin indices
    const float binFreqWidth = hzMax / static_cast<float>(numBins);
    std::vector<int> binIndices(numPoints);
    for (size_t i = 0; i < numPoints; ++i)
    {
        binIndices[i] = static_cast<int>(std::round(melToHz(melPoints[i]) / binFreqWidth));
        binIndices[i] = std::max(0, std::min(numBins - 1, binIndices[i]));
    }

    // Build triangular filters
    for (int m = 0; m < numMelBands; ++m)
    {
        const size_t mz = static_cast<size_t>(m);
        const int startBin  = binIndices[mz];
        const int centerBin = binIndices[mz + 1];
        const int endBin    = binIndices[mz + 2];

        // Ascending slope
        for (int k = startBin; k < centerBin && k < numBins; ++k)
        {
            const size_t kz = static_cast<size_t>(k);
            if (centerBin == startBin)
                filterbank[mz][kz] = 1.0f;
            else
                filterbank[mz][kz] = static_cast<float>(k - startBin)
                                   / static_cast<float>(centerBin - startBin);
        }

        // Descending slope
        for (int k = centerBin; k <= endBin && k < numBins; ++k)
        {
            const size_t kz = static_cast<size_t>(k);
            if (endBin == centerBin)
                filterbank[mz][kz] = 1.0f;
            else
                filterbank[mz][kz] = 1.0f - static_cast<float>(k - centerBin)
                                          / static_cast<float>(endBin - centerBin);
        }
    }
}

//==============================================================================
// STFT analysis
//==============================================================================

void NeuralStyleTransfer::stftAnalysis(
    const std::vector<float>& audio,
    std::vector<std::vector<float>>& mag,
    std::vector<std::vector<float>>& phase)
{
    const int numSamples = static_cast<int>(audio.size());
    const int numBins    = fftSize_ / 2 + 1;

    if (numSamples < fftSize_)
        return;

    const int numFrames = (numSamples - fftSize_) / hopSize_ + 1;

    mag.clear();
    phase.clear();
    mag.resize(static_cast<size_t>(numFrames));
    phase.resize(static_cast<size_t>(numFrames));

    // Scratch buffers (pre-allocated)
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize_), {0.0f, 0.0f});
    std::vector<float> scratch(static_cast<size_t>(fftSize_), 0.0f);

    for (int i = 0; i < numFrames; ++i)
    {
        const size_t iz = static_cast<size_t>(i);
        const int startPos = i * hopSize_;

        // Windowed frame copy via SIMD
        SIMDKernels::vectorMul(scratch.data(),
                                audio.data() + startPos,
                                windowTable_.data(),
                                fftSize_);

        // Zero the tail when the frame extends past the audio end
        const int remaining = fftSize_ - (numSamples - startPos);
        if (remaining > 0)
        {
            std::memset(scratch.data() + fftSize_ - remaining, 0,
                        static_cast<size_t>(remaining) * sizeof(float));
        }

        // Copy windowed data to spectrum (real part)
        for (int j = 0; j < fftSize_; ++j)
            spectrum[static_cast<size_t>(j)] = {scratch[static_cast<size_t>(j)], 0.0f};

        // Forward FFT
        fft_->perform(spectrum.data(), spectrum.data(), false);

        // Extract magnitude and phase (bins 0 .. fftSize/2)
        std::vector<float> magFrame(static_cast<size_t>(numBins));
        std::vector<float> phaseFrame(static_cast<size_t>(numBins));

        for (int k = 0; k < numBins; ++k)
        {
            const size_t kz = static_cast<size_t>(k);
            const float r = spectrum[kz].real();
            const float j = spectrum[kz].imag();
            magFrame[kz]   = std::sqrt(r * r + j * j);
            phaseFrame[kz] = std::atan2(j, r);
        }

        mag[iz]   = std::move(magFrame);
        phase[iz] = std::move(phaseFrame);
    }
}

//==============================================================================
// STFT synthesis
//==============================================================================

std::vector<float> NeuralStyleTransfer::stftSynthesis(
    const std::vector<std::vector<float>>& mag,
    const std::vector<std::vector<float>>& phase,
    int expectedLength)
{
    const int numFrames = static_cast<int>(mag.size());
    if (numFrames == 0)
        return {};

    const int numBins = fftSize_ / 2 + 1;

    // Compute output length: frames × hop + fft overlap tail
    const int outputLength = std::max(expectedLength,
                                      numFrames * hopSize_ + fftSize_);

    std::vector<float> output(static_cast<size_t>(outputLength), 0.0f);
    std::vector<float> overlap(static_cast<size_t>(outputLength), 0.0f);

    // Scratch buffers
    std::vector<std::complex<float>> spectrum(static_cast<size_t>(fftSize_), {0.0f, 0.0f});
    std::vector<float> scratch(static_cast<size_t>(fftSize_), 0.0f);
    std::vector<float> windowedOut(static_cast<size_t>(fftSize_), 0.0f);

    for (int i = 0; i < numFrames; ++i)
    {
        const size_t iz = static_cast<size_t>(i);
        const int writePos = i * hopSize_;

        // Reconstruct complex spectrum from magnitude and phase
        for (int k = 0; k < numBins; ++k)
        {
            const size_t kz = static_cast<size_t>(k);
            const float m = mag[iz][kz];
            const float p = phase[iz][kz];
            spectrum[kz] = std::complex<float>(m * std::cos(p), m * std::sin(p));
        }

        // Enforce conjugate symmetry for IFFT
        spectrum[0].imag(0.0f);
        spectrum[static_cast<size_t>(fftSize_ / 2)].imag(0.0f);

        for (int k = fftSize_ / 2 + 1; k < fftSize_; ++k)
        {
            const size_t kz        = static_cast<size_t>(k);
            const size_t mirrorK   = static_cast<size_t>(fftSize_ - k);
            spectrum[kz] = std::conj(spectrum[mirrorK]);
        }

        // Inverse FFT
        fft_->perform(spectrum.data(), spectrum.data(), true);

        // Extract real part from IFFT output and apply synthesis window
        for (int j = 0; j < fftSize_; ++j)
            scratch[static_cast<size_t>(j)] = spectrum[static_cast<size_t>(j)].real();

        SIMDKernels::vectorMul(windowedOut.data(),
                                scratch.data(),
                                windowTable_.data(),
                                fftSize_);

        for (int j = 0; j < fftSize_; ++j)
        {
            const int wp = writePos + j;
            if (wp >= 0 && wp < outputLength)
            {
                const size_t wpz = static_cast<size_t>(wp);
                output[wpz]  += windowedOut[static_cast<size_t>(j)];
                overlap[wpz] += windowTable_[static_cast<size_t>(j)];
            }
        }
    }

    // Normalize by accumulated overlap weight
    for (int i = 0; i < outputLength; ++i)
    {
        const size_t iz = static_cast<size_t>(i);
        if (overlap[iz] > 1e-6f)
            output[iz] /= overlap[iz];
    }

    return output;
}

//==============================================================================
// Histogram matching
//==============================================================================

void NeuralStyleTransfer::applyHistogramMatch(
    std::vector<float>& content,
    const std::vector<float>& style)
{
    if (content.empty() || style.empty())
        return;

    // Sort copies of both distributions
    std::vector<float> sortedContent = content;
    std::vector<float> sortedStyle   = style;
    std::sort(sortedContent.begin(), sortedContent.end());
    std::sort(sortedStyle.begin(),   sortedStyle.end());

    const float contentSizeF = static_cast<float>(content.size());
    const float styleSizeF   = static_cast<float>(style.size());

    // Map each content value to the style value at the same percentile
    for (size_t i = 0; i < content.size(); ++i)
    {
        // Find percentile of content[i] within the content distribution
        auto it = std::upper_bound(sortedContent.begin(), sortedContent.end(),
                                   content[i]);
        const float percentile = static_cast<float>(it - sortedContent.begin())
                               / contentSizeF;

        // Map to style value at the same percentile
        const size_t styleIdx = std::min(
            static_cast<size_t>(percentile * styleSizeF),
            style.size() - 1);

        content[i] = sortedStyle[styleIdx];
    }
}

//==============================================================================
// Spectral envelope extraction (cepstral liftering)
//==============================================================================

void NeuralStyleTransfer::extractSpectralEnvelope(
    const std::vector<std::vector<float>>& mag,
    std::vector<std::vector<float>>& envelope,
    int numCepstralBins)
{
    const int numFrames = static_cast<int>(mag.size());
    if (numFrames == 0)
        return;

    const int numBins = static_cast<int>(mag[0].size());

    envelope.clear();
    envelope.resize(static_cast<size_t>(numFrames));
    for (auto& env : envelope)
        env.resize(static_cast<size_t>(numBins));

    numCepstralBins = std::min(numCepstralBins, numBins);

    // Scratch buffer for cepstrum
    std::vector<float> cepstrum(static_cast<size_t>(numBins), 0.0f);

    for (int i = 0; i < numFrames; ++i)
    {
        const size_t iz = static_cast<size_t>(i);
        const auto& magFrame = mag[iz];
        auto& envFrame = envelope[iz];

        // --- 1. Log-magnitude spectrum ---
        std::vector<float> logMag(static_cast<size_t>(numBins));
        for (int k = 0; k < numBins; ++k)
            logMag[static_cast<size_t>(k)] = std::log(
                std::max(1e-10f, magFrame[static_cast<size_t>(k)]));

        // --- 2. Forward DCT-II (real cepstrum) ---
        // X_q = Σ_n x_n * cos(π * q * (n + 0.5) / N)
        for (int q = 0; q < numBins; ++q)
        {
            float sum = 0.0f;
            for (int n = 0; n < numBins; ++n)
            {
                sum += logMag[static_cast<size_t>(n)]
                     * std::cos(juce::MathConstants<float>::pi
                         * static_cast<float>(q)
                         * (static_cast<float>(n) + 0.5f)
                         / static_cast<float>(numBins));
            }
            cepstrum[static_cast<size_t>(q)] = sum;
        }

        // --- 3. Lifter: zero out high quefrencies ---
        for (int q = numCepstralBins; q < numBins; ++q)
            cepstrum[static_cast<size_t>(q)] = 0.0f;

        // --- 4. Inverse DCT-III ---
        // x_n = (1/N) * X_0 + (2/N) * Σ_q X_q * cos(π * q * (n + 0.5) / N)
        const float invN = 1.0f / static_cast<float>(numBins);
        for (int n = 0; n < numBins; ++n)
        {
            float sum = cepstrum[0] * 0.5f;  // DC term scaled by 1/2
            for (int q = 1; q < numBins; ++q)
            {
                sum += cepstrum[static_cast<size_t>(q)]
                     * std::cos(juce::MathConstants<float>::pi
                         * static_cast<float>(q)
                         * (static_cast<float>(n) + 0.5f)
                         / static_cast<float>(numBins));
            }

            // Exponentiate to get magnitude envelope
            // Apply offset correction: the DCT pair above yields a
            // reconstruction of the *log* spectrum multiplied by N/2,
            // so we divide by (N/2) to recover the log spectrum.
            envFrame[static_cast<size_t>(n)] = std::exp(sum * invN * 2.0f);
        }
    }
}

//==============================================================================
// Feature extraction
//==============================================================================

void NeuralStyleTransfer::extractFeatures(
    const std::vector<float>& audio,
    double sampleRate,
    StyleFeatures& features)
{
    const int numSamples = static_cast<int>(audio.size());
    if (numSamples < fftSize_)
        return;

    const int numBins    = fftSize_ / 2 + 1;
    const int numFrames  = (numSamples - fftSize_) / hopSize_ + 1;
    const float hzPerBin = static_cast<float>(sampleRate)
                         / static_cast<float>(fftSize_);

    // ------------------------------------------------------------------
    // STFT analysis
    // ------------------------------------------------------------------
    std::vector<std::vector<float>> mag, phase;
    stftAnalysis(audio, mag, phase);

    // ------------------------------------------------------------------
    // Mel filterbank (26 bands → 13 MFCC)
    // ------------------------------------------------------------------
    constexpr int numMelBands    = 26;
    constexpr int numMfccCoeffs  = 13;

    std::vector<std::vector<float>> melFilterbank;
    buildMelFilterbank(numBins, numMelBands, melFilterbank);

    // ------------------------------------------------------------------
    // Allocate per-frame feature vectors
    // ------------------------------------------------------------------
    features.spectralCentroid.resize(static_cast<size_t>(numFrames));
    features.spectralFlux.resize(static_cast<size_t>(numFrames));
    features.spectralRolloff.resize(static_cast<size_t>(numFrames));
    features.temporalEnvelope.resize(static_cast<size_t>(numFrames));
    features.zeroCrossingRate.resize(static_cast<size_t>(numFrames));
    features.mfcc.resize(static_cast<size_t>(numFrames * numMfccCoeffs));
    features.chroma.resize(static_cast<size_t>(numFrames * 12));

    // Scratch for previous-frame magnitudes (spectral flux)
    std::vector<float> prevMag(static_cast<size_t>(numBins), 0.0f);

    // ------------------------------------------------------------------
    // Per-frame feature extraction
    // ------------------------------------------------------------------
    for (int i = 0; i < numFrames; ++i)
    {
        const size_t iz   = static_cast<size_t>(i);
        const int startSample = i * hopSize_;
        const auto& magFrame = mag[iz];

        // -- Total spectral energy --
        float totalEnergy = 0.0f;
        for (int k = 0; k < numBins; ++k)
            totalEnergy += magFrame[static_cast<size_t>(k)];

        // ---- 1. Spectral centroid -----------------------------------
        float weightedSum = 0.0f;
        for (int k = 0; k < numBins; ++k)
        {
            weightedSum += static_cast<float>(k) * hzPerBin
                         * magFrame[static_cast<size_t>(k)];
        }
        features.spectralCentroid[iz] = (totalEnergy > 1e-10f)
            ? weightedSum / totalEnergy : 0.0f;

        // ---- 2. Spectral flux ---------------------------------------
        if (i > 0)
        {
            float flux = 0.0f;
            for (int k = 0; k < numBins; ++k)
            {
                const size_t kz = static_cast<size_t>(k);
                flux += std::abs(magFrame[kz] - prevMag[kz]);
            }
            features.spectralFlux[iz] = flux;
        }
        else
        {
            features.spectralFlux[iz] = 0.0f;
        }

        // ---- 3. Spectral rolloff (85% energy) -----------------------
        float cumEnergy = 0.0f;
        const float rolloffThreshold = totalEnergy * 0.85f;
        int rolloffBin = numBins - 1;
        for (int k = 0; k < numBins; ++k)
        {
            cumEnergy += magFrame[static_cast<size_t>(k)];
            if (cumEnergy >= rolloffThreshold)
            {
                rolloffBin = k;
                break;
            }
        }
        features.spectralRolloff[iz] = static_cast<float>(rolloffBin) * hzPerBin;

        // ---- 4. MFCC (mel-filterbank → log → DCT) ------------------
        std::vector<float> melEnergies(static_cast<size_t>(numMelBands), 0.0f);
        for (int m = 0; m < numMelBands; ++m)
        {
            double energy = 0.0;
            for (int k = 0; k < numBins; ++k)
            {
                energy += static_cast<double>(
                    magFrame[static_cast<size_t>(k)])
                    * melFilterbank[static_cast<size_t>(m)][static_cast<size_t>(k)];
            }
            melEnergies[static_cast<size_t>(m)] = (energy > 1e-10)
                ? static_cast<float>(std::log(energy)) : 0.0f;
        }

        // DCT-II to decorrelate → first 13 coefficients
        for (int c = 0; c < numMfccCoeffs; ++c)
        {
            float mfccVal = 0.0f;
            for (int m = 0; m < numMelBands; ++m)
            {
                mfccVal += melEnergies[static_cast<size_t>(m)]
                    * std::cos(juce::MathConstants<float>::pi
                        * static_cast<float>(c)
                        * (static_cast<float>(m) + 0.5f)
                        / static_cast<float>(numMelBands));
            }
            features.mfcc[iz * static_cast<size_t>(numMfccCoeffs)
                          + static_cast<size_t>(c)] = mfccVal;
        }

        // ---- 5. Chroma (12 pitch classes) ---------------------------
        constexpr float a4Freq = 440.0f;
        // Zero chroma bins for this frame
        for (int c = 0; c < 12; ++c)
            features.chroma[iz * 12 + static_cast<size_t>(c)] = 0.0f;

        for (int k = 0; k < numBins; ++k)
        {
            const float freq = static_cast<float>(k) * hzPerBin;
            // Restrict to musical range [C2..C7] ~ [65..2000] Hz
            if (freq < 65.0f || freq > 2000.0f)
                continue;

            const float midiNote = 12.0f * std::log2(freq / a4Freq) + 69.0f;
            int chromaBin = static_cast<int>(std::round(midiNote)) % 12;
            if (chromaBin < 0)
                chromaBin += 12;

            features.chroma[iz * 12 + static_cast<size_t>(chromaBin)]
                += magFrame[static_cast<size_t>(k)];
        }

        // ---- 6. Temporal envelope (RMS) -----------------------------
        float rmsSum = 0.0f;
        const int frameEnd = std::min(startSample + fftSize_, numSamples);
        for (int j = startSample; j < frameEnd; ++j)
            rmsSum += audio[static_cast<size_t>(j)]
                    * audio[static_cast<size_t>(j)];
        features.temporalEnvelope[iz] = std::sqrt(
            rmsSum / static_cast<float>(frameEnd - startSample));

        // ---- 7. Zero-crossing rate ----------------------------------
        int zeroCrossings = 0;
        const int zcrEnd = std::min(startSample + fftSize_, numSamples);
        for (int j = startSample + 1; j < zcrEnd; ++j)
        {
            if ((audio[static_cast<size_t>(j - 1)] >= 0.0f)
                != (audio[static_cast<size_t>(j)] >= 0.0f))
            {
                ++zeroCrossings;
            }
        }
        features.zeroCrossingRate[iz] = static_cast<float>(zeroCrossings)
                                      / static_cast<float>(zcrEnd - startSample);

        // Store current magnitudes for next frame's flux
        std::copy(magFrame.begin(), magFrame.end(), prevMag.begin());
    }

    // ------------------------------------------------------------------
    // Aggregate features
    // ------------------------------------------------------------------

    // -- avgBrightness: mean spectral centroid --
    {
        double brightnessSum = 0.0;
        for (int i = 0; i < numFrames; ++i)
            brightnessSum += features.spectralCentroid[static_cast<size_t>(i)];
        features.avgBrightness = static_cast<float>(
            brightnessSum / static_cast<double>(numFrames));
    }

    // -- avgWarmth: ratio of energy below 500 Hz to total --
    {
        const int warmthBin = std::min(
            static_cast<int>(500.0f / hzPerBin), numBins - 1);
        double lowFreqEnergy  = 0.0;
        double totalFreqEnergy = 0.0;
        for (int i = 0; i < numFrames; ++i)
        {
            const auto& magFrame = mag[static_cast<size_t>(i)];
            for (int k = 0; k < numBins; ++k)
            {
                const float m = magFrame[static_cast<size_t>(k)];
                totalFreqEnergy += static_cast<double>(m);
                if (k <= warmthBin)
                    lowFreqEnergy += static_cast<double>(m);
            }
        }
        features.avgWarmth = (totalFreqEnergy > 1e-10)
            ? static_cast<float>(lowFreqEnergy / totalFreqEnergy) : 0.0f;
    }

    // -- roughness: std-dev of spectral flux --
    {
        if (numFrames > 1)
        {
            double fluxMean = 0.0;
            for (int i = 1; i < numFrames; ++i)
                fluxMean += features.spectralFlux[static_cast<size_t>(i)];
            fluxMean /= static_cast<double>(numFrames - 1);

            double fluxVar = 0.0;
            for (int i = 1; i < numFrames; ++i)
            {
                const double diff = features.spectralFlux[static_cast<size_t>(i)]
                                  - fluxMean;
                fluxVar += diff * diff;
            }
            features.roughness = static_cast<float>(
                std::sqrt(fluxVar / static_cast<double>(numFrames - 1)));
        }
    }
}

//==============================================================================
// Feature distribution matching (reserved)
//==============================================================================

std::vector<float> NeuralStyleTransfer::matchFeatureDistribution(
    const std::vector<float>& content,
    const std::vector<float>& style)
{
    if (content.empty() || style.empty())
        return content;

    std::vector<float> result = content;
    applyHistogramMatch(result, style);
    return result;
}

//==============================================================================
// Public analysis
//==============================================================================

StyleFeatures NeuralStyleTransfer::extractStyle()
{
    if (!styleAudio_.empty() && !styleAnalyzed_)
    {
        const double sr = (styleSampleRate_ > 0.0) ? styleSampleRate_ : sampleRate_;
        extractFeatures(styleAudio_, sr, styleFeatures_);
        styleAnalyzed_ = true;
    }
    return styleFeatures_;
}

StyleFeatures NeuralStyleTransfer::getContentFeatures() const
{
    return contentFeatures_;
}

//==============================================================================
// Main style-transfer processing (full-band)
//==============================================================================

std::vector<float> NeuralStyleTransfer::process()
{
    if (contentAudio_.empty())
        return {};

    // -- Ensure content features are extracted --
    if (!contentAnalyzed_ && !contentAudio_.empty())
    {
        const double sr = (contentSampleRate_ > 0.0) ? contentSampleRate_ : sampleRate_;
        extractFeatures(contentAudio_, sr, contentFeatures_);
        contentAnalyzed_ = true;
    }

    // -- Ensure style features are extracted --
    if (!styleAnalyzed_ && !styleAudio_.empty())
    {
        const double sr = (styleSampleRate_ > 0.0) ? styleSampleRate_ : sampleRate_;
        extractFeatures(styleAudio_, sr, styleFeatures_);
        styleAnalyzed_ = true;
    }

    // If there is no style audio, return content unchanged
    if (styleAudio_.empty())
        return contentAudio_;

    // ==================================================================
    // STFT analysis: content + style
    // ==================================================================
    std::vector<std::vector<float>> contentMag, contentPhase;
    stftAnalysis(contentAudio_, contentMag, contentPhase);

    std::vector<std::vector<float>> styleMag, stylePhase;
    stftAnalysis(styleAudio_, styleMag, stylePhase);

    const int numContentFrames = static_cast<int>(contentMag.size());
    const int numStyleFrames   = static_cast<int>(styleMag.size());

    if (numContentFrames == 0 || numStyleFrames == 0)
        return contentAudio_;

    const int numBins = fftSize_ / 2 + 1;

    // ==================================================================
    // Spectral envelope extraction
    // ==================================================================
    // Number of cepstral coefficients: higher = more detail, lower = smoother
    // Map spectralSmoothness_ [0,1] → cepstral bins [40..4]
    const int numCepstralBins = std::max(4, static_cast<int>(
        (1.0f - spectralSmoothness_) * 36.0f + 4.0f));

    std::vector<std::vector<float>> styleEnvelope;
    extractSpectralEnvelope(styleMag, styleEnvelope, numCepstralBins);

    std::vector<std::vector<float>> contentEnvelope;
    extractSpectralEnvelope(contentMag, contentEnvelope, numCepstralBins);

    // Working copy of content magnitude (will be iteratively modified)
    std::vector<std::vector<float>> resultMag = contentMag;
    std::vector<std::vector<float>> resultPhase = contentPhase;

    // ==================================================================
    // Multi-iteration style transfer
    // ==================================================================
    for (int iter = 0; iter < iterations_; ++iter)
    {
        // Compute spectral envelope for the current iteration's result
        std::vector<std::vector<float>> currentEnvelope;
        extractSpectralEnvelope(resultMag, currentEnvelope, numCepstralBins);

        const float iterStrength   = strength_
            * (1.0f - 0.3f * static_cast<float>(iter) / static_cast<float>(iterations_));
        const float histBlend      = (iter == iterations_ - 1) ? strength_ * 0.5f : 0.0f;

        for (int i = 0; i < numContentFrames; ++i)
        {
            const size_t iz = static_cast<size_t>(i);

            // Map content frame to nearest style frame
            const int styleIdx = std::min(
                static_cast<int>(static_cast<float>(i)
                    * static_cast<float>(numStyleFrames)
                    / static_cast<float>(numContentFrames)),
                numStyleFrames - 1);
            const size_t siz = static_cast<size_t>(styleIdx);

            auto&       resultMagFrame   = resultMag[iz];
            const auto& contentMagFrame  = contentMag[iz];
            const auto& contentEnvFrame  = contentEnvelope[iz];
            const auto& styleEnvFrame    = styleEnvelope[siz];
            const auto& currentEnvFrame  = currentEnvelope[iz];

            // (a) Envelope replacement preserving content fine structure
            for (int k = 0; k < numBins; ++k)
            {
                const size_t kz = static_cast<size_t>(k);

                // Fine structure = magnitude / envelope ratio
                float fineStructure = 1.0f;
                if (currentEnvFrame[kz] > 1e-10f)
                    fineStructure = resultMagFrame[kz] / currentEnvFrame[kz];

                // Apply style envelope with content's fine structure
                const float styledMag = fineStructure * styleEnvFrame[kz];

                // Blend toward styled magnitude
                resultMagFrame[kz] = (1.0f - iterStrength) * resultMagFrame[kz]
                                   + iterStrength * styledMag;
            }

            // (b) Histogram matching (last iteration only)
            if (histBlend > 1e-6f)
            {
                std::vector<float> contentFlat = resultMagFrame;
                const auto& styleFlat = styleMag[siz];
                applyHistogramMatch(contentFlat, styleFlat);

                for (int k = 0; k < numBins; ++k)
                {
                    const size_t kz = static_cast<size_t>(k);
                    resultMagFrame[kz] = (1.0f - histBlend) * resultMagFrame[kz]
                                       + histBlend * contentFlat[kz];
                }
            }
        }

        // Update envelope for the next iteration
        if (iter < iterations_ - 1)
            contentEnvelope = currentEnvelope;
    }

    // ==================================================================
    // Transient preservation
    // ==================================================================
    if (preserveTransients_ && numContentFrames > 1)
    {
        const float transientMix = strength_ * 0.3f;

        for (int i = 1; i < numContentFrames; ++i)
        {
            const size_t iz     = static_cast<size_t>(i);
            const size_t prevIz = static_cast<size_t>(i - 1);

            // Detection: spectral flux of the result magnitudes
            float flux = 0.0f;
            for (int k = 0; k < numBins; ++k)
            {
                const size_t kz = static_cast<size_t>(k);
                flux += std::abs(resultMag[iz][kz] - resultMag[prevIz][kz]);
            }

            // If flux is high (transient onset), blend toward original phase
            const float fluxThreshold = 0.1f * static_cast<float>(numBins);
            const float transientAmount = std::min(1.0f,
                flux / (fluxThreshold * 3.0f));

            if (transientAmount > 1e-6f)
            {
                for (int k = 0; k < numBins; ++k)
                {
                    const size_t kz = static_cast<size_t>(k);
                    const float origPhase = contentPhase[iz][kz];
                    const float curPhase  = resultPhase[iz][kz];

                    // Complex-domain phase blending
                    const float phaseDiff = origPhase - curPhase;
                    const float wrappedDiff = std::atan2(
                        std::sin(phaseDiff), std::cos(phaseDiff));

                    resultPhase[iz][kz] += wrappedDiff * transientAmount * transientMix;

                    // Wrap to [-π, π]
                    resultPhase[iz][kz] = std::atan2(
                        std::sin(resultPhase[iz][kz]),
                        std::cos(resultPhase[iz][kz]));
                }
            }
        }
    }

    // ==================================================================
    // Temporal smoothing
    // ==================================================================
    if (temporalSmoothness_ > 0.0f && numContentFrames > 1)
    {
        const float smoothFactor = temporalSmoothness_ * 0.5f;

        for (int i = 1; i < numContentFrames; ++i)
        {
            const size_t iz     = static_cast<size_t>(i);
            const size_t prevIz = static_cast<size_t>(i - 1);

            for (int k = 0; k < numBins; ++k)
            {
                const size_t kz = static_cast<size_t>(k);
                resultMag[iz][kz] = (1.0f - smoothFactor) * resultMag[iz][kz]
                                  + smoothFactor * resultMag[prevIz][kz];
            }
        }
    }

    // ==================================================================
    // ISTFT reconstruction
    // ==================================================================
    const int expectedLength = static_cast<int>(contentAudio_.size());
    return stftSynthesis(resultMag, resultPhase, expectedLength);
}

//==============================================================================
// PartialDataSIMD processing
//==============================================================================

void NeuralStyleTransfer::process(PartialDataSIMD& partials)
{
    // -- Ensure style features are available --
    if (!styleAnalyzed_ && !styleAudio_.empty())
    {
        const double sr = (styleSampleRate_ > 0.0) ? styleSampleRate_ : sampleRate_;
        extractFeatures(styleAudio_, sr, styleFeatures_);
        styleAnalyzed_ = true;
    }

    // Nothing to do without style data
    if (!styleAnalyzed_)
        return;

    // -- Quick additive synthesis from partials (one hop of audio) --
    const int bufLen = hopSize_;
    std::vector<float> partialAudio(static_cast<size_t>(bufLen), 0.0f);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float freq   = partials.frequency[i];
        const float amp    = partials.amplitude[i];
        const float ph     = partials.phase[i];

        if (amp < 1e-6f)
            continue;

        const float twoPiFreqOverSr = juce::MathConstants<float>::twoPi
                                    * freq / static_cast<float>(sampleRate_);

        for (int s = 0; s < bufLen; ++s)
        {
            partialAudio[static_cast<size_t>(s)] += amp * std::sin(
                twoPiFreqOverSr * static_cast<float>(s) + ph);
        }
    }

    // -- Save and temporarily replace content --
    const auto savedContent      = contentAudio_;
    const auto savedContentSr    = contentSampleRate_;
    const auto savedAnalyzed     = contentAnalyzed_;
    const auto savedFeatures     = contentFeatures_;

    contentAudio_      = std::move(partialAudio);
    contentSampleRate_ = sampleRate_;
    contentAnalyzed_   = false;

    // Run the full style transfer
    std::vector<float> processed = process();

    // Restore original content
    contentAudio_       = savedContent;
    contentSampleRate_  = savedContentSr;
    contentAnalyzed_    = savedAnalyzed;
    contentFeatures_    = savedFeatures;

    if (processed.empty())
        return;

    // -- Analyze the processed audio --
    std::vector<std::vector<float>> procMag, procPhase;
    stftAnalysis(processed, procMag, procPhase);

    if (procMag.empty())
        return;

    const int   numBins   = fftSize_ / 2 + 1;
    const float hzPerBin  = static_cast<float>(sampleRate_)
                          / static_cast<float>(fftSize_);

    // Average processed magnitude across the first few frames for stability
    const int numAvgFrames = std::min(3, static_cast<int>(procMag.size()));
    std::vector<float> avgMag(static_cast<size_t>(numBins), 0.0f);

    for (int i = 0; i < numAvgFrames; ++i)
    {
        for (int k = 0; k < numBins; ++k)
            avgMag[static_cast<size_t>(k)]
                += procMag[static_cast<size_t>(i)][static_cast<size_t>(k)];
    }
    if (numAvgFrames > 0)
    {
        const float invCount = 1.0f / static_cast<float>(numAvgFrames);
        for (int k = 0; k < numBins; ++k)
            avgMag[static_cast<size_t>(k)] *= invCount;
    }

    // -- Apply spectral shaping back to partial amplitudes --
    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float freq = partials.frequency[i];
        const int bin = static_cast<int>(freq / hzPerBin);

        if (bin >= 0 && bin < numBins)
        {
            const float processedMag = avgMag[static_cast<size_t>(bin)];
            const float originalAmp  = partials.amplitude[i];

            partials.amplitude[i] = (1.0f - strength_) * originalAmp
                                  + strength_ * processedMag;
        }
    }
}

//==============================================================================
// Reset
//==============================================================================

void NeuralStyleTransfer::reset()
{
    contentAudio_.clear();
    styleAudio_.clear();

    contentFeatures_   = StyleFeatures{};
    styleFeatures_     = StyleFeatures{};
    contentAnalyzed_   = false;
    styleAnalyzed_     = false;

    sampleRate_            = 44100.0;
    strength_              = 0.5f;
    preserveTransients_    = true;
    iterations_            = 3;
    spectralSmoothness_    = 0.3f;
    temporalSmoothness_    = 0.3f;
    fftSize_               = 2048;
    hopSize_               = 512;

    const int order = static_cast<int>(std::log2(static_cast<float>(fftSize_)));
    fft_ = std::make_unique<juce::dsp::FFT>(order);

    windowTable_.resize(static_cast<size_t>(fftSize_));
    for (int i = 0; i < fftSize_; ++i)
    {
        windowTable_[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(fftSize_ - 1)));
    }
}

} // namespace ana
