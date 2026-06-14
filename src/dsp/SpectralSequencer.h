#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <random>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PartialDataSIMD.h"
#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/** Per-step spectral processing parameters for the step sequencer.

    Each step can independently control pitch, time, filter, spectral,
    and effects parameters applied to partial data.
*/
struct SpectralStep
{
    bool active = true;

    // Pitch
    float pitchShift      = 1.0f;    // frequency multiplier (0.25 to 4.0)
    float pitchQuantize   = 0.0f;    // snap to nearest scale degree (0=off)

    // Time
    float timeStretch     = 1.0f;    // time stretch factor (0.25 to 4.0)
    float grainSize       = 100.0f;  // grain size in ms for time manipulation

    // Filter
    float filterCutoff    = 20000.0f; // Hz
    float filterResonance = 0.0f;    // Q factor

    // Spectral
    float spectralBlur    = 0.0f;    // cross-partial blur amount (0-1)
    float spectralTilt    = 0.0f;    // high-frequency tilt (-1 to 1)
    float formantShift    = 0.0f;    // formant shift in semitones

    // Effects
    float feedback        = 0.0f;    // spectral feedback (0-1)
    float noise           = 0.0f;    // additive noise amount (0-1)
    float bitcrush        = 0.0f;    // frequency quantization (0-1)

    // Amplitude
    float gain            = 1.0f;    // per-step gain

    // Duration
    int numBeats          = 1;       // how many beats this step lasts

    // Comparison for change detection
    bool operator==(const SpectralStep& o) const noexcept
    {
        return active         == o.active
            && pitchShift     == o.pitchShift
            && pitchQuantize  == o.pitchQuantize
            && timeStretch    == o.timeStretch
            && grainSize      == o.grainSize
            && filterCutoff   == o.filterCutoff
            && filterResonance == o.filterResonance
            && spectralBlur   == o.spectralBlur
            && spectralTilt   == o.spectralTilt
            && formantShift   == o.formantShift
            && feedback       == o.feedback
            && noise          == o.noise
            && bitcrush       == o.bitcrush
            && gain           == o.gain
            && numBeats       == o.numBeats;
    }

    bool operator!=(const SpectralStep& o) const noexcept
    {
        return !(*this == o);
    }

    /** Linear interpolation between two steps. */
    static SpectralStep lerp(const SpectralStep& a, const SpectralStep& b, float t) noexcept
    {
        SpectralStep r;
        r.active          = t < 0.5f ? a.active : b.active;
        r.pitchShift      = a.pitchShift     + (b.pitchShift     - a.pitchShift)     * t;
        r.pitchQuantize   = a.pitchQuantize  + (b.pitchQuantize  - a.pitchQuantize)  * t;
        r.timeStretch     = a.timeStretch    + (b.timeStretch    - a.timeStretch)    * t;
        r.grainSize       = a.grainSize      + (b.grainSize      - a.grainSize)      * t;
        r.filterCutoff    = a.filterCutoff   + (b.filterCutoff   - a.filterCutoff)   * t;
        r.filterResonance = a.filterResonance+ (b.filterResonance- a.filterResonance)* t;
        r.spectralBlur    = a.spectralBlur   + (b.spectralBlur   - a.spectralBlur)   * t;
        r.spectralTilt    = a.spectralTilt   + (b.spectralTilt   - a.spectralTilt)   * t;
        r.formantShift    = a.formantShift   + (b.formantShift   - a.formantShift)   * t;
        r.feedback        = a.feedback       + (b.feedback       - a.feedback)       * t;
        r.noise           = a.noise          + (b.noise          - a.noise)          * t;
        r.bitcrush        = a.bitcrush       + (b.bitcrush       - a.bitcrush)       * t;
        r.gain            = a.gain           + (b.gain           - a.gain)           * t;
        r.numBeats        = t < 0.5f ? a.numBeats : b.numBeats;
        return r;
    }
};

//==============================================================================
/**
    A step sequencer that applies per-step spectral processing parameters
    to partial data.  Each step can independently control pitch shift,
    spectral filtering, blur, tilt, formant shift, feedback, noise,
    bitcrushing, and gain.

    The sequencer advances through steps at a rate determined by tempo,
    beat division, and per-step beat duration.  Transitions between steps
    are crossfaded to avoid zipper noise.
*/
class SpectralSequencer
{
public:
    SpectralSequencer();
    ~SpectralSequencer() = default;

    //==============================================================================
    // Step management
    void setNumSteps(int numSteps);              // 1 to 64
    void setStep(int index, const SpectralStep& step);
    const SpectralStep& getStep(int index) const;
    SpectralStep& getStep(int index);

    //==============================================================================
    // Global parameters
    void setTempo(float bpm);                    // beats per minute
    void setSwing(float amount);                 // 0.0 to 1.0 (swing amount)
    void setStepLength(int samples);             // override samples per step (not beats)
    void setStepLengthBeats(float beats);        // beats per step
    void setBeatDivision(int division);          // 1/4, 1/8, 1/16 notes

    //==============================================================================
    // Transport
    void start();
    void stop();
    bool isPlaying() const;
    void resetPosition();
    void setPosition(int step);                  // seek to step

    //==============================================================================
    // Randomization
    void randomizeSteps(float amount);           // 0.0 to 1.0
    void randomizeStep(int index, float amount);

    //==============================================================================
    // Preset patterns
    enum class Preset { Off, Gate, FilterSweep, PitchRise, Chaos, FormantWobble, HarmonicSweep };
    void loadPreset(Preset preset);

    //==============================================================================
    // Processing
    void process(PartialDataSIMD& partials, int numSamples);
    void processAudio(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    // Get current step info
    int getCurrentStep() const;
    float getStepProgress() const;               // 0.0 to 1.0 within current step

    //==============================================================================
    // Reset
    void reset();

    //==============================================================================
    // Sample rate
    void setSampleRate(double sampleRate);

private:
    // Step advancement
    void advanceStep();
    void computeStepLength();
    int getEffectiveStepLength(int stepIndex) const;

    // Step application
    void applyStep(const SpectralStep& step, PartialDataSIMD& partials);
    void applyStepAudio(const SpectralStep& step, juce::AudioBuffer<float>& buffer);

    // Individual effect processors (called by applyStep)
    void applyPitchShift(const SpectralStep& step, PartialDataSIMD& partials);
    void applyPitchQuantize(const SpectralStep& step, PartialDataSIMD& partials);
    void applySpectralFilter(const SpectralStep& step, PartialDataSIMD& partials);
    void applySpectralBlur(const SpectralStep& step, PartialDataSIMD& partials);
    void applySpectralTilt(const SpectralStep& step, PartialDataSIMD& partials);
    void applyFormantShift(const SpectralStep& step, PartialDataSIMD& partials);
    void applyFeedback(const SpectralStep& step, PartialDataSIMD& partials);
    void applyNoise(const SpectralStep& step, PartialDataSIMD& partials);
    void applyBitcrush(const SpectralStep& step, PartialDataSIMD& partials);
    void applyGain(const SpectralStep& step, PartialDataSIMD& partials);

    // Preset loaders
    void loadPresetGate();
    void loadPresetFilterSweep();
    void loadPresetPitchRise();
    void loadPresetChaos();
    void loadPresetFormantWobble();
    void loadPresetHarmonicSweep();

    //==============================================================================
    // Steps
    std::vector<SpectralStep> steps_;
    int numSteps_ = 16;

    // Transport state
    bool playing_ = false;
    int currentStep_ = 0;
    int totalSamplesInStep_ = 0;
    int stepLengthSamples_ = 0;
    int beatDivision_ = 16;              // 16th notes
    float tempo_ = 120.0f;
    float swing_ = 0.0f;
    float stepLengthBeats_ = 1.0f;

    // Sample rate
    double sampleRate_ = 44100.0;

    // Step length override (0 = use computed)
    int stepLengthOverride_ = 0;

    // Crossfade state
    SpectralStep prevStep_;
    float crossfadeSamples_ = 0.0f;
    int crossfadeCount_ = 0;

    // Feedback buffer (holds previous partial state)
    float feedbackFreq_[PartialDataSIMD::kMaxPartials]{};
    float feedbackAmp_[PartialDataSIMD::kMaxPartials]{};

    // Pre-allocated scratch buffers (no heap alloc in audio thread)
    struct PartialEntry { int index; float freq; float amp; };
    mutable std::vector<float> scratch_weights_;
    mutable std::vector<int> scratch_activeIndices_;
    mutable std::vector<float> scratch_blurredAmp_;
    mutable std::vector<PartialEntry> scratch_partialEntries_;

    // Random number generator for noise / randomize
    std::mt19937 rng_{42};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralSequencer)
};

} // namespace ana
