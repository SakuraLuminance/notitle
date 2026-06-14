#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>

namespace ana {

//==============================================================================
/**
 * Voice state machine states for the synthesizer's voice lifecycle.
 *
 * Lifecycle: free -> attack -> decay -> sustain -> release -> idle -> free
 */
enum class VoiceState : uint8_t
{
    free    = 0,  /**< Voice slot is available for allocation. */
    attack  = 1,  /**< Envelope amplitude is rising from 0 to 1. */
    decay   = 2,  /**< Envelope amplitude is falling from 1 to sustain level. */
    sustain = 3,  /**< Envelope is holding at the sustain level. */
    release = 4,  /**< Envelope amplitude is falling to 0 (note-off received). */
    idle    = 5   /**< Envelope reached 0 after release, ready for reuse. */
};

//==============================================================================
/**
 * Voice allocation strategy for choosing which voice slot to assign next.
 */
enum class AllocationMode : uint8_t
{
    roundRobin,  /**< Cycle through voice slots in order. */
    oldestFirst, /**< Pick the oldest free voice slot. */
    random       /**< Pick a random free voice slot. */
};

//==============================================================================
/**
 * Represents a single voice in the synthesizer's polyphonic voice pool.
 *
 * Each voice carries its own note, velocity, pitch, amplitude, phase,
 * envelope level, ADSR parameters, and state machine state.
 */
struct Voice
{
    // --- Voice identity ---
    std::atomic<VoiceState> state{VoiceState::free};
    int        note     = -1;       /**< MIDI note number (0-127), -1 if unused. */
    float      velocity = 0.0f;     /**< Note-on velocity in [0, 1]. */

    // --- Synthesis state ---
    std::atomic<float> pitchHz       { 0.0f }; /**< Oscillator frequency in Hz. */
    std::atomic<float> amplitude     { 0.0f }; /**< Current output amplitude (includes velocity scaling). */
    float              phase         = 0.0f;   /**< Oscillator phase in radians [0, 2pi). */
    std::atomic<float> envelopeLevel { 0.0f }; /**< Current ADSR envelope value in [0, 1]. */

    // --- Per-voice ADSR parameters ---
    float attackSeconds  = 0.01f;   /**< Attack time in seconds [0, 10]. */
    float decaySeconds   = 0.2f;    /**< Decay time in seconds [0, 10]. */
    float sustainLevel   = 0.7f;    /**< Sustain level [0, 1]. */
    float releaseSeconds = 0.3f;    /**< Release time in seconds [0, 10]. */

    // --- Internal bookkeeping ---
    std::atomic<float> releaseStartLevel { 0.0f }; /**< Envelope level when release phase started. */
    uint64_t noteOnIndex       = 0;     /**< Monotonically increasing allocation index for age tracking. */
    float envScale = 1.0f;              /**< Adaptive envelope scaling factor based on pitch. */

    // --- Per-voice modulation (lock-free safe) ---
    std::atomic<float> aftertouch { 0.0f }; /**< Per-voice aftertouch amount [0, 1]. */
    std::atomic<float> pitchBend  { 1.0f }; /**< Per-voice pitch bend multiplier (1.0 = unity, 2.0 = +1 oct). */

    // --- MPE (MIDI Polyphonic Expression) fields ---
    int   midiChannel = -1;    /**< MPE per-note MIDI channel (-1 = not set). */
    float slideAmount = 0.0f; /**< MPE slide (CC74) in [0, 1]. */
    float pressure    = 0.0f; /**< MPE per-note channel pressure [0, 1]. */
    float timbre      = 0.0f; /**< MPE timbre (CC74 mapped to brightness/filter) [0, 1]. */

    // --- State variable filter (for MPE timbre modulation) ---
    float lp0 = 0.0f;  /**< Lowpass state sample (0). */
    float lp1 = 0.0f;  /**< Lowpass state sample (1). */
    float bp0 = 0.0f;  /**< Bandpass state sample (0). */
    float bp1 = 0.0f;  /**< Bandpass state sample (1). */
    float hp0 = 0.0f;  /**< Highpass state sample (0). */
    float hp1 = 0.0f;  /**< Highpass state sample (1). */

    // --- Complex phasor oscillator (lock-free rotation) ---
    float phasorRe = 1.0f;    /**< cos(phase) of complex oscillator. */
    float phasorIm = 0.0f;    /**< sin(phase) of complex oscillator. */
    float cosDelta = 1.0f;    /**< cos(2π * freq / sampleRate) per-sample rotation. */
    float sinDelta = 0.0f;    /**< sin(2π * freq / sampleRate) per-sample rotation. */
};

//==============================================================================
/**
 * Polyphonic voice manager with 32-voice support.
 *
 * Handles voice allocation (round-robin, oldest-first, random), voice stealing,
 * per-voice ADSR envelope processing, and audio output summation.
 *
 * Usage:
 * @code
 *   ana::VoiceManager vm;
 *   vm.prepare(44100.0);
 *   vm.noteOn(60, 0.7f);
 *   vm.noteOff(60);
 *
 *   juce::AudioBuffer<float> buffer(2, 512);
 *   vm.process(buffer);
 * @endcode
 */
class VoiceManager
{
public:
    static constexpr int maxVoices = 32;

    //==============================================================================
    /** Creates a VoiceManager with all voices in the free state. */
    VoiceManager();

    /** Destructor. */
    ~VoiceManager() = default;

    JUCE_DECLARE_NON_COPYABLE(VoiceManager)

    //==============================================================================
    /**
     * Prepares the voice manager for processing at a given sample rate.
     * Must be called before any process() calls.
     */
    void prepare(double sampleRate);

    //==============================================================================
    /**
     * Triggers a note-on event: allocates a voice and starts its attack phase.
     *
     * @param note     MIDI note number (0-127). Clamped to valid range.
     * @param velocity Note-on velocity (0-1). Clamped to valid range.
     */
    void noteOn(int note, float velocity);

    /**
     * Triggers a note-off event: transitions any voice playing the given note
     * into the release phase.
     *
     * @param note MIDI note number (0-127). Clamped to valid range.
     */
    void noteOff(int note);

    /**
     * Triggers a note-off on a specific MPE channel.
     * Only voices matching both channel and note are released.
     *
     * @param midiChannel  MPE per-note channel to target.
     * @param note         MIDI note number (0-127).
     */
    void noteOffWithChannel(int midiChannel, int note);

    /**
     * Immediately transitions all active voices into the release phase.
     * Useful for panic / all-silence functionality.
     */
    void allVoicesOff();

    //==============================================================================
    /**
     * Processes one audio buffer, summing all active voices into the output.
     *
     * Each active voice generates a sine wave at its pitch, shaped by its
     * envelope and amplitude. The buffer is cleared before summation.
     *
     * @param buffer  The output audio buffer to fill. Must be non-null.
     */
    void process(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    /** Sets the voice allocation strategy. */
    void setAllocationMode(AllocationMode mode);

    /** Returns the current voice allocation strategy. */
    [[nodiscard]] AllocationMode getAllocationMode() const;

    //==============================================================================
    /** Sets the attack time in seconds for a specific voice. Clamped to [0, 10]. */
    void setVoiceAttack(int voiceIndex, float seconds);

    /** Sets the decay time in seconds for a specific voice. Clamped to [0, 10]. */
    void setVoiceDecay(int voiceIndex, float seconds);

    /** Sets the sustain level [0, 1] for a specific voice. Clamped to [0, 1]. */
    void setVoiceSustain(int voiceIndex, float level);

    /** Sets the release time in seconds for a specific voice. Clamped to [0, 10]. */
    void setVoiceRelease(int voiceIndex, float seconds);

    //==============================================================================
    /** Sets the default attack time for newly allocated voices. Clamped to [0, 10]. */
    void setDefaultAttack(float seconds);

    /** Sets the default decay time for newly allocated voices. Clamped to [0, 10]. */
    void setDefaultDecay(float seconds);

    /** Sets the default sustain level for newly allocated voices. Clamped to [0, 1]. */
    void setDefaultSustain(float level);

    /** Sets the default release time for newly allocated voices. Clamped to [0, 10]. */
    void setDefaultRelease(float seconds);

    //==============================================================================
    /** Returns the number of voices that are not in the free state. */
    [[nodiscard]] int getNumActiveVoices() const;

    /**
     * Returns true if the voice at the given index is active
     * (i.e. not in the free state).
     */
    [[nodiscard]] bool isVoiceActive(int voiceIndex) const;

    /**
     * Returns a const reference to the voice at the given index.
     * Useful for testing and debug inspection.
     */
    [[nodiscard]] const Voice& getVoice(int voiceIndex) const;

    //==============================================================================
    /**
     * Sets per-voice aftertouch amount.
     *
     * @param voiceIndex  Index of the voice to modify.
     * @param amount      Aftertouch amount in [0, 1].
     */
    void setVoiceAftertouch(int voiceIndex, float amount);

    /**
     * Sets per-voice pitch bend multiplier.
     *
     * @param voiceIndex  Index of the voice to modify.
     * @param bend        Pitch bend amount in [-1, 1] (maps to 0.5x-2x).
     */
    void setVoicePitchBend(int voiceIndex, float bend);

    //==============================================================================
    // MPE (MIDI Polyphonic Expression)
    //==============================================================================

    /**
     * Triggers a note-on with an explicit MPE MIDI channel.
     *
     * @param midiChannel  MPE per-note MIDI channel [1, 15] or 0 for master.
     * @param note         MIDI note number (0-127).
     * @param velocity     Note-on velocity [0, 1].
     * @param sampleRate   Current sample rate for pitch calculation.
     */
    void noteOnWithChannel(int midiChannel, int note, float velocity, double sampleRate);

    /**
     * Sets slide amount (CC74) for a specific voice.
     *
     * @param voiceIndex  Index of the voice to modify.
     * @param amount      Slide amount in [0, 1].
     */
    void setSlide(int voiceIndex, float amount);

    /**
     * Sets per-voice pressure (channel pressure / aftertouch).
     *
     * @param voiceIndex  Index of the voice to modify.
     * @param amount      Pressure amount in [0, 1].
     */
    void setPressure(int voiceIndex, float amount);

    /**
     * Sets per-voice timbre (CC74 mapped to filter cutoff).
     *
     * @param voiceIndex  Index of the voice to modify.
     * @param amount      Timbre amount in [0, 1].
     */
    void setTimbre(int voiceIndex, float amount);

    /**
     * Enables or disables MPE mode.
     * When enabled, note-on/off events use per-note MIDI channels
     * instead of the global channel.
     */
    void enableMPE(bool enabled);

    /**
     * Returns true if MPE mode is currently enabled.
     */
    [[nodiscard]] bool isMPEEnabled() const;

    /**
     * Sets the MPE master channel. Per-note channels are
     * masterChannel + 1 to 15.
     *
     * @param channel  Master MIDI channel [0, 15].
     */
    void setMPEMasterChannel(int channel);

    /**
     * Returns the current MPE master channel.
     */
    [[nodiscard]] int getMPEMasterChannel() const;

    /**
     * Sets the velocity curve mapping amount.
     *
     * @param amount  Curve amount [0, 1] where 0 = linear, 1 = exponential.
     */
    void setVelocityCurve(float amount);

    /**
     * Returns the current velocity curve amount.
     */
    [[nodiscard]] float getVelocityCurve() const;

    //==============================================================================
    /** Sets the adaptive envelope tracking amount (0 = none, 1 = full key tracking). */
    void setAdaptiveEnvelopeAmount(float amount);

    /** Returns the adaptive envelope tracking amount. */
    [[nodiscard]] float getAdaptiveEnvelopeAmount() const;

private:
    //==============================================================================
    /** Allocates a voice slot using the current allocation mode. */
    int allocateVoice();

    /**
     * Steals a voice when no free or idle voices are available.
     * Prefers stealing voices in this order: sustain > release > decay > attack.
     * Within the same state, the oldest voice (lowest noteOnIndex) is stolen.
     */
    int stealVoice();

    /**
     * Initialises a voice slot for a new note. Resets envelope state,
     * sets pitch/amplitude, and transitions to attack.
     */
    void startVoice(int voiceIndex, int note, float velocity);

    /**
     * Advances the envelope of the given voice by one audio sample.
     * Handles state transitions: attack -> decay -> sustain, and release -> idle.
     *
     * @param voiceIndex  Index of the voice to update.
     * @param dt          Time delta for one sample (1.0 / sampleRate).
     */
    void updateEnvelope(int voiceIndex, float dt);

    /**
     * Generates a single audio sample for the given voice.
     * Output = sin(phase) * envelopeLevel * amplitude.
     */
    float generateSample(int voiceIndex);

    /**
     * Applies a state-variable filter to the given voice sample.
     * The cutoff is modulated by the voice's timbre value.
     *
     * @param voiceIndex  Index of the voice to filter.
     * @param sample      Input sample to filter.
     * @return            Filtered output sample.
     */
    float applyFilter(int voiceIndex, float sample);

    /**
     * Resets the state-variable filter states for a voice.
     */
    void resetFilter(int voiceIndex);

    /** Applies the velocity curve mapping to a raw velocity value. */
    float applyVelocityCurve(float velocity) const;

    /** Converts a MIDI note number to frequency in Hz. */
    static float noteToFrequency(int note);

    //==============================================================================
    Voice voices[maxVoices];

    double                    sampleRate     = 44100.0;
    std::atomic<AllocationMode> allocationMode{ AllocationMode::roundRobin };
    std::atomic<int> nextVoiceIndex{0};
    std::atomic<uint64_t> globalNoteCounter{0};

    float defaultAttack   = 0.01f;
    float defaultDecay    = 0.2f;
    float defaultSustain  = 0.7f;
    float defaultRelease  = 0.3f;

    float velocityCurveAmount = 0.0f;  /**< Velocity curve mapping [0, 1]. 0 = linear, 1 = exponential. */
    float adaptiveEnvelopeAmount = 0.0f; /**< Adaptive envelope tracking [0, 1]. */

    // --- MPE state ---
    bool mpeEnabled     = false; /**< Whether MPE mode is active. */
    int  mpeMasterChannel = 0;  /**< Master MPE channel (0-15). Per-note channels are master+1 to 15. */
};

} // namespace ana
