#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PartialDataSIMD.h"

namespace ana {

// ============================================================================
// FrequencyShaper - Non-linear Frequency Domain Processor
//
// Applies shaping functions to partial frequencies and amplitudes for
// spectral transformation effects including saturation, folding, bitcrushing,
// resonance, formant shifting, harmonic excitation, frequency shifting,
// and phase distortion.
// ============================================================================
class FrequencyShaper
{
public:
    enum class ShaperType
    {
        Saturate,          // Soft saturation - compress high frequencies
        Fold,              // Frequency folding - mirror frequencies at boundary
        Bitcrush,          // Quantize frequencies to grid
        Resonant,          // Enhance frequencies around a center
        FormantShift,      // Shift spectral envelope (formant manipulation)
        HarmonicExciter,   // Generate harmonics from existing partials
        FrequencyShift,    // Shift ALL frequencies by constant offset (not multiplier)
        PhaseDistortion    // Distort phase relationships between partials
    };

    FrequencyShaper();
    ~FrequencyShaper() = default;

    // Mode selection
    void setType(ShaperType type);
    void setAmount(float amount);          // 0.0 to 1.0 (effect intensity)

    // Saturation
    void setThreshold(float freqHz);       // saturation threshold frequency

    // Fold
    void setFoldBoundary(float freqHz);    // mirror boundary

    // Bitcrush
    void setQuantization(float steps);     // 1 to 128 (frequency steps per octave)

    // Resonant
    void setCenterFrequency(float freqHz);
    void setResonance(float Q);            // 0.5 to 20.0
    void setBandwidth(float octaves);      // 0.1 to 4.0 octaves

    // FormantShift
    void setFormantShift(float semitones); // -12 to +12
    void setFormantAmount(float amount);   // 0.0 to 1.0

    // HarmonicExciter
    void setHarmonicOrder(int order);      // 2, 3, 4, 5
    void setHarmonicMix(float mix);        // 0.0 to 1.0 (dry/wet)

    // FrequencyShift
    void setShiftAmount(float hz);         // -2000 to +2000 Hz

    // PhaseDistortion
    void setPhaseWarp(float amount);       // 0.0 to 1.0
    void setPhaseModFreq(float freqHz);    // modulation frequency

    // Main processing
    void process(PartialDataSIMD& partials);
    void processAudio(juce::AudioBuffer<float>& buffer, double sampleRate);

    // Reset
    void reset();

private:
    // Per-type processing (PartialDataSIMD)
    void processSaturate(PartialDataSIMD& partials);
    void processFold(PartialDataSIMD& partials);
    void processBitcrush(PartialDataSIMD& partials);
    void processResonant(PartialDataSIMD& partials);
    void processFormantShift(PartialDataSIMD& partials);
    void processHarmonicExciter(PartialDataSIMD& partials);
    void processFrequencyShift(PartialDataSIMD& partials);
    void processPhaseDistortion(PartialDataSIMD& partials);

    // Audio buffer processing (for FrequencyShift which needs FFT)
    void processFrequencyShiftAudio(juce::AudioBuffer<float>& buffer, double sampleRate);

    // --- Parameters ---
    ShaperType type_ = ShaperType::Saturate;
    float amount_ = 0.5f;
    float threshold_ = 8000.0f;
    float foldBoundary_ = 10000.0f;
    float quantization_ = 12.0f;
    float centerFrequency_ = 1000.0f;
    float resonance_ = 2.0f;
    float bandwidth_ = 1.0f;
    float formantShift_ = 0.0f;
    float formantAmount_ = 0.5f;
    int harmonicOrder_ = 3;
    float harmonicMix_ = 0.5f;
    float shiftAmount_ = 0.0f;
    float phaseWarp_ = 0.0f;
    float phaseModFreq_ = 0.5f;

    double sampleRate_ = 44100.0;
    double phaseModPhase_ = 0.0;

    // Pre-allocated scratch buffers (max FFT size = 16384)
    mutable std::vector<std::complex<float>> scratch_fftIn_;
    mutable std::vector<float> scratch_windowVec_;
    mutable std::vector<std::complex<float>> scratch_fftOut_;
    mutable int scratch_maxFftSize_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyShaper)
};

} // namespace ana
