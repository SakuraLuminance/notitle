#pragma once
#include <vector>
#include <complex>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/**
    Selectable pitch-shift algorithm.
*/
enum class PitchAlgorithm
{
    Simple,       ///< Windowed sinc interpolation (fast, low quality)
    PhaseVocoder, ///< FFT-based phase vocoder (good quality, moderate speed)
    Spectral,     ///< Spectral-domain shift with envelope preservation (high quality)
    Formant,      ///< Formant-preserving pitch shift (best for vocals)
    Granular      ///< Granular synthesis pitch shift (creative, textured)
};

//==============================================================================
/**
    Pitch correction and shifting with 5 selectable algorithms.

    Algorithms:
        - Simple:       windowed sinc interpolation (fast, low quality)
        - PhaseVocoder: FFT phase vocoder with frequency scaling
        - Spectral:     spectral-domain shift with envelope preservation
        - Formant:      formant-preserving shift via spectral envelope separation
        - Granular:     granular synthesis pitch shift (creative, textured)

    Each algorithm operates on juce::AudioBuffer<float> in-place, processing
    all channels independently.  Streaming is supported — process() may be
    called repeatedly with arbitrary buffer sizes.
*/
class PitchCorrector
{
public:
    PitchCorrector();
    ~PitchCorrector() = default;

    //==============================================================================
    /// Select the pitch-shift algorithm.
    void setAlgorithm(PitchAlgorithm algo) noexcept;
    /// Set pitch shift in semitones (-24 .. +24).
    void setPitchShift(float semitones) noexcept;
    /// Formant preservation amount 0..1 (used by Formant algorithm).
    void setFormantPreservation(float amount) noexcept;
    /// Correction / wet amount 0..1.
    void setCorrectionAmount(float amount) noexcept;
    /// Sample rate in Hz.
    void setSampleRate(double sr) noexcept;
    /// FFT size (power of 2, default 2048).
    void setFftSize(int size) noexcept;

    //==============================================================================
    /** Process audio in-place.
        Reads from buffer, writes shifted result back.
        Makes an internal copy of input so all channels are processed independently.
    */
    void process(juce::AudioBuffer<float>& buffer);

    /** Analyse audio and estimate pitch.
        @return MIDI note number (0-127) or 0.0 if no pitch detected.
    */
    float detectPitch(const std::vector<float>& audio, double sampleRate);

    /** Pre-allocate input copy buffer for worst-case block size.
        Call after setSampleRate() and before process(). */
    void prepare(int maxNumChannels, int maxBlockSize);

    /** Reset all internal state. Call after seek or when starting new audio. */
    void reset();

private:
    //==============================================================================
    // Per-algorithm dispatchers (receive const input, write to output).
    void processSimple(const juce::AudioBuffer<float>& input,
                       juce::AudioBuffer<float>& output);
    void processPhaseVocoder(const juce::AudioBuffer<float>& input,
                             juce::AudioBuffer<float>& output);
    void processSpectralShift(const juce::AudioBuffer<float>& input,
                              juce::AudioBuffer<float>& output);
    void processFormantPreserving(const juce::AudioBuffer<float>& input,
                                  juce::AudioBuffer<float>& output);
    void processGranularShift(const juce::AudioBuffer<float>& input,
                              juce::AudioBuffer<float>& output);

    //==============================================================================
    // Helpers
    void initDSP();
    void computeWindow();

    /** Blackman-windowed sinc interpolation at fractional position @p pos.
        @param ratio  pitch ratio (for anti-alias cutoff).
    */
    static float sincInterp(const float* in, int len, float pos, float ratio);

    /** Triangular spectral smoothing for envelope extraction.
        @param smoothWidth  one-sided width in bins.
    */
    static void spectralSmooth(const float* mag, int halfSize,
                               float* envelope, int smoothWidth);

    /** Autocorrelation LPC analysis (autocorrelation + Levinson-Durbin).
        Coefficients are stored with a[0] = 1.0.
        Minimum-phase residual = gain * white noise.
    */
    void lpcAutocorrelation(const float* x, int n, int order,
                            float* coeffs, float& gain);

    //==============================================================================
    // Members
    void ensureScratchSizes() noexcept;

    std::unique_ptr<juce::dsp::FFT> fft_;
    int  fftOrder_   = 11;

    PitchAlgorithm algo_                = PitchAlgorithm::Spectral;
    float          pitchShift_          = 0.0f;
    float          formantPreservation_ = 0.5f;
    float          correctionAmount_    = 1.0f;
    double         sampleRate_          = 44100.0;
    int            fftSize_             = 2048;
    int            hopSize_             = 512;
    int            halfSize_            = 1025;

    std::vector<float> hannWindow_;

    // Cached grain window size (avoids recomputation of scratch_grainWin_)
    int cachedGrainSize_ = 0;

    //==============================================================================
    // Pre-allocated scratch buffers — no heap allocations in audio thread
    // These are sized for worst-case fftSize_ and must be large enough for
    // any numSamples that the DAW can deliver.
    mutable std::vector<float> scratch_accum_;
    mutable std::vector<float> scratch_prevPhase_;
    mutable std::vector<float> scratch_outPhase_;
    mutable std::vector<float> scratch_mag_;
    mutable std::vector<float> scratch_envelope_;
    mutable std::vector<float> scratch_fineStruct_;
    mutable std::vector<float> scratch_shiftedFine_;
    mutable std::vector<float> scratch_frame_;
    mutable std::vector<float> scratch_grainWin_;
    mutable std::vector<float> scratch_grainAccum_;
    mutable std::vector<float> scratch_weightAccum_;
    mutable std::vector<float> scratch_grain_;
    mutable std::vector<float> scratch_fftPitch_;
    mutable std::vector<double> scratch_lpcR_;
    mutable std::vector<float> scratch_aPrev_;
    mutable std::vector<float> scratch_aCurr_;

    // Pre-allocated input copy — never stack-allocate in process()
    juce::AudioBuffer<float> inputCopyBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchCorrector)
};

} // namespace ana
