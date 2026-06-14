#pragma once

// ============================================================================
// SpectralReverb - Frequency-Domain Convolution Reverb
//
// Applies an impulse response's spectral envelope to partial data (frequency
// domain). Zero latency — no feedback buffer, operates on the spectral
// snapshot directly. Uses analytic IR generation for presets; no FFT needed
// in the real-time process() path.
// ============================================================================

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PartialDataSIMD.h"
#include "SIMDSupport.h"

namespace ana {

class SpectralReverb
{
public:
    // IR Presets
    enum class Preset
    {
        Room,
        Hall,
        Chamber,
        Plate,
        Cathedral,
        Ambience,
        Spring
    };

    SpectralReverb();
    ~SpectralReverb() = default;

    // Presets & IR loading
    void loadPreset(Preset preset);
    void loadImpulseResponse(const std::vector<float>& ir, double sampleRate);

    // Parameters
    void setMix(float mix);               // 0.0 to 1.0
    void setDecay(float decay);           // 0.0 to 1.0 (reverb tail length)
    void setPredelay(float ms);           // 0 to 200 ms
    void setDamping(float damping);       // 0.0 to 1.0 (high-frequency absorption)
    void setDiffusion(float diffusion);   // 0.0 to 1.0 (echo density / blur)
    void setSize(float size);             // 0.1 to 2.0 (room size multiplier)
    void setStereoWidth(float width);     // 0.0 to 1.0

    void setSampleRate(double sr);
    void setFftSize(int size);            // must be power of 2

    // Pre-allocate scratch buffers for the audio thread (call from prepareToPlay)
    void prepare(int maxBlockSize);

    // Process on partial data (spectral reverb — main path)
    void process(PartialDataSIMD& partials);

    // Process on audio buffer (uses JUCE dsp::Convolution)
    void processAudio(juce::AudioBuffer<float>& buffer);

    void reset();

private:
    // Extract spectral envelope from loaded IR
    void analyzeIR();

    // Generate analytic spectral envelope for a preset
    void generatePresetEnvelope(Preset preset);

    // Look up spectral envelope gain at a given frequency (linear interpolation)
    float getEnvelopeGain(float freqHz) const noexcept;

    // Apply spectral envelope to partials (multiply amplitudes)
    void applySpectralEnvelope(PartialDataSIMD& partials);

    // Impulse response data
    std::vector<float> ir_;
    double irSampleRate_ = 44100.0;
    bool irLoaded_ = false;

    // Spectral envelope (frequency-domain representation of IR)
    // Indexed by FFT bin: bin i covers frequency i * sampleRate / fftSize
    std::vector<float> spectralEnvelope_;   // size = fftSize/2 + 1

    // Scratch buffers (pre-allocated — no heap in audio thread)
    float scratchGains_[PartialDataSIMD::kMaxPartials]{};   // per-partial gains
    float blurred_[PartialDataSIMD::kMaxPartials]{};        // diffusion scratch
    std::vector<float> scratchDry_;     // pre-sized to max block size
    std::vector<float> scratchWet_;     // pre-sized to maxIRLen + max block size

    // Current preset (for FFT size changes)
    Preset currentPreset_ = Preset::Room;

    // Parameters
    float mix_            = 0.3f;
    float decay_          = 0.5f;
    float predelayMs_     = 20.0f;
    float damping_        = 0.5f;
    float diffusion_      = 0.5f;
    float size_           = 1.0f;
    float stereoWidth_    = 0.5f;

    double sampleRate_    = 44100.0;
    int fftSize_          = 2048;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralReverb)
};

} // namespace ana
