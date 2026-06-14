#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <random>

namespace ana {

//==============================================================================
/** Available LFO waveform types. */
enum class WaveformType
{
    Sine,      /**< Pure sine wave. */
    Triangle,  /**< Symmetrical triangle wave (bipolar -1 to 1). */
    Saw,       /**< Rising sawtooth wave. */
    Square,    /**< Square wave (bipolar -1 or 1). */
    Random     /**< Sample & hold — new random value each cycle. */
};

//==============================================================================
/**
    LFO System with multiple waveforms and tempo sync for modulation.

    Generates low-frequency modulation signals with 5 waveform types,
    adjustable rate, depth, phase offset, bipolar/unipolar output, and
    tempo-synchronised rates.

    Waveforms are generated from a normalised phase accumulator that wraps
    at 1.0 (one full cycle). The phase offset shifts the starting position
    of the cycle. In tempo-sync mode the rate is computed from the current
    BPM and beat division.

    Usage:
        LFOSystem lfo;
        lfo.prepare(44100.0);

        lfo.setWaveform(WaveformType::Sine);
        lfo.setRate(5.0f);         // 5 Hz free-running
        lfo.setDepth(75.0f);       // 75 % depth
        lfo.setBipolar(true);

        for (int i = 0; i < 1024; ++i)
        {
            float mod = lfo.process(1);
            // Apply mod to parameter…
        }
*/
class LFOSystem
{
public:
    LFOSystem();
    ~LFOSystem() = default;

    //==============================================================================
    /** Initialises the LFO with the given sample rate.
        Must be called before processing. Resets phase to initial position.
    */
    void prepare(double sampleRate);

    /** Resets the LFO phase to the initial position (taking phase offset into account). */
    void reset();

    //==============================================================================
    /** Sets the waveform type. */
    void setWaveform(WaveformType type) noexcept;

    /** Returns the current waveform type. */
    WaveformType getWaveform() const noexcept;

    //==============================================================================
    /** Sets the LFO rate in Hertz (free-running mode).
        Disables tempo sync when called.
        @param hz  Rate in Hz (clamped to 0.01–100.0)
    */
    void setRate(float hz);

    /** Returns the current free-running rate in Hz. */
    float getRate() const noexcept;

    /** Sets the LFO rate in beats for tempo-synced mode.
        Enables tempo sync.
        @param beats  Beat division: 1.0 = quarter note, 0.5 = eighth note,
                      0.25 = sixteenth note, etc.
    */
    void setRateBeats(float beats);

    /** Returns the current beat division for tempo sync. */
    float getRateBeats() const noexcept;

    //==============================================================================
    /** Sets the modulation depth.
        @param percent  Depth in percent 0–100. 100 % = full range.
    */
    void setDepth(float percent);

    /** Returns the current depth percentage. */
    float getDepth() const noexcept;

    //==============================================================================
    /** Sets the phase offset.
        @param degrees  Phase offset in degrees 0–360.
    */
    void setPhase(float degrees);

    /** Returns the current phase offset in degrees. */
    float getPhase() const noexcept;

    //==============================================================================
    /** Sets bipolar/unipolar output mode.
        @param bipolar  true = bipolar output (-1 to 1), false = unipolar (0 to 1)
    */
    void setBipolar(bool bipolar) noexcept;

    /** Returns true if bipolar mode is enabled. */
    bool isBipolar() const noexcept;

    //==============================================================================
    /** Sets the tempo for beat-synced rate calculation.
        @param bpm  Beats per minute (must be > 0)
    */
    void setTempo(double bpm);

    /** Returns the current tempo in BPM. */
    double getTempo() const noexcept;

    //==============================================================================
    /** Returns true if tempo sync is currently enabled. */
    bool isSyncEnabled() const noexcept;

    //==============================================================================
    /** Advances the LFO by the given number of samples and returns the current value.
        The phase accumulator is advanced, then the waveform value is computed
        at the new phase position.
        @param numSamples  Number of samples to advance
        @return The current LFO value (bipolar: -1 to 1, unipolar: 0 to 1, scaled by depth)
    */
    float process(int numSamples);

    /** Returns the current LFO value without advancing the phase. */
    float getValue() const noexcept;

    //==============================================================================
    /** Returns the current normalised phase position in [0, 1). Useful for diagnostics. */
    double getCurrentPhase() const noexcept;

private:
    //==============================================================================
    /** Computes the raw waveform value from a normalised phase in [0, 1). */
    float computeWaveform(float phase) const;

    /** Generates a new random value uniformly distributed in [-1, 1]. */
    float generateRandomValue();

    /** Returns the effective rate in Hz (respects sync mode). */
    double getEffectiveRate() const noexcept;

    //==============================================================================
    double sampleRate = 44100.0;
    double phase = 0.0;
    float currentValue = 0.0f;

    WaveformType waveform = WaveformType::Sine;
    float rateHz = 1.0f;
    float rateBeats = 1.0f;
    float depthPercent = 100.0f;
    float phaseOffsetDeg = 0.0f;
    double tempo = 120.0;
    bool bipolar = true;
    bool syncEnabled = false;

    float randomValue = 0.0f;
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> dist{ -1.0f, 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LFOSystem)
};

} // namespace ana
