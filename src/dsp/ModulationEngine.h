#pragma once
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana {

//==============================================================================
/** Modulation source selector for per-parameter ModulationConnection.
    Each parameter slot selects which source modulates it (or OFF for none).
    LFO sources 1-4 map to processor lfoPool_[0..3].
    ENV sources 1-3 map to processor envPool_[0..2].
*/
enum ModSource : int
{
    OFF       = 0,  /**< No modulation. */
    LFO1      = 1,  /**< LFO pool index 0. */
    LFO2      = 2,  /**< LFO pool index 1. */
    LFO3      = 3,  /**< LFO pool index 2. */
    LFO4      = 4,  /**< LFO pool index 3. */
    ENV1      = 5,  /**< Envelope pool index 0. */
    ENV2      = 6,  /**< Envelope pool index 1. */
    ENV3      = 7,  /**< Envelope pool index 2. */
    Sequencer = 8   /**< Step sequencer CV output. */
};

//==============================================================================
/** A single modulation connection: source + depth + response curve.
    Each parameter's modulation slot holds one of these.
*/
struct ModulationConnection
{
    ModSource source = OFF;  /**< Modulation source (LFO1-4, ENV1-3, or OFF). */
    float depth   = 0.0f;   /**< Modulation depth in [-1.0, 1.0]. */
    float curve   = 1.0f;   /**< Power response curve (1.0 = linear). */
};

//==============================================================================
/** A modulation slot ties a base parameter value to an active ModulationConnection.
    The engine reads baseValuePtr, applies modulation from the selected source,
    and writes the result to modulatedValue for DSP to consume.

    All access is block-rate (not per-sample). No virtual calls, no string
    lookups, no ValueTree access in the audio thread.
*/
struct ModulationSlot
{
    ModulationConnection mod;               /**< Active modulation connection. */
    std::atomic<float>* baseValuePtr = nullptr; /**< Points to the raw parameter atomic. */
    float modulatedValue = 0.0f;            /**< Output: base + modulation, read by DSP. */
    juce::String paramId;                   /**< Human-readable parameter ID (message-thread only). */
};

} // namespace ana
