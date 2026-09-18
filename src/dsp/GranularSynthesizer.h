#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>
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
    /** Reserves the internal window-table cache for @a maxSamples window values.

        Call this from prepareToPlay()/setSourceBuffer() so process() never
        allocates: the cache is only ever resize()d to the current grain
        duration, which stays within the reserved capacity.
    */
    void reserveWindowCache(int maxSamples);

    /** Capacity of the window-table cache (diagnostics/tests). */
    int getWindowCacheCapacity() const noexcept;

    //==============================================================================
    /** One sounding grain, normalised for a view: it never mentions sample
        rates or buffer sizes, so the UI can draw it directly.  This is live
        visualisation data - the audio thread keeps moving between two reads,
        so a caller on another thread must tolerate a value shifting under it
        (the processor publishes a guarded copy for exactly that reason).
    */
    struct GrainSnapshot
    {
        float position  = 0.0f;   // current read position in the source, 0..1
        float duration  = 0.0f;   // grain length as a fraction of the source
        float progress  = 0.0f;   // 0 = just spawned, 1 = about to finish
        float amplitude = 0.0f;   // grain amplitude, 0..1
        float pan       = 0.0f;   // -1 = left, +1 = right
    };

    //==============================================================================
    /** Buckets in the source envelope the GRAIN page draws. */
    static constexpr int kSourcePeakBuckets = 256;

    /** Copies the loaded source's peak envelope into @a out (max |x| per
        bucket, low index = start of the sample) and returns how many values
        were written.  Allocation-free; count 0 means no source is loaded.
    */
    int getSourcePeaks(float* out, int maxCount) const noexcept;

    /** Copies up to @a maxCount active grains into @a out (pool order) and
        returns how many were written.  Allocation-free and lock-free, so it is
        safe on the audio thread.
    */
    int getActiveGrainSnapshots(GrainSnapshot* out, int maxCount) const noexcept;

    /** Returns the number of currently active grains. */
    int getActiveGrainCount() const;

    /** Returns the total number of grains spawned since last reset. */
    int getTotalGrainsSpawned() const;

    /** Returns the current sample rate. */
    double getSampleRate() const;

private:
    // Source envelope for the display.  The count is published with a release
    // store after the buckets are filled, so a reader that sees a non-zero
    // count also sees a filled array.
    float sourcePeaks_[kSourcePeakBuckets] = {};
    std::atomic<int> sourcePeakCount_{ 0 };

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

    /** Drop inactive slots from the top of the scanned grain prefix. */
    void shrinkScannedGrains() noexcept;
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

    // Grains live at the LOW end of the pool: spawnGrain() always takes the
    // lowest free slot, so scanning [0, scanCount_) visits every grain that can
    // be active.  The audio loop used to walk all 256 slots per sample, i.e.
    // >12 M mostly-skipping iterations per second at 48 kHz, while a default
    // patch (10 grains/s x 50 ms) holds well under one active grain.
    int scanCount_ = 0;

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
