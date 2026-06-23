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
    Result of pitch detection analysis.
*/
struct PitchDetectionResult {
    float detectedFreq = 0.0f;      // detected pitch in Hz
    int detectedMidiNote = -1;      // nearest MIDI note
    float detectedCents = 0.0f;     // cents deviation from nearest note
    float confidence = 0.0f;        // 0.0 to 1.0
    int numFrames = 0;              // analysis frames
    std::vector<float> pitchCurve;  // per-frame pitch track
    std::vector<float> confidenceCurve; // per-frame confidence
};

//==============================================================================
/**
    FL Studio style "pitch flatten" processing.

    Detects the pitch contour of a loaded sample and corrects it to the
    nearest chromatic note (or a user-specified note), with smoothness
    and formant-preservation controls.

    Uses a frame-based YIN pitch detector and a phase vocoder / spectral
    shift engine, sharing scratch buffers to avoid heap allocations in
    the audio-processing path.
*/
class SampleProcessor {
public:
    SampleProcessor();
    ~SampleProcessor() = default;

    //==============================================================================
    // === PITCH DETECTION ===

    /** Analyse the full audio buffer, return detailed pitch result. */
    PitchDetectionResult detectPitch(const std::vector<float>& audio, double sampleRate);

    /** Quick detect: just returns the most likely MIDI note (0-127), -1 if none. */
    int detectRootNote(const std::vector<float>& audio, double sampleRate);

    //==============================================================================
    // === PITCH FLATTEN ===

    /** "发音音高拉平" — FL Studio style pitch flatten.

        @param input         Audio buffer to process.
        @param sampleRate    Sample rate in Hz.
        @param targetNote    -1 = auto (nearest note), 0-127 = specific MIDI note.
        @param smoothness    0.0 = abrupt note transitions, 1.0 = smooth curves.
        @param preserveFormants  Keep original formant structure (vocal quality).

        @return Corrected audio buffer.
    */
    std::vector<float> flattenPitch(const std::vector<float>& input,
                                     double sampleRate,
                                     int targetNote = -1,
                                     float smoothness = 0.7f,
                                     bool preserveFormants = true);

    //==============================================================================
    // === ROOT NOTE ===

    void setRootNote(int midiNote);         // 0-127
    int  getRootNote() const;
    void setRootFineTune(float cents);      // -50.0 to +50.0
    float getRootFineTune() const;

    //==============================================================================
    // === UTILITIES ===

    /** Format a frequency for UI display, e.g. "C4 (+12 cents)".
        Returns "---" for freqHz <= 0.
    */
    juce::String getPitchDisplayText(float freqHz) const;

    /** Convert MIDI note number to frequency: A4=69 → 440 Hz.
        @param fineTuneCents  fine tuning offset in cents.
    */
    static float midiNoteToFrequency(int note, float fineTuneCents = 0.0f);

    //==============================================================================
    // === CONFIG ===

    void setSampleRate(double sr);
    void setFftSize(int size);

    /** Reset internal cached state. */
    void reset();

private:
    //==============================================================================
    // Pitch detection helpers

    /** Compute the YIN cumulative-mean-normalized difference function
        and find the best period lag.

        @return  Detected frequency in Hz, or 0.0 if no clear pitch.
    */
    float yinPitchDetection(const float* audio, int n, double sampleRate,
                            float& confidence, float threshold = 0.15f) const;

    /** Frame-based pitch analysis: split audio into overlapping windows
        and run YIN on each.
    */
    void analyzePitchFrames(const std::vector<float>& audio, double sampleRate,
                            std::vector<float>& pitchCurve,
                            std::vector<float>& confidenceCurve) const;

    //==============================================================================
    // Pitch-correction engine

    /** Apply pitch flatten using a phase vocoder with per-frame ratios.

        @param input      Source audio (mono).
        @param output     Output buffer (pre-allocated, same length as input).
        @param numSamples Number of samples.
        @param sr         Sample rate.
        @param ratios     Per-frame pitch correction ratios.
        @param numRatios  Number of ratios (must match frame count).
        @param hopSize    Analysis/synthesis hop (samples).
        @param fftSize    FFT size.
    */
    void flattenVocoder(const float* input, float* output, int numSamples,
                        double sr, const float* ratios, int numRatios,
                        int hopSize, int fftSize);

    /** Apply pitch flatten using formant-preserving spectral shift.

        Parameters same as flattenVocoder.
    */
    void flattenFormant(const float* input, float* output, int numSamples,
                        double sr, const float* ratios, int numRatios,
                        int hopSize, int fftSize);

    /** Triangular spectral smoothing for envelope extraction.
        @param smoothWidth  one-sided triangular half-width in bins.
    */
    static void spectralSmooth(const float* mag, int halfSize,
                               float* envelope, int smoothWidth);

    //==============================================================================
    // Signal-processing utility

    /** Wrap phase angle to [-pi, pi] via IEEE remainder. */
    inline float princArg(float x) noexcept {
        return std::remainderf(x, 6.2831853071795864769f);
    }

    /** Median filter for pitch curve post-processing. */
    void medianFilter(std::vector<float>& data, int windowSize);

    /** Grow a vector to at least @p needed elements (never shrinks). */
    template<typename T>
    static void resizeIfSmaller(std::vector<T>& v, size_t needed) {
        if (v.size() < needed)
            v.resize(needed);
    }

    //==============================================================================
    // State
    int    rootNote_    = 60;        // C4
    float  rootFineTune_ = 0.0f;    // cents
    double sampleRate_  = 44100.0;
    int    fftSize_     = 2048;
    int    hopSize_     = 512;      // fftSize / 4

    PitchDetectionResult lastResult_;

    //==============================================================================
    // Pre-allocated scratch buffers — no heap allocations in flatten path.
    // Sized for worst-case fftSize and never shrink.

    std::unique_ptr<juce::dsp::FFT> fft_;

    // Hann analysis window (pre-computed).
    std::vector<float> hannWindow_;

    // FFT work buffer (real[fftSize] + imag[fftSize] packed).
    mutable std::vector<float> scratch_frame_;

    // Overlap-add accumulator.
    mutable std::vector<float> scratch_accum_;

    // Phase-vocoder state.
    mutable std::vector<float> scratch_prevPhase_;
    mutable std::vector<float> scratch_outPhase_;

    // Spectral scratch.
    mutable std::vector<float> scratch_mag_;
    mutable std::vector<float> scratch_envelope_;
    mutable std::vector<float> scratch_fineStruct_;
    mutable std::vector<float> scratch_shiftedFine_;

    // YIN working buffer (double precision for cumulative mean).
    mutable std::vector<double> scratch_yinDiff_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleProcessor)
};

} // namespace ana
