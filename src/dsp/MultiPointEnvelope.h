#pragma once

#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana {

//==============================================================================
/** Curve type for transitions between breakpoints. */
enum class CurveType
{
    Linear,      /**< Straight line between points. */
    Exponential, /**< Fast initial change that decelerates toward target. */
    SCurve       /**< Smooth S-shaped Hermite transition. */
};

//==============================================================================
/** Loop modes for the envelope. */
enum class LoopMode
{
    None,     /**< Play through once, then stop. */
    Forward,  /**< Loop from loopStart to loopEnd repeatedly. */
    PingPong, /**< Bounce back and forth between loopStart and loopEnd. */
    Sustain   /**< Hold at loopEnd until release(), then continue. */
};

//==============================================================================
/** A single breakpoint defining the envelope shape. */
struct Breakpoint
{
    float time = 0.0f;               /**< Time position (seconds, or beats if sync enabled). */
    float value = 0.0f;              /**< Value at this point (0.0 to 1.0). */
    CurveType curve = CurveType::Linear; /**< Curve type for segment from this point to the next. */
};

//==============================================================================
/**
    Multi-Point Envelope Generator with up to 32 breakpoints.

    A flexible envelope generator supporting breakpoint-based shape definition
    with multiple curve types, loop modes, and tempo synchronization.

    Features:
    - Up to 32 breakpoints with time, value, and per-segment curve type
    - Curve types: linear, exponential, S-curve
    - Loop modes: none, forward, ping-pong, sustain
    - Tempo sync with beat-based timing
    - Trigger/release lifecycle for ADSR-style use

    Usage:
        MultiPointEnvelope env;
        env.prepare(44100.0);

        // 4-point ADSR-like envelope
        env.addBreakpoint(0.0f,   0.0f, CurveType::Exponential);
        env.addBreakpoint(0.01f,  1.0f, CurveType::Linear);     // Attack
        env.addBreakpoint(0.3f,   0.7f, CurveType::Exponential); // Decay to sustain
        env.addBreakpoint(1.0f,   0.7f, CurveType::Linear);     // Sustain level
        env.addBreakpoint(1.5f,   0.0f, CurveType::Exponential); // Release

        env.trigger();
        while (env.isActive())
        {
            float val = env.process(64); // Advance 64 samples
            // Use val for modulation...
        }
*/
class MultiPointEnvelope
{
public:
    /** Maximum number of breakpoints allowed. */
    static constexpr int maxBreakpoints = 32;

    MultiPointEnvelope();
    ~MultiPointEnvelope() = default;

    //==============================================================================
    /** Initialises the envelope with the given sample rate.
        Must be called before processing.
    */
    void prepare(double sampleRate);

    /** Resets envelope state to idle. Clears all runtime state but preserves breakpoints. */
    void reset();

    //==============================================================================
    /** Adds a breakpoint at the given time and value.
        Breakpoints are kept sorted by time.
        @param time   Time position (0-10s, or beats if sync enabled)
        @param value  Value at this point (0.0 to 1.0)
        @param curve  Curve type for the segment from this point to the next
        @return true if breakpoint was added, false if at maximum capacity
    */
    bool addBreakpoint(float time, float value, CurveType curve = CurveType::Linear);

    /** Removes the breakpoint at the given index.
        @return true if the index was valid and the point was removed
    */
    bool removeBreakpoint(int index);

    /** Moves/repositions the breakpoint at the given index.
        @return true if the index was valid and the point was moved
    */
    bool moveBreakpoint(int index, float time, float value);

    /** Removes all breakpoints. */
    void clearBreakpoints();

    /** Returns the number of breakpoints. */
    int getNumBreakpoints() const noexcept;

    /** Returns a const reference to the breakpoint at the given index.
        Asserts if index is out of range.
    */
    const Breakpoint& getBreakpoint(int index) const;

    //==============================================================================
    /** Sets the loop mode. */
    void setLoopMode(LoopMode mode) noexcept;

    /** Returns the current loop mode. */
    LoopMode getLoopMode() const noexcept;

    /** Sets the breakpoint index where looping begins. */
    void setLoopStart(int breakpointIndex) noexcept;

    /** Returns the loop start breakpoint index. */
    int getLoopStart() const noexcept;

    /** Sets the breakpoint index where looping ends (or sustain holds).
        In Sustain mode, the envelope holds at this breakpoint's value until
        release() is called, then continues through remaining breakpoints.
    */
    void setLoopEnd(int breakpointIndex) noexcept;

    /** Returns the loop end breakpoint index. */
    int getLoopEnd() const noexcept;

    //==============================================================================
    /** Sets the tempo in BPM for tempo-synced timing.
        @param bpm  Beats per minute (must be > 0)
    */
    void setTempo(double bpm);

    /** Returns the current tempo. */
    double getTempo() const noexcept;

    /** Sets the beat division for time interpretation.
        @param beats  Beat value: 1.0 = quarter note, 0.5 = eighth note,
                      0.25 = sixteenth note, etc.
    */
    void setBeatDivision(double beats);

    /** Returns the current beat division. */
    double getBeatDivision() const noexcept;

    /** Enables or disables tempo-synced timing.
        When enabled, breakpoint times are interpreted as beats and
        converted to seconds using tempo and beat division.
        When disabled, breakpoint times are in seconds directly.
    */
    void setSyncMode(bool sync) noexcept;

    /** Returns whether tempo sync is enabled. */
    bool getSyncMode() const noexcept;

    //==============================================================================
    // --- ADSR convenience API ---
    /** Sets the attack time (seconds). Rebuilds breakpoints to standard ADSR shape. */
    void setAttack(float time);
    /** Returns the current attack time in seconds. */
    float getAttack() const noexcept { return attack_; }

    /** Sets the decay time (seconds). Rebuilds breakpoints to standard ADSR shape. */
    void setDecay(float time);
    /** Returns the current decay time in seconds. */
    float getDecay() const noexcept { return decay_; }

    /** Sets the sustain level (0.0-1.0). Rebuilds breakpoints to standard ADSR shape. */
    void setSustain(float level);
    /** Returns the current sustain level. */
    float getSustain() const noexcept { return sustain_; }

    /** Sets the release time (seconds). Rebuilds breakpoints to standard ADSR shape. */
    void setRelease(float time);
    /** Returns the current release time in seconds. */
    float getRelease() const noexcept { return release_; }

    /** Rebuilds breakpoints from the current ADSR parameters
        using a standard 4-point envelope:
          (0,0) → (attack, 1.0) → (attack+decay, sustain) → (attack+decay+release, 0)
    */
    void rebuildADSR();

    //==============================================================================
    /** Starts the envelope from the beginning.
        If already active, resets to the start.
    */
    void trigger();

    /** Triggers the release phase.
        In Sustain mode, this allows the envelope to continue past the
        sustain point. In other modes, this is a no-op.
    */
    void release();

    /** Returns true if the envelope is currently running. */
    bool isActive() const noexcept;

    /** Returns true if release() has been called while the envelope was active. */
    bool isReleased() const noexcept;

    //==============================================================================
    /** Advances the envelope by the given number of samples and returns the current value.
        @param numSamples  Number of samples to advance (for sample-accurate timing)
        @return The current envelope value (0.0 to 1.0)
    */
    float process(int numSamples);

    /** Returns the current envelope value without advancing. */
    float getValue() const noexcept;

private:
    //==============================================================================
    /** Converts breakpoint time to seconds based on sync mode. */
    double timeToSeconds(float breakpointTime) const;

    /** Internal advance by a time delta in seconds. */
    void advanceEnvelope(double deltaSeconds);

    /** Computes interpolated value between v0 and v1 at position t (0-1). */
    static float interpolateValue(float v0, float v1, float t, CurveType curve);

    /** Handles what happens when we reach the end of the envelope. */
    void handleEnvelopeEnd();

    //==============================================================================
    double sampleRate = 44100.0;
    double timePosSeconds = 0.0;
    float currentValue = 0.0f;
    bool active = false;
    bool released = false;
    int direction = 1; /**< 1 = forward, -1 = backward (for ping-pong). */

    std::vector<Breakpoint> breakpoints;

    LoopMode loopMode = LoopMode::None;
    int loopStartIndex = 0;
    int loopEndIndex = -1;

    // ADSR convenience state (defaults match UI slider initial values)
    float attack_  = 0.01f;
    float decay_   = 0.5f;
    float sustain_ = 0.7f;
    float release_ = 1.0f;

    double tempo = 120.0;
    double beatDiv = 1.0;
    bool syncEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiPointEnvelope)
};

} // namespace ana
