#pragma once

#include <vector>
#include <cmath>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "PartialDataSIMD.h"
#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/**
    Real-time Spectrum Freezer for additive synthesis.

    Captures and manipulates the spectral state of an additive synthesis engine
    in four distinct modes:

        - Snapshot:   Instantaneous capture — freezes the spectrum at the moment
                      of trigger and holds it indefinitely.
        - Accumulate: Gradual spectral buildup — merges incoming energy into
                      the frozen state over time.
        - Motion:     Evolving freeze — plays back a ring buffer of recent
                      spectral frames at a controlled rate, producing a
                      "moving" frozen spectrum.
        - Reverse:    Reverse freeze — records spectral history and plays it
                      back backwards.

    The frozen spectrum can be further sculpted via pitch shifting (frequency
    scaling), spectral blurring (Gaussian convolution across partial bins),
    and spectral tilting (amplitude scaling by frequency).

    Two processing paths are provided:
        - process()        — operates on PartialDataSIMD (additive partials)
        - processAudio()   — operates on AudioBuffer<float> (raw signal)
*/
class SpectralFreezeEngine
{
public:
    //==============================================================================
    enum class FreezeMode
    {
        Snapshot,     ///< Instantaneous capture — freeze at moment of trigger
        Accumulate,   ///< Gradual buildup — merge incoming energy into frozen state
        Motion,       ///< Evolving freeze — slow evolution of frozen spectrum
        Reverse       ///< Reverse freeze — play captured spectrum backwards
    };

    //==============================================================================
    SpectralFreezeEngine();
    ~SpectralFreezeEngine() = default;

    //==============================================================================
    /** @name Freeze Control */
    //@{
    /** Set the freeze algorithm mode.  May be changed at any time,
        including while frozen. */
    void setFreezeMode(FreezeMode mode);

    /** Toggle the freeze engine on or off.
        When turned on the current spectral state is captured immediately.
        When turned off the mix smoothly crossfades back to the live signal. */
    void setFreeze(bool shouldFreeze);

    /** One-shot freeze: resets any existing frozen state and captures
        the current spectrum.  Equivalent to setFreeze(false) followed
        by setFreeze(true) in a single call. */
    void triggerFreeze();

    /** Set where in the source history to freeze (0.0 = earliest, 1.0 = latest).
        @param normalizedTime  Normalised position in the source buffer. */
    void setFreezePoint(float normalizedTime);
    //@}

    //==============================================================================
    /** @name Frozen Spectrum Manipulation */
    //@{
    /** Pitch shift the frozen spectrum in semitones (-24 to +24).
        Positive values transpose upward (higher pitch). */
    void setPitchShift(float semitones);

    /** Blur the frozen spectrum amplitudes via Gaussian convolution.
        @param amount  0.0 to 1.0  (0 = no blur, 1 = maximum blur). */
    void setSpectralBlur(float amount);

    /** Tilt the frozen spectrum amplitude envelope.
        @param tilt  -1.0 to 1.0 (negative = boost lows/cut highs,
                       positive = cut lows/boost highs). */
    void setSpectralTilt(float tilt);

    /** Grain size in milliseconds for Motion mode (10.0 — 500.0).
        Controls how much of the history is blended per read. */
    void setGrainSize(float ms);

    /** Evolution rate for Motion mode (0.01 — 1.0).
        Higher values cause the frozen spectrum to evolve faster. */
    void setEvolutionRate(float rate);
    //@}

    //==============================================================================
    /** @name Mix & Filtering */
    //@{
    /** Wet/dry mix (0.0 — 1.0).
        0.0 = dry only (no frozen signal), 1.0 = fully frozen. */
    void setMix(float mix);

    /** High-pass filter cutoff for the dry (live) signal path. */
    void setDryHP(float freqHz);

    /** Low-pass filter cutoff for the wet (frozen) signal path. */
    void setWetLP(float freqHz);
    //@}

    //==============================================================================
    /** @name Configuration */
    //@{
    /** Set the sample rate.  Needed for time- and frequency-dependent processing. */
    void setSampleRate(double sr);

    /** Set the FFT size for audio buffer processing (must be a power of two). */
    void setFftSize(int size);
    //@}

    //==============================================================================
    /** @name Processing */
    //@{
    /** Process additive synthesis partials through the freeze engine.
        @param partials      Live partial data (read/write — modified in-place).
        @param currentFrame  Global frame counter for timeline-relative playback. */
    void process(PartialDataSIMD& partials, int currentFrame);

    /** Process an audio buffer through the freeze engine.
        The output buffer receives the mix of dry (live) and wet (frozen) signal.
        @param input   Live input audio buffer (read-only).
        @param output  Output audio buffer (written with the processed result). */
    void processAudio(const juce::AudioBuffer<float>& input,
                      juce::AudioBuffer<float>& output);
    //@}

    //==============================================================================
    /** @name State Management */
    //@{
    /** Full reset: clears all frozen state, history, and buffers. */
    void reset();

    /** Clear the currently frozen spectrum without resetting configuration
        or history buffers. */
    void clearFrozen();
    //@}

private:
    //==============================================================================
    /** @name Core Freeze Operations */
    //@{
    void snapshotFreeze(const PartialDataSIMD& current);
    void accumulateFreeze(const PartialDataSIMD& current);
    void motionFreeze(const PartialDataSIMD& current, int frame);
    void reverseFreeze(const PartialDataSIMD& current, int frame);
    //@}

    //==============================================================================
    /** @name Frozen State Application */
    //@{
    void applyFrozenToOutput(PartialDataSIMD& output);
    void recordToHistory(const PartialDataSIMD& partials);
    //@}

    //==============================================================================
    /** @name Spectral Effects */
    //@{
    static void applyPitchShift(PartialDataSIMD& data, float semitones);
    static void applySpectralBlur(PartialDataSIMD& data, float amount,
                                  std::vector<float>& kernel, int& kernelRadius,
                                  bool& kernelDirty);
    static void applySpectralTilt(PartialDataSIMD& data, float tilt);
    static void buildGaussianKernel(std::vector<float>& kernel, int& radius,
                                    int newRadius, float sigma);
    //@}

    //==============================================================================
    /** @name Audio Path Helpers */
    //@{
    void updateFilters();
    //@}

    //==============================================================================
    // Frozen spectrum history (for Reverse and Motion modes)
    struct FrozenFrame
    {
        std::vector<float> magnitudes;
        std::vector<float> phases;
    };
    std::vector<FrozenFrame> frozenHistory_;

    // Current frozen state
    PartialDataSIMD frozenPartials_;
    PartialDataSIMD originalFrozenPartials_;  ///< Unprocessed capture (base for spectral effects)

    bool  isFrozen_         = false;
    bool  pendingFreeze_    = false;
    int   freezeFrameIndex_ = 0;
    int   frameCount_       = 0;

    // Mode and mix
    FreezeMode mode_        = FreezeMode::Snapshot;
    float      mix_         = 0.5f;

    // Spectral manipulation
    float pitchShift_     = 0.0f;
    float spectralBlur_   = 0.0f;
    float spectralTilt_   = 0.0f;
    float grainSizeMs_    = 100.0f;
    float evolutionRate_  = 0.1f;
    float freezePoint_    = 0.0f;

    // Sample rate / FFT
    double sampleRate_     = 44100.0;
    int    fftSize_        = 2048;

    // Dry/wet filters
    float dryHP_           = 20.0f;
    float wetLP_           = 20000.0f;
    bool  filtersDirty_    = true;

    // Motion mode — ring buffer of spectral frames
    std::vector<std::vector<float>> motionFrames_;
    int  motionFramePosition_ = 0;
    int  motionReadPosition_  = 0;
    int  motionMaxFrames_     = 512;

    // Smooth crossfade envelope (avoids clicks on freeze/unfreeze)
    float currentMix_      = 0.0f;
    float crossfadeRate_   = 0.005f;  // ~5 ms at 44.1 kHz

    // Gaussian kernel for spectral blur
    std::vector<float> gaussianKernel_;
    int                kernelRadius_ = 0;
    bool               kernelDirty_  = true;

    // Audio buffer freeze state
    std::vector<float> frozenAudioBuffer_;
    int                audioWritePos_ = 0;
    int                audioReadPos_  = 0;

    // Audio path filters (per-channel)
    std::vector<juce::dsp::IIR::Filter<float>> dryHPFilters_;
    std::vector<juce::dsp::IIR::Filter<float>> wetLPFilters_;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFreezeEngine)
};

} // namespace ana
