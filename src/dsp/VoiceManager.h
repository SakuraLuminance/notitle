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
 * Voice mode for the synthesizer.
 *
 * Poly:   Multiple voices can play simultaneously (current behavior).
 * Mono:   Only one voice plays at a time. New note-on replaces the current note.
 *         Release phase only begins when all keys have been released.
 * Legato: Like Mono but the envelope does NOT retrigger on overlapping notes.
 *         Pitch slides smoothly from the old note to the new note via portamento.
 */
enum class VoiceMode : uint8_t
{
    Poly   = 0,  /**< Full polyphony — multiple simultaneous voices. */
    Mono   = 1,  /**< Monophonic — single voice, retrigger envelope. */
    Legato = 2   /**< Monophonic — single voice, no envelope retrigger. */
};

//==============================================================================
/**
 * Portamento curve type for pitch glide interpolation.
 */
enum class PortamentoCurve : uint8_t
{
    Linear      = 0,  /**< Linear pitch interpolation. */
    Exponential = 1,  /**< Exponential (constant-rate) pitch interpolation. */
    Logarithmic = 2   /**< Logarithmic pitch interpolation (fast initial, slow finish). */
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
 * Represents a single voice in the MPE synthesizer's polyphonic voice pool.
 *
 * Inherits from juce::MPESynthesiserVoice for MPE-compatible per-note
 * modulation (pitch bend, pressure, timbre). Each voice carries its own
 * oscillator state, ADSR envelope, state-variable filter, and complex
 * phasor oscillator.
 */
class AnaVoice : public juce::MPESynthesiserVoice
{
public:
    //==============================================================================
    // --- Synthesis state ---
    std::atomic<float> pitchHz       { 0.0f }; /**< Oscillator frequency in Hz. */
    std::atomic<float> amplitude     { 0.0f }; /**< Current output amplitude (includes velocity scaling). */
    float              phase         = 0.0f;   /**< Oscillator phase in radians [0, 2pi). */
    float              envelopeLevel = 0.0f;   /**< Current ADSR envelope value in [0, 1]. */

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

    // --- Performance caches ---
    float cachedMod = 1.0f; /**< Precomputed amplitude * (1+aftertouch*0.5) * (1+pressure*0.5). */
    float smoothFc  = 0.0f; /**< Smoothed filter cutoff for 1-pole zipper prevention. */

    // --- Complex phasor oscillator (lock-free rotation) ---
    float phasorRe = 1.0f;    /**< cos(phase) of complex oscillator. */
    float phasorIm = 0.0f;    /**< sin(phase) of complex oscillator. */
    float cosDelta = 1.0f;    /**< cos(2π * freq / sampleRate) per-sample rotation. */
    float sinDelta = 0.0f;    /**< sin(2π * freq / sampleRate) per-sample rotation. */

    // --- Portamento / glide state ---
    float portamentoStartPitch  = 0.0f;   /**< Pitch at the start of a glide (Hz). */
    float portamentoTargetPitch = 0.0f;   /**< Target pitch for the glide (Hz). */
    int   portamentoElapsed     = 0;      /**< Samples elapsed since glide start. */
    int   portamentoTotalSamples= 0;      /**< Total samples the glide should last. */
    PortamentoCurve portamentoCurve = PortamentoCurve::Linear;
    bool  portamentoActive      = false;  /**< True while a glide is in progress. */

    // --- Voice state machine ---
    std::atomic<VoiceState> state{VoiceState::free};
    int        note     = -1;       /**< MIDI note number (0-127), -1 if unused. */
    float      velocity = 0.0f;     /**< Note-on velocity in [0, 1]. */

    //==============================================================================
    // --- Waveform type ---
    enum class WaveformType : uint8_t
    {
        Sine     = 0, /**< Pure sine wave (default). */
        Saw      = 1, /**< Rising sawtooth (bipolar, DC-corrected). */
        Square   = 2, /**< Square wave (bipolar). */
        Triangle = 3, /**< Triangle wave (bipolar). */
        Noise    = 4  /**< White noise. */
    };

    WaveformType waveformType_{WaveformType::Sine}; /**< Current waveform shape. */

    //==============================================================================
    // MPESynthesiserVoice overrides
    //==============================================================================

    /** Returns true if this voice is not in the free state. */
    bool isActive() const override;

    /** Called by MPESynthesiser when a new MPE note starts on this voice. */
    void noteStarted() override;

    /** Called by MPESynthesiser when the note on this voice stops. */
    void noteStopped(bool allowTailOff) override;

    /** Called by MPESynthesiser when the note's pressure changes. */
    void notePressureChanged() override;

    /** Called by MPESynthesiser when the note's pitch bend changes. */
    void notePitchbendChanged() override;

    /** Called by MPESynthesiser when the note's timbre changes. */
    void noteTimbreChanged() override;

    /** Called by MPESynthesiser when the note's key state changes (sustain pedal). */
    void noteKeyStateChanged() override;

    /** Renders the next block of audio for this voice. */
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample,
                         int numSamples) override;
};

//==============================================================================
/**
 * MPE-compatible polyphonic voice manager.
 *
 * Inherits from juce::MPESynthesiser for full MPE support including
 * per-note pitch bend, pressure, and timbre (CC74). Falls back to
 * standard MIDI via legacy mode when MPE is disabled.
 *
 * Usage:
 * @code
 *   ana::VoiceManager vm;
 *   vm.prepare(44100.0);
 *   vm.enableMPE(false);  // standard MIDI mode
 *
 *   juce::AudioBuffer<float> buffer(2, 512);
 *   juce::MidiBuffer midi;
 *   vm.renderNextBlock(buffer, midi, 0, 512);
 * @endcode
 */
class VoiceManager : public juce::MPESynthesiser
{
public:
    static constexpr int maxVoices = 32;

    //==============================================================================
    /** Creates a VoiceManager with maxVoices voices. */
    VoiceManager();

    /** Destructor. */
    ~VoiceManager() override = default;

    JUCE_DECLARE_NON_COPYABLE(VoiceManager)

    //==============================================================================
    /**
     * Prepares the voice manager for processing at a given sample rate.
     * Must be called before any renderNextBlock() calls.
     */
    void prepare(double sampleRate);

    //==============================================================================
    /**
     * Processes one audio buffer (legacy API).
     *
     * Clears the buffer, renders all active voices into it using the
     * MPESynthesiser audio pipeline. No MIDI is processed — use
     * renderNextBlock() if you need MIDI input.
     *
     * @param buffer  The output audio buffer to fill.
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
     * Returns a pointer to the AnaVoice at the given index.
     * Shadows MPESynthesiser::getVoice() to return the concrete type.
     */
    AnaVoice* getVoice(int index) const
    {
        return static_cast<AnaVoice*>(MPESynthesiser::getVoice(index));
    }

    //==============================================================================
    /** Sets per-voice aftertouch amount. */
    void setVoiceAftertouch(int voiceIndex, float amount);

    /** Sets per-voice pitch bend multiplier. */
    void setVoicePitchBend(int voiceIndex, float bend);

    //==============================================================================
    // MPE (MIDI Polyphonic Expression)
    //==============================================================================

    /**
     * Enables or disables MPE mode.
     *
     * When enabled, a lower MPE zone (channels 1-15) is configured with
     * channel 1 as the master channel. When disabled, the synthesiser
     * falls back to standard MIDI (legacy mode) using all channels.
     */
    void enableMPE(bool enabled);

    /** Returns true if MPE mode is currently enabled (non-legacy). */
    [[nodiscard]] bool isMPEEnabled() const;

    /**
     * Sets the MPE master channel number (0-15, 0-indexed).
     * The per-note channels occupy masterChannel+1 through 15.
     * The zone layout is reconfigured on the next enableMPE(true) call.
     */
    void setMPEMasterChannel(int channel);

    /** Returns the current MPE master channel. */
    [[nodiscard]] int getMPEMasterChannel() const;

    /** Sets the velocity curve mapping amount. */
    void setVelocityCurve(float amount);

    /** Returns the current velocity curve amount. */
    [[nodiscard]] float getVelocityCurve() const;

    //==============================================================================
    /**
     * Sets the voice mode (Poly / Mono / Legato).
     * In Mono and Legato modes, only voice 0 is used.
     */
    void setVoiceMode(VoiceMode mode);

    /** Returns the current voice mode. */
    [[nodiscard]] VoiceMode getVoiceMode() const;

    //==============================================================================
    /** Sets the portamento glide time in seconds [0, 2]. */
    void setPortamentoTime(float seconds);

    /** Returns the portamento glide time in seconds. */
    [[nodiscard]] float getPortamentoTime() const;

    /** Sets the portamento curve type. */
    void setPortamentoCurve(PortamentoCurve curve);

    /** Returns the portamento curve type. */
    [[nodiscard]] PortamentoCurve getPortamentoCurve() const;

    //==============================================================================
    /** Sets the adaptive envelope tracking amount (0 = none, 1 = full key tracking). */
    void setAdaptiveEnvelopeAmount(float amount);

    /** Returns the adaptive envelope tracking amount. */
    [[nodiscard]] float getAdaptiveEnvelopeAmount() const;

    //==============================================================================
    /** Sets the waveform type for a specific voice. */
    void setWaveformType(int voiceIndex, AnaVoice::WaveformType type);

    /** Returns the waveform type for a specific voice. */
    [[nodiscard]] AnaVoice::WaveformType getWaveformType(int voiceIndex) const;

    /** Sets the waveform type for all voices (global change). */
    void setAllWaveforms(AnaVoice::WaveformType type);

    //==============================================================================
    /**
     * Starts a note on the first available voice (legacy compat API).
     * Finds a free voice, or steals one if all voices are active.
     *
     * @param note     MIDI note number [0, 127]
     * @param velocity Note-on velocity [0, 1]
     */
    void noteOn(int note, float velocity);

    /**
     * Stops all voices currently playing the given MIDI note.
     * Transitions matching voices from attack/decay/sustain to release.
     *
     * @param note  MIDI note number to stop
     */
    void noteOff(int note);

    /**
     * Stops all active voices immediately.
     * Every active voice is transitioned to its release phase.
     */
    void allVoicesOff();

protected:
    //==============================================================================
    /**
     * Overrides noteAdded to set up our custom voice state when MPESynthesiser
     * assigns a new note to a voice.
     */
    void noteAdded(juce::MPENote newNote) override;

    /**
     * Overrides noteReleased to initiate voice release when a note ends.
     */
    void noteReleased(juce::MPENote finishedNote) override;

    /**
     * Overrides renderNextSubBlock to clear the buffer before summing voices.
     */
    void renderNextSubBlock(juce::AudioBuffer<float>& outputAudio,
                            int startSample,
                            int numSamples) override;

    //==============================================================================
    /**
     * Overrides findFreeVoice to use our custom allocation algorithm
     * (free -> idle -> steal with the configured AllocationMode).
     */
    juce::MPESynthesiserVoice* findFreeVoice(juce::MPENote noteToFindVoiceFor,
                                        bool stealIfNoneAvailable) const override;

    /**
     * Overrides findVoiceToSteal to use our priority-based stealing:
     * sustain > release > decay > attack, then oldest.
     */
    juce::MPESynthesiserVoice* findVoiceToSteal(juce::MPENote noteToStealVoiceFor) const override;

private:
    //==============================================================================
    /** Allocates a voice slot using the current allocation mode. Returns index or -1. */
    int allocateVoice() const;

    /**
     * Steals a voice when no free or idle voices are available.
     * Prefers stealing voices in this order: sustain > release > decay > attack.
     * Within the same state, the oldest voice (lowest noteOnIndex) is stolen.
     */
    int stealVoice() const;

    /**
     * Initialises a voice slot for a new note. Resets envelope state,
     * sets pitch/amplitude, and transitions to attack.
     */
    void startVoice(int voiceIndex, int note, float velocity);

    /**
     * Initialises a voice for the given MPE note, applying velocity curve,
     * adaptive envelope, and portamento setup.  In Legato mode the envelope
     * state is preserved (no retrigger).  In Mono mode the voice is forcibly
     * restarted.
     */
    void anaVoiceInit(juce::MPESynthesiserVoice* voice, const juce::MPENote& newNote, VoiceMode mode);

    /** Applies the velocity curve mapping to a raw velocity value. */
    float applyVelocityCurve(float velocity) const;

    /** Converts a MIDI note number to frequency in Hz. */
    static float noteToFrequency(int note);

    /** Generates a single audio sample for the given voice. */
    float generateSample(int voiceIndex) const;

    /**
     * Applies a state-variable filter to the given voice sample.
     */
    float applyFilter(int voiceIndex, float sample, float nyquist, float minCutoff, float maxCutoff) const;

    /** Resets the state-variable filter states for a voice. */
    void resetFilter(int voiceIndex) const;

    //==============================================================================
    double                    sampleRate_       = 44100.0;
    std::atomic<AllocationMode> allocationMode_{ AllocationMode::roundRobin };
    mutable std::atomic<int>  nextVoiceIndex_{0};
    mutable std::atomic<uint64_t> globalNoteCounter_{0};

    float defaultAttack_   = 0.01f;
    float defaultDecay_    = 0.2f;
    float defaultSustain_  = 0.7f;
    float defaultRelease_  = 0.3f;

    float velocityCurveAmount_      = 0.0f;
    float adaptiveEnvelopeAmount_   = 0.0f;

    // --- Voice mode / portamento ---
    std::atomic<VoiceMode>       voiceMode_{ VoiceMode::Poly };
    float                        portamentoTime_    = 0.0f;
    PortamentoCurve              portamentoCurve_{ PortamentoCurve::Linear };
    std::atomic<int>             heldKeys_{ 0 };

    // --- MPE state ---
    std::atomic<bool> mpeEnabled_       { false };
    std::atomic<int>  mpeMasterChannel_ { 0 };
};

} // namespace ana
