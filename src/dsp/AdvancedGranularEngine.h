#pragma once

#include <vector>
#include <map>
#include <string>
#include <random>
#include <juce_audio_basics/juce_audio_basics.h>

#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/** Per-grain configuration for the Advanced Granular Engine.
    Each grain carries independent control over source position, pitch, pan,
    timing, amplitude, and a per-grain lowpass filter.
*/
struct Grain
{
    // Source position
    float startSample = 0.0f;       // start position in source audio (samples)
    float duration = 0.1f;          // grain duration (seconds)
    float envelope = 1.0f;          // overall gain envelope

    // Spatial
    float pan = 0.0f;               // -1 to 1
    float spread = 0.0f;            // stereo width

    // Pitch
    float pitchShift = 1.0f;        // grain playback pitch (0.25 to 4.0)
    float pitchRandom = 0.0f;       // random pitch deviation

    // Timing
    float delay = 0.0f;             // delay before grain starts (seconds)
    float playbackSpeed = 1.0f;     // speed within the grain (excludes pitch shift)
    bool reversed = false;          // play grain backwards

    // DSP
    float filterCutoff = 20000.0f;  // lowpass per grain
    float filterResonance = 0.0f;
    float amplitude = 0.5f;         // per-grain amplitude
};

//==============================================================================
/**
    An L-System (Lindenmayer system) for generating fractal-like grain
    trajectories. The generated string is interpreted as a sequence of
    commands:
        F  = create a grain at the current time position
        +  = pan right for the next grain
        -  = pan left
        [  = save state (push current time/pan)
        ]  = restore state (pop time/pan)
*/
class LSystem
{
public:
    LSystem() = default;

    void setAxiom(const std::string& axiom);
    void addRule(char symbol, const std::string& replacement);
    void setIterations(int n);

    /** Generates the L-System string by iteratively applying rewrite rules. */
    std::string generate() const;

    /** Interprets an L-System string as a vector of Grain configurations.
        @param lstring        The L-System string to interpret
        @param totalDuration  Total time span of the resulting grain cloud
        @param numGrains      Maximum number of grains to produce
    */
    std::vector<Grain> interpretAsGrains(const std::string& lstring,
                                          float totalDuration,
                                          int numGrains) const;

private:
    std::string axiom_ = "F";
    std::map<char, std::string> rules_;
    int iterations_ = 3;
};

//==============================================================================
/**
    A cloud of grains with various generation strategies.
    Acts as a container and factory for Grain definitions that are
    subsequently passed to the AdvancedGranularEngine for playback.
*/
class GrainCloud
{
public:
    std::vector<Grain> grains;

    /** Fills the cloud with @a count randomly-placed grains. */
    void generateRandom(int count, float duration, float chaos);

    /** Fills the cloud by interpreting an L-System trajectory. */
    void generateLSystem(const LSystem& lsys, float duration, int grains);

    /** Fills the cloud with grains arranged in a spiral pattern. */
    void generateSpiral(int count, float duration, float radius);

    /** Applies random chaos to all existing grain parameters. */
    void applyChaos(float amount);

    /** Applies rhythmic syncopation by shifting grain delays. */
    void applySyncopation(float amount);

    /** Randomly scatters grain parameters within given ranges. */
    void scatter(float timeAmount, float pitchAmount, float panAmount);
};

//==============================================================================
/**
    A next-generation granular synthesis engine with L-System trajectories,
    particle cloud effects, and per-grain DSP processing.

    Manages up to 1024 simultaneous grains, each with independent pitch, pan,
    amplitude, window envelope, and lowpass filter. Grains are defined through
    a GrainCloud and scheduled for playback based on per-grain delay times.
*/
class AdvancedGranularEngine
{
public:
    AdvancedGranularEngine();
    ~AdvancedGranularEngine();

    //==============================================================================
    /** Sets the source audio buffer and its sample rate. */
    void setSourceBuffer(const std::vector<float>& audio, double sampleRate);

    //==============================================================================
    /** Replaces the current grain cloud with @a cloud. */
    void setGrainCloud(const GrainCloud& cloud);

    /** Sets the maximum number of simultaneous grains (1 to 1024). */
    void setNumGrains(int numGrains);

    /** Auto-calculates numGrains from a desired grain density. */
    void setDensity(float grainsPerSecond);

    /** Sets the overall chaos level (0.0 to 1.0). */
    void setChaos(float chaos);

    //==============================================================================
    /** Per-grain randomisation (0.0 to 1.0). */
    void setPitchRandom(float amount);
    void setPanRandom(float amount);
    void setAmpRandom(float amount);
    void setReverseProb(float probability);
    void setStutterProb(float probability);

    //==============================================================================
    /** Grain envelope shape controls. */
    void setGrainDuration(float ms);
    void setGrainAttack(float percent);
    void setGrainDecay(float percent);

    //==============================================================================
    /** Panning controls. */
    void setPanSpread(float spread);
    void setPanPosition(float pan);

    //==============================================================================
    /** Generates granular output into the provided buffer.
        Clears the buffer before writing. Grains are scheduled and rendered
        with individual envelopes, panning, and DSP.
    */
    void process(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    /** Resets all grain state and clears the active grain list. */
    void reset();

    /** Clears the grain cloud and any active grains. */
    void clearCloud();

private:
    //==============================================================================
    /** Runtime state for a single active grain. */
    struct GrainState
    {
        Grain  config;              // original configuration
        double sourcePosition;      // current read position in source (fractional samples)
        int    currentSample;       // sample index within the grain (0 .. durationSamples-1)
        int    durationSamples;     // total grain length in samples
        float  pitchRatio;          // effective pitch ratio (includes randomness)
        float  amplitude;           // effective amplitude (includes randomness)
        float  panL;                // left pan coefficient
        float  panR;                // right pan coefficient
        bool   active;              // true while the grain is rendering
    };

    //==============================================================================
    void updateGrains(int numSamples);
    void renderGrain(const Grain& grain, juce::AudioBuffer<float>& buffer, int startSample);
    float getGrainEnvelope(float position, const Grain& grain) const;
    void generateWindowTable();
    float interpolateSource(double position) const;

    //==============================================================================
    std::vector<float> sourceBuffer_;
    double sourceSampleRate_ = 44100.0;

    // Cloud
    GrainCloud cloud_;
    std::vector<Grain> activeGrains_;    // grains currently active (from cloud)

    // Runtime state
    std::vector<GrainState> grainStates_;
    size_t nextGrainToActivate_ = 0;

    // Parameters
    int    maxGrains_          = 256;
    float  density_            = 50.0f;
    float  chaos_              = 0.0f;
    float  pitchRandom_        = 0.0f;
    float  panRandom_          = 0.0f;
    float  ampRandom_          = 0.0f;
    float  reverseProb_        = 0.0f;
    float  stutterProb_        = 0.0f;
    float  grainDurationMs_    = 100.0f;
    float  grainAttack_        = 10.0f;
    float  grainDecay_         = 30.0f;
    float  panSpread_          = 1.0f;
    float  panPosition_        = 0.0f;

    // Precomputed window table
    std::vector<float> windowTable_;

    // Scheduling / RNG
    double  sampleRate_           = 44100.0;
    int64_t totalSamplesProcessed_ = 0;
    std::mt19937 rng_;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvancedGranularEngine)
};

} // namespace ana
