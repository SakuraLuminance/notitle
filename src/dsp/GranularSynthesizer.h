#pragma once

#include <vector>
#include <random>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana {

//==============================================================================
/** Window shapes applied to each grain envelope. */
enum class GrainWindowType
{
    Hann,
    Triangle,
    Gaussian,
    Sinc
};

//==============================================================================
/** Position modulation modes for varying grain read position. */
enum class PositionModulation
{
    Off,
    LFO,
    Envelope,
    Random
};

//==============================================================================
/**
    A real-time granular synthesis engine for texture-based sample processing.

    Spawns and manages up to 256 simultaneous grains with independent control
    over grain size, density, pitch, position, amplitude, and pan. Supports
    multiple window shapes and position modulation strategies (LFO, envelope,
    random walk).

    Usage:
        GranularSynthesizer synth;
        synth.setSourceBuffer(myAudioData, 44100.0);
        synth.setDensity(50.0f);        // 50 grains/second
        synth.setGrainSize(30.0f);      // 30ms per grain
        synth.setPosition(0.5f);        // middle of source
        synth.setPitch(0.0f);           // original pitch

        juce::AudioBuffer<float> output(2, 512);
        synth.process(output);          // fills buffer with granular output
*/
class GranularSynthesizer
{
public:
    GranularSynthesizer();
    ~GranularSynthesizer();

    //==============================================================================
    /** Sets the source audio buffer and its sample rate.
        @param buffer  Audio samples to granulate (must not be empty)
        @param sampleRate  Sample rate of the source buffer
    */
    void setSourceBuffer(const std::vector<float>& buffer, double sampleRate);

    //==============================================================================
    /** Sets the grain duration in milliseconds. Clamped to [1, 100].
        @param ms  Grain duration in milliseconds
    */
    void setGrainSize(float ms);

    /** Sets the average grain spawn density. Clamped to [1, 1000].
        @param grainsPerSec  Number of grains to spawn per second
    */
    void setDensity(float grainsPerSec);

    /** Sets the normalized playback position in the source. Clamped to [0, 1].
        @param normalizedPosition  0 = start of buffer, 1 = end of buffer
    */
    void setPosition(float normalizedPosition);

    /** Sets pitch shift in semitones. Clamped to [-24, +24].
        @param semitones  Positive = higher pitch, negative = lower
    */
    void setPitch(float semitones);

    /** Sets master output amplitude. Clamped to [0, 1].
        @param amp  Output gain scaling factor
    */
    void setAmplitude(float amp);

    /** Sets stereo pan. Clamped to [-1, +1].
        @param pan  -1 = full left, 0 = center, +1 = full right
    */
    void setPan(float pan);

    /** Sets the window shape applied to each grain envelope. */
    void setWindowType(GrainWindowType type);

    /** Configures position modulation.
        @param mod    Modulation mode
        @param depth  Modulation depth (fraction of buffer, 0-1)
        @param rate   Modulation rate in Hz
    */
    void setPositionModulation(PositionModulation mod, float depth = 0.1f, float rate = 1.0f);

    //==============================================================================
    /** Generates granular output into the provided buffer.
        Clears the buffer before writing. Grain scheduling, overlap-add
        synthesis, windowing, and panning happen inside this call.

        @param output  Audio buffer to fill (stereo or mono)
    */
    void process(juce::AudioBuffer<float>& output);

    /** Resets all grain state, accumulators, and LFO phase. */
    void reset();

    //==============================================================================
    /** Returns the number of currently active grains. */
    int getActiveGrainCount() const;

    /** Returns the total number of grains spawned since last reset. */
    int getTotalGrainsSpawned() const;

    /** Returns the current sample rate. */
    double getSampleRate() const;

private:
    //==============================================================================
    struct InternalGrain
    {
        double sourcePosition;     // current read position in source (fractional samples)
        int    currentSample;      // sample index within the grain (0 .. durationSamples-1)
        int    durationSamples;    // total grain length in samples
        double pitchRatio;         // playback speed ratio (1.0 = original)
        float  amplitude;          // grain amplitude
        float  panL;               // left-channel pan gain
        float  panR;               // right-channel pan gain
        GrainWindowType windowType;
        bool   active;
    };

    //==============================================================================
    bool spawnGrain();
    float getWindowValue(int sampleIndex, int duration, GrainWindowType type) const;
    float getCachedWindowValue(int sampleIndex, int duration, GrainWindowType type) const;
    float interpolateSource(double position) const;

    //==============================================================================
    std::vector<float> sourceBuffer;
    double sampleRate_ = 44100.0;

    // Grain parameters
    float grainSizeMs_    = 50.0f;
    float density_        = 10.0f;
    float position_       = 0.5f;
    float pitchSemitones_ = 0.0f;
    float amplitude_      = 0.5f;
    float pan_            = 0.0f;
    GrainWindowType windowType_ = GrainWindowType::Hann;

    // Position modulation
    PositionModulation posMod_        = PositionModulation::Off;
    float              posModDepth_   = 0.1f;
    float              posModRate_    = 1.0f;
    double             lfoPhase_      = 0.0;

    // Scheduling state
    double grainAccumulator_    = 0.0;
    int    totalGrainsSpawned_  = 0;

    // Fixed-size grain pool (no heap allocation during process)
    static constexpr int maxGrains_ = 256;
    InternalGrain grains_[maxGrains_];

    // Window table cache (avoids exp/sin/cos per sample per grain)
    mutable std::vector<float> windowCache_;
    mutable int cachedWindowDuration_ = 0;
    mutable GrainWindowType cachedWindowType_ = GrainWindowType::Hann;

    // Random number generator for PositionModulation::Random
    std::mt19937 rng_;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranularSynthesizer)
};

} // namespace ana
