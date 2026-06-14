#include "FrequencyShaper.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "SIMDSupport.h"

namespace ana {

// ============================================================================
// Construction
// ============================================================================

FrequencyShaper::FrequencyShaper() = default;

FrequencyShaper::~FrequencyShaper() = default;

// ============================================================================
// Parameter setters
// ============================================================================

void FrequencyShaper::setType(ShaperType type)      { type_ = type; }
void FrequencyShaper::setAmount(float amount)        { amount_ = std::clamp(amount, 0.0f, 1.0f); }
void FrequencyShaper::setThreshold(float freqHz)     { threshold_ = std::max(0.0f, freqHz); }
void FrequencyShaper::setFoldBoundary(float freqHz)  { foldBoundary_ = std::max(100.0f, freqHz); }
void FrequencyShaper::setQuantization(float steps)   { quantization_ = std::clamp(steps, 1.0f, 128.0f); }
void FrequencyShaper::setCenterFrequency(float freqHz) { centerFrequency_ = std::max(20.0f, freqHz); }
void FrequencyShaper::setResonance(float Q)          { resonance_ = std::clamp(Q, 0.5f, 20.0f); }
void FrequencyShaper::setBandwidth(float octaves)    { bandwidth_ = std::clamp(octaves, 0.1f, 4.0f); }
void FrequencyShaper::setFormantShift(float semitones) { formantShift_ = std::clamp(semitones, -12.0f, 12.0f); }
void FrequencyShaper::setFormantAmount(float amount) { formantAmount_ = std::clamp(amount, 0.0f, 1.0f); }
void FrequencyShaper::setHarmonicOrder(int order)    { harmonicOrder_ = std::clamp(order, 2, 5); }
void FrequencyShaper::setHarmonicMix(float mix)      { harmonicMix_ = std::clamp(mix, 0.0f, 1.0f); }
void FrequencyShaper::setShiftAmount(float hz)       { shiftAmount_ = std::clamp(hz, -2000.0f, 2000.0f); }
void FrequencyShaper::setPhaseWarp(float amount)     { phaseWarp_ = std::clamp(amount, 0.0f, 1.0f); }
void FrequencyShaper::setPhaseModFreq(float freqHz)  { phaseModFreq_ = std::max(0.0f, freqHz); }

// ============================================================================
// Reset
// ============================================================================

void FrequencyShaper::reset()
{
    phaseModPhase_ = 0.0;
}

// ============================================================================
// Main processing dispatch
// ============================================================================

void FrequencyShaper::process(PartialDataSIMD& partials)
{
    sampleRate_ = partials.sampleRate;

    switch (type_)
    {
        case ShaperType::Saturate:        processSaturate(partials);        break;
        case ShaperType::Fold:            processFold(partials);            break;
        case ShaperType::Bitcrush:        processBitcrush(partials);        break;
        case ShaperType::Resonant:        processResonant(partials);        break;
        case ShaperType::FormantShift:    processFormantShift(partials);    break;
        case ShaperType::HarmonicExciter: processHarmonicExciter(partials); break;
        case ShaperType::FrequencyShift:  processFrequencyShift(partials);  break;
        case ShaperType::PhaseDistortion: processPhaseDistortion(partials); break;
    }
}

void FrequencyShaper::processAudio(juce::AudioBuffer<float>& buffer, double sampleRate)
{
    sampleRate_ = sampleRate;

    if (type_ == ShaperType::FrequencyShift)
        processFrequencyShiftAudio(buffer, sampleRate);
    // All other types operate on partials only — audio passes through unmodified
}

// ============================================================================
// Saturate - Soft clip frequencies above threshold
// ============================================================================

void FrequencyShaper::processSaturate(PartialDataSIMD& partials)
{
    if (amount_ < 1e-6f)
        return;

    const float nyquist = static_cast<float>(partials.sampleRate * 0.5);
    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float f = partials.frequency[i];

        if (f > threshold_)
        {
            // Soft saturation: compress excess above threshold using
            //   f_out = threshold + (f - threshold) / (1 + |f - threshold| / (nyquist - threshold) * amount)
            const float excess = f - threshold_;
            const float range  = nyquist - threshold_;

            if (range > 1.0f)
            {
                const float ratio = excess / range;        // 0..1 above threshold
                const float compressed = excess / (1.0f + ratio * amount_);
                partials.frequency[i] = threshold_ + compressed;
            }
        }
    }
}

// ============================================================================
// Fold - Mirror frequencies that exceed fold boundary
// ============================================================================

void FrequencyShaper::processFold(PartialDataSIMD& partials)
{
    if (amount_ < 1e-6f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    // Number of folds scales with amount (1 to 4 folds)
    const int numFolds = 1 + static_cast<int>(amount_ * 3.0f);

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        float f = partials.frequency[i];

        if (f > foldBoundary_)
        {
            for (int fold = 0; fold < numFolds; ++fold)
            {
                f = foldBoundary_ - std::abs(f - foldBoundary_);
                if (f < 0.0f)
                    f = -f;
                if (f <= foldBoundary_)
                    break;
            }
        }

        partials.frequency[i] = std::max(0.0f, f);
    }
}

// ============================================================================
// Bitcrush - Quantize frequencies to grid
// ============================================================================

void FrequencyShaper::processBitcrush(PartialDataSIMD& partials)
{
    if (amount_ < 1e-6f)
        return;

    const float nyquist = static_cast<float>(partials.sampleRate * 0.5);
    const float step = std::max(1.0f, nyquist / quantization_);

    // Interpolate between original and quantized based on amount
    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float f    = partials.frequency[i];
        const float fq   = std::round(f / step) * step;
        partials.frequency[i] = f + (fq - f) * amount_;
    }
}

// ============================================================================
// Resonant - Amplify partials near center frequency and harmonics
// ============================================================================

void FrequencyShaper::processResonant(PartialDataSIMD& partials)
{
    if (amount_ < 1e-6f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    // Resonance coefficient from Q: higher Q = sharper peak
    const float gainScale = resonance_ * 0.5f * amount_;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float f = partials.frequency[i];
        if (f < 1.0f)
            continue;

        // Gaussian weight around center frequency (log-frequency domain)
        const float octDist = std::log2(f / centerFrequency_);
        const float weight  = std::exp(-0.5f * (octDist / bandwidth_) * (octDist / bandwidth_));

        // Additional resonance at harmonics 2x, 3x, 4x, 5x
        float harmWeight = 0.0f;
        for (int h = 2; h <= 5; ++h)
        {
            const float hOctDist = std::log2(f / (centerFrequency_ * static_cast<float>(h)));
            const float hw = std::exp(-0.5f * (hOctDist / bandwidth_) * (hOctDist / bandwidth_));
            harmWeight += hw / static_cast<float>(h); // higher harmonics get less weight
        }

        const float totalWeight = weight + harmWeight * 0.5f;
        const float gain = 1.0f + totalWeight * gainScale;

        partials.amplitude[i] = std::min(1.0f, partials.amplitude[i] * gain);
    }
}

// ============================================================================
// FormantShift - Shift spectral envelope by semitones
// ============================================================================

void FrequencyShaper::processFormantShift(PartialDataSIMD& partials)
{
    if (std::abs(formantShift_) < 0.01f || formantAmount_ < 1e-6f)
        return;

    const float nyquist = static_cast<float>(partials.sampleRate * 0.5);
    const float ratio   = std::pow(2.0f, formantShift_ / 12.0f);

    // Blend between original and shifted based on formantAmount
    const float blend = formantAmount_;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float f      = partials.frequency[i];
        const float fShift = f * ratio;
        const float fClamp = std::clamp(fShift, 0.0f, nyquist);

        // Blend original with shifted
        partials.frequency[i] = f + (fClamp - f) * blend;
    }
}

// ============================================================================
// HarmonicExciter - Generate harmonics from existing partials
// ============================================================================

void FrequencyShaper::processHarmonicExciter(PartialDataSIMD& partials)
{
    if (amount_ < 1e-6f || harmonicMix_ < 1e-6f)
        return;

    const float nyquist = static_cast<float>(partials.sampleRate * 0.5);
    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    // Count active partials to know how much room we have
    int activeCount = partials.activeCount;
    int available = kMax - activeCount;

    if (available <= 0)
        return;

    const int hOrder = harmonicOrder_;
    const float mix  = harmonicMix_ * amount_;

    // Find first empty slot
    int emptyIdx = 0;
    while (emptyIdx < kMax && partials.isActive(emptyIdx))
        ++emptyIdx;

    // Generate harmonic clones
    for (int i = 0; i < kMax && available > 0; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float srcFreq = partials.frequency[i];
        const float srcAmp  = partials.amplitude[i];
        const float srcPhase = partials.phase[i];

        // Generate harmonic
        const float harmonicFreq = srcFreq * static_cast<float>(hOrder);

        if (harmonicFreq > 0.0f && harmonicFreq < nyquist)
        {
            partials.frequency[emptyIdx] = harmonicFreq;
            partials.amplitude[emptyIdx] = srcAmp * (mix / static_cast<float>(hOrder));
            partials.phase[emptyIdx]     = srcPhase * static_cast<float>(hOrder);

            --available;

            // Advance to next empty slot
            ++emptyIdx;
            while (emptyIdx < kMax && partials.isActive(emptyIdx))
                ++emptyIdx;
        }

        if (emptyIdx >= kMax)
            break;
    }

    // Rebuild active mask to include newly added partials
    partials.updateActiveMask();
}

// ============================================================================
// FrequencyShift (partials) - Shift all frequencies by constant offset
// ============================================================================

void FrequencyShaper::processFrequencyShift(PartialDataSIMD& partials)
{
    if (std::abs(shiftAmount_) < 0.5f)
        return;

    const float nyquist = static_cast<float>(partials.sampleRate * 0.5);
    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float f = partials.frequency[i] + shiftAmount_;
        partials.frequency[i] = std::clamp(f, 0.0f, nyquist);
    }
}

// ============================================================================
// PhaseDistortion - Warp phase relationships between partials
// ============================================================================

void FrequencyShaper::processPhaseDistortion(PartialDataSIMD& partials)
{
    if (phaseWarp_ < 1e-6f)
        return;

    constexpr int kMax = PartialDataSIMD::kMaxPartials;

    const float modPhase = static_cast<float>(phaseModPhase_);

    for (int i = 0; i < kMax; ++i)
    {
        if (!partials.isActive(i))
            continue;

        const float phi  = partials.phase[i];
        // Phase distortion: phi_out = phi + amount * sin(modPhase + phi)
        // This creates FM-like sidebands in the phase domain
        partials.phase[i] = phi + phaseWarp_ * std::sin(modPhase + phi);
    }

    // Advance modulation phase
    phaseModPhase_ += 2.0 * M_PI * phaseModFreq_ * (partials.hopSize / partials.sampleRate);
}

// ============================================================================
// FrequencyShiftAudio - FFT-based frequency shift for audio buffer
// ============================================================================

void FrequencyShaper::processFrequencyShiftAudio(juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (std::abs(shiftAmount_) < 0.5f)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0)
        return;

    // Choose FFT order: ensure fftSize >= numSamples * 2 for good frequency resolution
    int fftOrder = 10; // 1024 minimum
    while ((1 << fftOrder) < numSamples * 2 && fftOrder < 14)
        ++fftOrder;

    const int fftSize = 1 << fftOrder;
    const int numBins = fftSize;

    if (fftOrder != fftOrder_)
    {
        fft_ = std::make_unique<juce::dsp::FFT>(fftOrder);
        fftOrder_ = fftOrder;
    }

    const int binShift = static_cast<int>(std::round(
        shiftAmount_ * static_cast<float>(fftSize) / static_cast<float>(sampleRate)));

    if (binShift == 0)
        return;

    // Ensure scratch buffers are sized for current FFT size
    if (fftSize > scratch_maxFftSize_) {
        scratch_fftIn_.resize(fftSize);
        scratch_windowVec_.resize(fftSize);
        scratch_fftOut_.resize(fftSize);
        scratch_maxFftSize_ = fftSize;
    }

    // Pre-compute Hann window
    for (int i = 0; i < numSamples; ++i)
    {
        scratch_windowVec_[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(
            2.0f * float(M_PI) * static_cast<float>(i) / static_cast<float>(numSamples)));
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = buffer.getReadPointer(ch);
        float* dst       = buffer.getWritePointer(ch);

        // Clear FFT input buffer
        scratch_fftIn_.assign(fftSize, std::complex<float>(0.0f, 0.0f));

        // Window input signal and write into complex buffer
        for (int i = 0; i < numSamples; ++i)
            scratch_fftIn_[static_cast<size_t>(i)] = std::complex<float>(src[i] * scratch_windowVec_[static_cast<size_t>(i)], 0.0f);

        // Forward complex FFT
        fft_->perform(scratch_fftIn_.data(), scratch_fftIn_.data(), false);

        // --- Create analytic signal: zero out negative frequencies ---
        // Bin indices: 0 .. N-1 where 0 = DC, 1..N/2-1 = positive, N/2 = Nyquist, N/2+1..N-1 = negative
        for (int i = numBins / 2 + 1; i < numBins; ++i)
        {
            scratch_fftIn_[static_cast<size_t>(i)] = std::complex<float>(0.0f, 0.0f);
        }
        // Double positive frequencies (excluding DC and Nyquist)
        for (int i = 1; i < numBins / 2; ++i)
        {
            scratch_fftIn_[static_cast<size_t>(i)] *= 2.0f;
        }

        // --- Frequency shift: rotate spectrum ---

        // Zero-fill output buffer before frequency shift
        scratch_fftOut_.assign(fftSize, std::complex<float>(0.0f, 0.0f));

        for (int i = 0; i < numBins; ++i)
        {
            const int newIdx = i + binShift;
            if (newIdx >= 0 && newIdx < numBins)
            {
                scratch_fftOut_[static_cast<size_t>(newIdx)] = scratch_fftIn_[static_cast<size_t>(i)];
            }
        }

        // Inverse FFT (complex)
        fft_->perform(scratch_fftOut_.data(), scratch_fftOut_.data(), true);

        // Real part of the IFFT is the frequency-shifted output
        const float invScale = 1.0f / static_cast<float>(fftSize);
        for (int i = 0; i < numSamples; ++i)
            dst[i] = scratch_fftOut_[static_cast<size_t>(i)].real() * invScale;
    }
}

} // namespace ana
