#pragma once
#include <cmath>
#include <random>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana {

//==============================================================================
/** Playback direction mode for the step sequencer. */
enum class SeqPlayMode
{
    Forward,   /**< Steps 0→15, then wrap. */
    Backward,  /**< Steps 15→0, then wrap. */
    PingPong,  /**< 0→15→0→15... */
    Random     /**< Random step each trigger. */
};

//==============================================================================
/** Clock source selection for the step sequencer. */
enum class SeqClockSource
{
    Internal,  /**< Internal BPM-based clock. */
    External   /**< External MIDI clock (reset via trigger()). */
};

//==============================================================================
/** A single step in the 16-step sequencer pattern. */
struct SeqStep
{
    bool  active = false;  /**< Gate on/off (true = sound passes). */
    float value = 0.0f;    /**< CV value 0.0–1.0 for modulation depth. */
};

//==============================================================================
/**
    16-step sequencer with per-step gate and CV output.

    Produces a stepped control-voltage signal synchronised to an internal
    tempo (BPM) or an external MIDI clock. The current step's `.value` is
    output as a normalised float in [0, 1] that can be routed via the
    ModulationBus to any modulatable parameter.

    Play modes: Forward, Backward, PingPong, Random.

    Usage:
        StepSequencer seq;
        seq.prepare(44100.0);
        seq.setBpm(120.0);
        seq.setRateBeats(0.25f);          // 16th notes

        seq.setStep(0, true, 0.0f);
        seq.setStep(1, true, 0.5f);
        // ... (steps 2..15)

        for (int i = 0; i < numSamples; ++i)
            seq.process(1);

        float cv = seq.getCurrentValue();   // current CV output
        bool  g  = seq.isGateHigh();        // current gate state

        // Route via ModulationBus:
        //   bus.addRoute(ModulationBus::Source::Sequencer, 0, "paramId",
        //                &targetAtomic, &seq.currentValueRef(), 1.0f);
*/
class StepSequencer
{
public:
    StepSequencer();
    ~StepSequencer() = default;

    //==============================================================================
    /** Must be called before processing. Resets counters. */
    void prepare(double sampleRate);

    /** Resets step counter to the beginning. */
    void reset();

    //==============================================================================
    /** Sets the internal tempo in BPM.
        @param bpm  Beats per minute (clamped to 1.0–999.0)
    */
    void setBpm(double bpm);

    /** Returns the current tempo. */
    double getBpm() const noexcept { return bpm_; }

    //==============================================================================
    /** Sets the rate as a note division.
        @param beats  1.0 = quarter note, 0.5 = eighth, 0.25 = sixteenth, etc.
    */
    void setRateBeats(float beats);

    /** Returns the current beat division. */
    float getRateBeats() const noexcept { return rateBeats_; }

    //==============================================================================
    /** Sets the play mode. */
    void setPlayMode(SeqPlayMode mode) noexcept { playMode_ = mode; }

    /** Returns the current play mode. */
    SeqPlayMode getPlayMode() const noexcept { return playMode_; }

    //==============================================================================
    /** Sets the clock source. */
    void setClockSource(SeqClockSource src) noexcept { clockSource_ = src; }

    /** Returns the current clock source. */
    SeqClockSource getClockSource() const noexcept { return clockSource_; }

    //==============================================================================
    /** Enables or disables the sequencer. */
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    /** Returns true if the sequencer is enabled. */
    bool isEnabled() const noexcept { return enabled_; }

    //==============================================================================
    /** External clock tick — call this when an external MIDI clock is received.
        Advances the step counter by one step.
    */
    void externalTick();

    /** Resets the step to 0 and phase accumulator (for external clock start). */
    void trigger();

    //==============================================================================
    /** Pattern editing.
        @param index  0–15 step index
        @param active Gate state (true = on)
        @param value  CV value in [0, 1]
    */
    void setStep(int index, bool active, float value = 0.0f);

    /** Returns a const ref to the step at the given index.
        If the index is out of range, returns a default inactive step.
    */
    const SeqStep& getStep(int index) const;

    //==============================================================================
    /** Advances the sequencer by the given number of audio samples.
        In internal clock mode, this advances the sample counter and
        triggers step advances when the step duration is reached.
        In external clock mode, advances are driven by externalTick().

        @param numSamples  Number of audio samples in the current block.
    */
    void process(int numSamples);

    //==============================================================================
    /** Returns the current CV output value in [0, 1] (gate-on) or 0 (gate-off). */
    float getCurrentValue() const noexcept { return currentValue_; }

    /** Returns the current gate state. */
    bool isGateHigh() const noexcept { return gateHigh_; }

    /** Returns the current step index (0–15). */
    int getCurrentStep() const noexcept { return currentStep_; }

    /** Returns a const pointer to the current value for ModulationBus routing
        (must remain valid for the lifetime of the ModulationBus route). */
    const float* getCurrentValuePtr() const noexcept { return &currentValue_; }

private:
    //==============================================================================
    /** Advances to the next step according to the play mode. */
    void advanceStep();

    /** Returns the step duration in samples. */
    double getStepDurationSamples() const noexcept;

    //==============================================================================
    double sampleRate_ = 44100.0;
    double bpm_ = 120.0;
    float rateBeats_ = 0.25f;   // 16th notes default

    SeqPlayMode playMode_ = SeqPlayMode::Forward;
    SeqClockSource clockSource_ = SeqClockSource::Internal;

    bool enabled_ = true;
    bool gateHigh_ = false;
    int currentStep_ = -1;  // -1 = no step active yet; advanceStep() wraps to 0
    float currentValue_ = 0.0f;

    // 16-step pattern
    SeqStep steps_[16];

    // Internal clock state
    double sampleCounter_ = 0.0;
    int direction_ = 1;  // 1 = forward, -1 = backward (for PingPong)

    // Random mode RNG
    std::mt19937 rng_{ std::random_device{}() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencer)
};

} // namespace ana
