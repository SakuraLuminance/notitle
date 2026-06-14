#include "SpectralReverb.h"
#include <juce_dsp/juce_dsp.h>

namespace ana {

// ============================================================================
// Construction
// ============================================================================

SpectralReverb::SpectralReverb()
{
    // Initialize with Room preset
    loadPreset(Preset::Room);
}

// ============================================================================
// Parameter setters
// ============================================================================

void SpectralReverb::setMix(float mix)           { mix_         = std::clamp(mix, 0.0f, 1.0f); }
void SpectralReverb::setDecay(float decay)       { decay_       = std::clamp(decay, 0.0f, 1.0f); }
void SpectralReverb::setPredelay(float ms)        { predelayMs_  = std::clamp(ms, 0.0f, 200.0f); }
void SpectralReverb::setDamping(float damping)   { damping_     = std::clamp(damping, 0.0f, 1.0f); }
void SpectralReverb::setDiffusion(float diffusion) { diffusion_ = std::clamp(diffusion, 0.0f, 1.0f); }
void SpectralReverb::setSize(float size)          { size_        = std::clamp(size, 0.1f, 2.0f); }
void SpectralReverb::setStereoWidth(float width)  { stereoWidth_ = std::clamp(width, 0.0f, 1.0f); }
void SpectralReverb::setSampleRate(double sr)     { sampleRate_  = sr; }
void SpectralReverb::setFftSize(int size)
{
    // Ensure power of 2 with a minimum of 512
    int s = 512;
    while (s < size && s < 16384)
        s <<= 1;
    fftSize_ = s;

    // Regenerate envelope at new resolution
    if (irLoaded_)
        analyzeIR();
    else
        generatePresetEnvelope(currentPreset_);
}

// ============================================================================
// IR Loading & Presets
// ============================================================================

void SpectralReverb::loadPreset(Preset preset)
{
    currentPreset_ = preset;
    generatePresetEnvelope(preset);
    irLoaded_ = false;
}

void SpectralReverb::loadImpulseResponse(const std::vector<float>& ir, double sampleRate)
{
    ir_ = ir;
    irSampleRate_ = sampleRate;
    irLoaded_ = true;
    analyzeIR();
}

// ============================================================================
// Envelope generation per preset
// ============================================================================

void SpectralReverb::generatePresetEnvelope(Preset preset)
{
    const int numBins = fftSize_ / 2 + 1;
    spectralEnvelope_.resize(numBins);

    for (int i = 0; i < numBins; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(numBins - 1); // 0..1 across frequency
        const float freq = t * static_cast<float>(sampleRate_) * 0.5f;

        switch (preset)
        {
            case Preset::Room:
            {
                // Short decay, even frequency response
                spectralEnvelope_[i] = 1.0f;
                break;
            }

            case Preset::Hall:
            {
                // Long decay, slight HF rolloff
                const float rolloff = 0.35f * t * t;
                spectralEnvelope_[i] = std::max(0.0f, 1.0f - rolloff);
                break;
            }

            case Preset::Chamber:
            {
                // Medium decay, slight mid boost (~2 kHz)
                const float midCenter = 2000.0f / (sampleRate_ * 0.5f);
                const float bell = std::exp(-4.0f * (t - midCenter) * (t - midCenter) * 20.0f);
                spectralEnvelope_[i] = 1.0f + 0.25f * bell;

                // Slight HF rolloff
                const float hf = 0.2f * t * t;
                spectralEnvelope_[i] -= hf;
                spectralEnvelope_[i] = std::max(0.1f, spectralEnvelope_[i]);
                break;
            }

            case Preset::Plate:
            {
                // Medium decay, metallic sheen — boosted mids (~3 kHz)
                const float plateCenter = 3000.0f / (sampleRate_ * 0.5f);
                const float bell = std::exp(-6.0f * (t - plateCenter) * (t - plateCenter) * 15.0f);
                spectralEnvelope_[i] = 1.0f + 0.5f * bell;

                // Gentle HF shelf
                const float hf = 0.15f * t;
                spectralEnvelope_[i] -= hf;
                spectralEnvelope_[i] = std::max(0.1f, spectralEnvelope_[i]);
                break;
            }

            case Preset::Cathedral:
            {
                // Very long decay, strong HF rolloff
                spectralEnvelope_[i] = std::exp(-3.0f * t * t);
                break;
            }

            case Preset::Ambience:
            {
                // Very short decay, flat response
                spectralEnvelope_[i] = 1.0f;
                break;
            }

            case Preset::Spring:
            {
                // Short decay, resonant comb peaks
                const float combFreq  = 200.0f / (sampleRate_ * 0.5f);
                const float resonance = 1.0f + 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * t / (combFreq + 0.001f));
                spectralEnvelope_[i] = std::min(1.5f, resonance);

                // HF rolloff
                const float hf = 0.25f * t;
                spectralEnvelope_[i] -= hf;
                spectralEnvelope_[i] = std::max(0.05f, spectralEnvelope_[i]);
                break;
            }

            default:
                spectralEnvelope_[i] = 1.0f;
                break;
        }
    }

    // Set sensible defaults per preset
    switch (preset)
    {
        case Preset::Room:       decay_ = 0.2f; damping_ = 0.3f; diffusion_ = 0.1f; size_ = 0.5f; predelayMs_ =  5.0f; mix_ = 0.3f; break;
        case Preset::Hall:       decay_ = 0.7f; damping_ = 0.4f; diffusion_ = 0.5f; size_ = 1.5f; predelayMs_ = 20.0f; mix_ = 0.3f; break;
        case Preset::Chamber:    decay_ = 0.5f; damping_ = 0.4f; diffusion_ = 0.4f; size_ = 0.9f; predelayMs_ = 12.0f; mix_ = 0.3f; break;
        case Preset::Plate:      decay_ = 0.5f; damping_ = 0.3f; diffusion_ = 0.6f; size_ = 0.8f; predelayMs_ = 10.0f; mix_ = 0.4f; break;
        case Preset::Cathedral:  decay_ = 0.9f; damping_ = 0.7f; diffusion_ = 0.7f; size_ = 2.0f; predelayMs_ = 40.0f; mix_ = 0.4f; break;
        case Preset::Ambience:   decay_ = 0.1f; damping_ = 0.2f; diffusion_ = 0.2f; size_ = 0.3f; predelayMs_ =  2.0f; mix_ = 0.2f; break;
        case Preset::Spring:     decay_ = 0.3f; damping_ = 0.2f; diffusion_ = 0.3f; size_ = 0.6f; predelayMs_ =  3.0f; mix_ = 0.3f; break;
        default: break;
    }

}

// ============================================================================
// IR analysis (frequency-domain envelope extraction)
// ============================================================================

void SpectralReverb::analyzeIR()
{
    if (ir_.empty())
        return;

    const int irLen = static_cast<int>(ir_.size());

    // Use fftSize_ padded with zeros
    const int fftSize = std::max(fftSize_, juce::nextPowerOfTwo(irLen));
    const int numBins = fftSize / 2 + 1;

    spectralEnvelope_.resize(numBins);

    // Prepare FFT buffer (interleaved real/imag — JUCE format)
    std::vector<float> fftBuffer(static_cast<size_t>(fftSize) * 2u, 0.0f);

    // Copy IR with Hann window, find peak
    float peak = 0.0f;
    for (int i = 0; i < irLen; ++i)
    {
        const float window = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                                       * i / (irLen - 1)));
        fftBuffer[static_cast<size_t>(i) * 2u] = ir_[static_cast<size_t>(i)] * window;
        peak = std::max(peak, std::abs(fftBuffer[static_cast<size_t>(i) * 2u]));
    }

    // Normalize to 0 dB peak
    if (peak > 1e-12f)
    {
        const float invPeak = 1.0f / peak;
        for (int i = 0; i < fftSize; ++i)
            fftBuffer[static_cast<size_t>(i) * 2u] *= invPeak;
    }

    // Perform FFT
    {
        const int order = static_cast<int>(std::round(std::log2(static_cast<float>(fftSize))));
        juce::dsp::FFT fft(order);
        fft.performRealOnlyForwardTransform(fftBuffer.data());
    }

    // Extract magnitude spectrum (linear)
    for (int i = 0; i < numBins; ++i)
    {
        const float real = fftBuffer[static_cast<size_t>(i) * 2u];
        const float imag = fftBuffer[static_cast<size_t>(i) * 2u + 1u];
        spectralEnvelope_[i] = std::sqrt(real * real + imag * imag);
    }

    // Smooth the envelope: 3-point moving average to reduce comb artifacts
    std::vector<float> smooth(spectralEnvelope_.size());
    for (int i = 0; i < numBins; ++i)
    {
        float sum = 0.0f;
        int cnt = 0;
        for (int d = -1; d <= 1; ++d)
        {
            const int idx = i + d;
            if (idx >= 0 && idx < numBins)
            {
                sum += spectralEnvelope_[idx];
                ++cnt;
            }
        }
        smooth[i] = (cnt > 0) ? sum / static_cast<float>(cnt) : 0.0f;
    }
    spectralEnvelope_ = std::move(smooth);

    // Ensure minimum gain floor
    constexpr float kFloor = 1e-6f;
    for (auto& v : spectralEnvelope_)
        v = std::max(kFloor, v);

}

// ============================================================================
// Envelope lookup (linear interpolation)
// ============================================================================

float SpectralReverb::getEnvelopeGain(float freqHz) const noexcept
{
    if (spectralEnvelope_.empty())
        return 0.0f;

    // Apply size: stretch or compress the frequency axis
    // size_ > 1: stretch (larger room = more low-frequency energy)
    // size_ < 1: compress (smaller room = flatter response)
    const float effectiveFreq = freqHz / size_;

    const int numBins = static_cast<int>(spectralEnvelope_.size());
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;

    // Map frequency to bin index
    const float binIndex = (effectiveFreq / nyquist) * static_cast<float>(numBins - 1);
    const int idx0 = std::max(0, std::min(static_cast<int>(binIndex), numBins - 1));
    const int idx1 = std::min(idx0 + 1, numBins - 1);
    const float frac = binIndex - static_cast<float>(idx0);

    // Linear interpolation
    return spectralEnvelope_[static_cast<size_t>(idx0)]
           * (1.0f - frac)
           + spectralEnvelope_[static_cast<size_t>(idx1)]
           * frac;
}

// ============================================================================
// Prepare (pre-allocate scratch buffers — call from prepareToPlay)
// ============================================================================

void SpectralReverb::prepare(int maxBlockSize)
{
    constexpr int kMaxIRLen = 8192;
    scratchDry_.resize(static_cast<size_t>(maxBlockSize));
    scratchWet_.resize(static_cast<size_t>(maxBlockSize + kMaxIRLen));
}

// ============================================================================
// Process partial data (spectral reverb — main path)
// ============================================================================

void SpectralReverb::process(PartialDataSIMD& partials)
{
    const int numPartials = PartialDataSIMD::kMaxPartials;
    const float nyquist = static_cast<float>(sampleRate_) * 0.5f;

    float* gains = scratchGains_;

    // ---------------------------------------------------------------
    // Phase 1: compute per-partial reverb gain
    // ---------------------------------------------------------------
    for (int i = 0; i < numPartials; ++i)
    {
        if (!partials.isActive(i))
        {
            gains[i] = 0.0f;
            continue;
        }

        const float freq = partials.frequency[i];
        const float normFreq = freq / nyquist; // 0..1

        // 1a) Spectral envelope gain (from IR or preset)
        const float envGain = getEnvelopeGain(freq);

        // 1b) Decay: exponential tail based on frequency
        // Higher frequencies decay faster (natural reverb behavior)
        const float decayFactor = std::exp(-decay_ * 4.0f * normFreq);

        // 1c) Damping: high-frequency absorption
        const float dampingFactor = std::max(0.001f, 1.0f - damping_ * normFreq);

        // Combined pre-mix gain
        gains[i] = envGain * decayFactor * dampingFactor;
    }

    // ---------------------------------------------------------------
    // Phase 2: diffusion — spread energy across neighboring partials
    // ---------------------------------------------------------------
    if (diffusion_ > 0.005f)
    {
        // Compute blurred gains into the scratch buffer
        const int radius = std::max(1, static_cast<int>(diffusion_ * 3.0f));

        for (int i = 0; i < numPartials; ++i)
        {
            if (!partials.isActive(i))
            {
                blurred_[i] = 0.0f;
                continue;
            }

            float sum = gains[i];
            float weightSum = 1.0f;

            // Average with neighbors, weighted by distance (triangular window)
            for (int d = 1; d <= radius; ++d)
            {
                const float w = 1.0f - static_cast<float>(d) / static_cast<float>(radius + 1);

                const int left = i - d;
                if (left >= 0 && partials.isActive(left))
                {
                    sum += gains[left] * w;
                    weightSum += w;
                }

                const int right = i + d;
                if (right < numPartials && partials.isActive(right))
                {
                    sum += gains[right] * w;
                    weightSum += w;
                }
            }

            blurred_[i] = sum / weightSum;
        }

        // Blend original and blurred
        for (int i = 0; i < numPartials; ++i)
        {
            if (!partials.isActive(i))
                continue;
            gains[i] = gains[i] * (1.0f - diffusion_)
                     + blurred_[i] * diffusion_;
        }
    }

    // ---------------------------------------------------------------
    // Phase 3: apply gains with mix
    //
    //   output[i] = amp[i] * (1 - mix) + amp[i] * gain[i] * mix
    //   output[i] = amp[i] * (1 - mix + gain[i] * mix)
    //
    // Build combined coefficient, then SIMD multiply.
    // ---------------------------------------------------------------
    {
        // Build combined coefficients in the scratch buffer
        const float oneMinusMix = 1.0f - mix_;
        for (int i = 0; i < numPartials; ++i)
        {
            if (!partials.isActive(i))
                continue;
            gains[i] = oneMinusMix + gains[i] * mix_;
        }

        // Apply to amplitudes via SIMD vector multiply
        // We multiply the entire array; inactive partials have amplitude ~0 so the
        // product stays ~0 (the gain for inactive entries is `oneMinusMix` but their
        // amplitude is ~0).
        SIMDKernels::vectorMul(partials.amplitude,
                               partials.amplitude,
                               scratchGains_,
                               PartialDataSIMD::kMaxPartials);
    }
}

// ============================================================================
// Process audio buffer (direct convolution with loaded IR)
// ============================================================================

void SpectralReverb::processAudio(juce::AudioBuffer<float>& buffer)
{
    if (ir_.empty())
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    const int irLen       = static_cast<int>(ir_.size());

    // Cap IR length to prevent excessive CPU on long IRs in real-time
    constexpr int kMaxIRLen = 8192;
    const int effectiveIRLen = std::min(irLen, kMaxIRLen);

    // Predelay in samples
    const int predelaySamples = static_cast<int>(predelayMs_ * sampleRate_ / 1000.0);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* channel = buffer.getWritePointer(ch);

        // Stereo width: for stereo channels, scale the right channel
        if (ch == 1 && stereoWidth_ < 1.0f && numChannels > 1)
        {
            // Reduce right channel amplitude for narrower stereo image
            const float widthScale = 0.5f + 0.5f * stereoWidth_;
            for (int n = 0; n < numSamples; ++n)
                channel[n] *= widthScale;
        }

        // Save dry signal (scratchDry_ pre-sized in prepare())
        scratchDry_.resize(static_cast<size_t>(numSamples));
        std::copy(channel, channel + numSamples, scratchDry_.data());

        // Direct convolution: wet = dry * ir (scratchWet_ pre-sized in prepare())
        const int convLen = numSamples + effectiveIRLen - 1;
        std::fill(scratchWet_.begin(), scratchWet_.begin() + convLen, 0.0f);
        for (int n = 0; n < numSamples; ++n)
        {
            const float input = scratchDry_[static_cast<size_t>(n)];
            if (std::abs(input) < 1e-12f)
                continue;

            for (int k = 0; k < effectiveIRLen; ++k)
                scratchWet_[static_cast<size_t>(n + k)] += input * ir_[static_cast<size_t>(k)];
        }

        // Mix: dry/wet with predelay
        for (int n = 0; n < numSamples; ++n)
        {
            const int wetIdx = n - predelaySamples;
            float wetSample = 0.0f;
            if (wetIdx >= 0 && wetIdx < convLen)
                wetSample = scratchWet_[static_cast<size_t>(wetIdx)];

            channel[n] = scratchDry_[static_cast<size_t>(n)] * (1.0f - mix_)
                       + wetSample * mix_;
        }
    }
}

// ============================================================================
// Reset
// ============================================================================

void SpectralReverb::reset()
{
    // Spectral envelope remains (deterministic from IR/preset)
    // Clear any accumulated scratch state
    std::fill(scratchGains_, scratchGains_ + PartialDataSIMD::kMaxPartials, 0.0f);
}

} // namespace ana
