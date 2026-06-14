#pragma once

#include <vector>
#include <cmath>
#include <memory>
#include <juce_dsp/juce_dsp.h>

#include "PartialDataSIMD.h"
#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/**
    Vocoder Mode — external audio modulation for additive synthesis.

    Implements two complementary vocoder architectures:

        - Filter Bank mode (default): a bank of log-spaced bandpass IIR filters
          with per-band envelope followers.  The modulator signal is analysed
          through the filter bank to extract band energies; those energies gate
          and shape the carrier (additive partials or audio buffer).

        - FFT mode: the modulator magnitude spectrum is computed via FFT and
          grouped into log-spaced bands.  Band energies drive the same envelope
          followers and are applied to the carrier.  Activated by explicitly
          calling setFftSize().

    Formant shifting is achieved by resampling the band-energy envelope before
    applying it to the carrier, producing classic "vocal tract length"
    transformations across ±12 semitones.

    Usage (additive partials):
        VocoderMode vocoder;
        vocoder.setNumBands(16);
        vocoder.setMix(0.8f);
        vocoder.setAttack(5.0f);
        vocoder.setRelease(40.0f);
        vocoder.setModulator(externalAudio, externalSampleRate);
        vocoder.process(partialData);  // modulates partial amplitudes

    Usage (audio buffer):
        VocoderMode vocoder;
        vocoder.processAudio(carrierBuffer, modulatorBuffer);
*/
class VocoderMode
{
public:
    //==============================================================================
    VocoderMode();
    ~VocoderMode() = default;

    //==============================================================================
    /** @name Configuration */
    //@{
    /** Set the number of vocoder bands (8 to 32).
        Lower counts produce coarser, more intelligible speech;
        higher counts yield finer spectral detail. */
    void setNumBands(int bands);

    /** Wet/dry mix.  0.0 = dry carrier only, 1.0 = fully wet. */
    void setMix(float mix);

    /** Formant shift in semitones (-12 to +12).
        Positive values raise the formant (smaller vocal tract),
        negative values lower it (larger vocal tract). */
    void setFormantShift(float semitones);

    /** Envelope follower attack time in ms (1—100). */
    void setAttack(float ms);

    /** Envelope follower release time in ms (1—500). */
    void setRelease(float ms);

    /** Set the sample rate (needed for filter coefficients). */
    void setSampleRate(double sr);

    /** Set FFT size for FFT-based mode (512 / 1024 / 2048).
        Calling this method switches the analyser to FFT mode. */
    void setFftSize(int size);
    //@}

    //==============================================================================
    /** @name Modulator Input */
    //@{
    /** Provide the external modulator audio for subsequent process() calls.
        @param audio       Modulator sample buffer.
        @param sampleRate  Sample rate of the modulator audio. */
    void setModulator(const std::vector<float>& audio, double sampleRate);
    //@}

    //==============================================================================
    /** @name Processing */
    //@{
    /** Apply the vocoder to additive-synthesis partials.
        Analyses the most recently provided modulator buffer, extracts
        per-band envelopes, and scales each partial's amplitude according
        to the band it falls into. */
    void process(PartialDataSIMD& carrierPartials);

    /** Apply the vocoder to an audio buffer.
        Self-contained: analyses the modulator buffer in real time,
        applies band gains to the carrier, and mixes wet/dry.
        @param carrier    Audio buffer to process (modified in-place).
        @param modulator  Modulator audio buffer (read-only). */
    void processAudio(juce::AudioBuffer<float>& carrier,
                      const juce::AudioBuffer<float>& modulator);
    //@}

    //==============================================================================
    /** Reset all envelope followers and filter states. */
    void reset();

private:
    //==============================================================================
    /** Internal mode selection. */
    enum class Mode { FilterBank, FFT };

    /** Determine which analysis mode is active. */
    Mode selectMode() const noexcept;

    //==============================================================================
    /** @name Analysis (modulator → band energies) */
    //@{
    /** Analyse the modulator buffer through the filter bank + envelope followers. */
    void analyseFilterBank(const std::vector<float>& modulator,
                           std::vector<float>& energies);

    /** Analyse the modulator buffer via FFT magnitude → band grouping. */
    void analyseFFT(const std::vector<float>& modulator,
                    std::vector<float>& energies);
    //@}

    //==============================================================================
    /** @name Application (band energies → carrier) */
    //@{
    /** Scale each active partial's amplitude by the corresponding band energy. */
    void applyToPartials(PartialDataSIMD& carrier,
                         const std::vector<float>& bandEnergies);

    /** Process an audio buffer carrier through the filter bank + band gains. */
    void applyFilterBankToAudio(juce::AudioBuffer<float>& carrier,
                                const std::vector<float>& bandEnergies);
    //@}

    //==============================================================================
    /** @name Utility */
    //@{
    /** Build / rebuild the log-spaced bandpass filter bank. */
    void buildFilterBank();

    /** Recompute envelope follower coefficients from attack/release times. */
    void updateEnvelopeCoeffs();

    /** Apply formant shift to a band-energy vector via interpolation. */
    void shiftFormant(const std::vector<float>& bandEnergies,
                      std::vector<float>& shifted) const;

    /** Compute the squared-sum energy for a channel of audio. */
    static float channelEnergy(const float* data, int numSamples) noexcept;

    /** Build a Hann window of fftSize_ samples. */
    void buildWindow();
    //@}

    //==============================================================================
    // Configuration
    int    numBands_        = 16;
    float  mix_             = 0.5f;
    float  formantShift_    = 0.0f;
    float  attackMs_        = 10.0f;
    float  releaseMs_       = 50.0f;
    double sampleRate_      = 44100.0;
    int    fftSize_         = 0;       // 0 = FilterBank mode, >0 = FFT mode

    // Modulator state
    std::vector<float> modulatorBuffer_;
    double             modulatorSampleRate_ = 44100.0;

    // Envelope followers
    std::vector<float> bandEnvelopes_;
    std::vector<float> attackCoeffs_;
    std::vector<float> releaseCoeffs_;

    // Filter bank — shared coefficients for both modulator and carrier paths
    std::vector<juce::dsp::IIR::Coefficients<float>::Ptr> bandFilterCoeffs_;

    // Per-band filter state (modulator analysis — recreated each analyse call)
    // Per-band filter state (carrier processing — persistent across processAudio)
    std::vector<juce::dsp::IIR::Filter<float>> carrierFilters_;

    // Band centre frequencies (log-spaced 20 Hz – 20 kHz)
    std::vector<float> bandFrequencies_;

    // FFT state
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<float>              fftWindow_;
    std::vector<float>              overlapBuffer_;
    int                             fftHopSize_ = 0;

    // Precomputed bin-to-band mapping (indexed by FFT bin, value = band index)
    std::vector<int> binToBandLUT_;

    // Pre-allocated scratch buffers (no heap alloc in audio thread)
    mutable std::vector<float> scratch_energies_;
    mutable std::vector<float> scratch_modFrame_;
    mutable std::vector<int>   scratch_binCount_;
    mutable std::vector<float> scratch_fftIn_;
    mutable std::vector<float> scratch_magnitude_;
    mutable std::vector<float> scratch_wet_;

    // Dirty flags
    bool filtersDirty_        = true;
    bool envelopeCoeffsDirty_ = true;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderMode)
};

} // namespace ana
