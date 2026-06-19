#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace ana {

/**
 * Per-voice state for the unison engine.
 * Each unison voice maintains independent oscillator state.
 */
struct UnisonVoice
{
    float phase = 0.0f;          // Current phase accumulator [0, 2pi) (kept for test compat)
    float initialPhase = 0.0f;   // Starting phase offset set on noteOn
    float detuneCents = 0.0f;    // Detune offset from center pitch
    float pan = 0.0f;            // Pan position [-1 .. +1]

    // Recursive phasor state (replaces per-sample std::sin in the render loop)
    float phasorRe = 1.0f;       // Real part of phasor (cos)
    float phasorIm = 0.0f;       // Imag part of phasor (sin)
    float cosDelta = 1.0f;       // cos(phaseDelta) per-block
    float sinDelta = 0.0f;       // sin(phaseDelta) per-block

    // Precomputed voice parameters (set once in updateVoices, not per-sample)
    float detuneRatio = 1.0f;    // std::pow(2, detuneCents/1200)
    float leftGain  = 1.0f;      // std::sqrt(0.5 * (1 - pan))
    float rightGain = 1.0f;      // std::sqrt(0.5 * (1 + pan))
};

/**
 * UnisonEngine generates detuned, stereo-spread unison voices
 * for a synthesizer.  Supports 1-8 voices per note with independent
 * oscillator state, configurable detune, stereo spread, and phase
 * offset.
 */
class UnisonEngine
{
public:
    UnisonEngine();
    ~UnisonEngine() = default;

    // --- Life-cycle --------------------------------------------------------

    /** Prepare the engine for playback at a given sample rate and block size. */
    void prepare(double sampleRate, int blockSize);

    /** Reset all voice state (phases zeroed, initial phases cleared). */
    void reset();

    /** Called when a new note starts.  Re-rolls initial phases if
        phaseOffset > 0. */
    void noteOn();

    // --- Parameters --------------------------------------------------------

    /** Set the number of unison voices (1-8, clamped). */
    void setVoiceCount(int count);

    /** Set the detune range in cents per voice-step.
        E.g. 12 cents with 8 voices = +/- 48 cents total spread. */
    void setDetune(float cents);

    /** Set stereo spread as a percentage (0-100 %). */
    void setStereoSpread(float percent);

    /** Set the amount of random initial phase offset (0-1).
        0 = all voices start at the same phase;
        1 = fully randomised across [0, 2pi). */
    void setPhaseOffset(float amount);

    /** Set the base frequency in Hz (e.g. MIDI note frequency). */
    void setFrequency(float freqHz);

    // --- Accessors ---------------------------------------------------------

    int getVoiceCount() const noexcept { return voiceCount_; }
    float getDetune() const noexcept { return detuneCents_; }
    float getStereoSpread() const noexcept { return stereoSpread_; }
    float getPhaseOffset() const noexcept { return phaseOffset_; }

    /** Expose a single voice (useful for testing / inspection). */
    const UnisonVoice& getVoice(int index) const noexcept { return voices_[index]; }

    // --- Processing --------------------------------------------------------

    /** Fill the buffer with all unison voices summed together.
        The buffer should have at least 2 channels for stereo output. */
    void process(juce::AudioBuffer<float>& buffer);

private:
    /** Recalculate per-voice detune and pan values after parameter changes. */
    void updateVoices();

    std::vector<UnisonVoice> voices_;

    double sampleRate_ = 44100.0;
    float frequency_ = 440.0f;

    int voiceCount_ = 1;
    float detuneCents_ = 0.0f;
    float stereoSpread_ = 0.0f;
    float phaseOffset_ = 0.0f;

    juce::Random random_;
    bool voicesValid_ = false;
};

} // namespace ana
