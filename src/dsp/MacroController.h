#pragma once

#include <vector>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana {

//==============================================================================
/**
    A single macro-to-parameter mapping definition.

    Each MacroMapping associates one macro control with one target
    parameter and defines how the raw 0-1 macro value is transformed
    into the output value sent to that parameter.
*/
struct MacroMapping
{
    /** Which parameter this mapping controls. */
    int targetParamIndex = 0;

    /** Output range minimum (after curve + invert). */
    float min = 0.0f;

    /** Output range maximum (after curve + invert). */
    float max = 1.0f;

    /** Transfer curve type applied to the macro value before scaling. */
    enum class Curve
    {
        Linear,          /**< output = input */
        Exponential,     /**< output = input^2 */
        Logarithmic,     /**< output = sqrt(input) */
        SCurve,          /**< Smooth S-curve (slow in/out). */
        ReverseSCurve,   /**< Reverse S-curve (fast in/out). */
        Step             /**< Binary on/off at 50 %. */
    };

    Curve curve = Curve::Linear;

    /** When true, the output range becomes symmetric around zero:
        curve output [0, 1] is mapped to [-1, 1] before scaling.
        Useful for bipolar controls like pan or pitch bend. */
    bool bipolar = false;

    /** When true, the curve output is flipped: 1 - val. */
    bool invert = false;
};

//==============================================================================
/**
    Macro Control System.

    One knob controls multiple parameters simultaneously with configurable
    transfer curves.  Modeled after Serum's Macro controls and Massive's
    Perform tab.

    Features:
    - Up to 16 macros, each with arbitrary number of mappings
    - 6 curve types: Linear, Exponential, Logarithmic, S-Curve,
      Reverse S-Curve, Step
    - Per-mapping bipolar mode and invert
    - Thread-safe target value reads for the audio thread
    - SIMD-accelerated bulk target value computation (via SIMDSupport.h)
    - Full XML serialisation for preset save/load

    Usage:
        MacroController mc;
        mc.setNumMacros(4);

        // Map macro 0 to control parameter 3 with exponential curve
        MacroMapping m;
        m.targetParamIndex = 3;
        m.min = 20.0f;
        m.max = 20000.0f;
        m.curve = MacroMapping::Curve::Exponential;
        mc.addMapping(0, m);

        // Audio thread reads mapped output
        float cutoff = mc.getTargetValue(0);

    @see MacroMapping
*/
class MacroController
{
public:
    /** Maximum number of macros. */
    static constexpr int maxMacros = 16;

    MacroController();
    ~MacroController() = default;

    //==============================================================================
    /** Sets the number of macros (1 to maxMacros).
        Preserves existing macro names and mappings for indices that already exist.
        New macros are created with default state.
    */
    void setNumMacros(int count);

    /** Sets a human-readable name for the given macro index. */
    void setMacroName(int index, const juce::String& name);

    //==============================================================================
    /** Adds a mapping to the specified macro.
        @param macroIndex  Index of the macro to add the mapping to
        @param mapping     Mapping definition (target, range, curve, etc.)
    */
    void addMapping(int macroIndex, const MacroMapping& mapping);

    /** Removes all mappings from the specified macro. */
    void clearMappings(int macroIndex);

    /** Removes all mappings from all macros. */
    void clearAllMappings();

    //==============================================================================
    /** Sets the raw macro control value (0.0 to 1.0).
        This triggers a recomputation of all target values.
        Safe to call from the GUI/message thread.
        @param macroIndex  Index of the macro to update
        @param value       Raw value in [0, 1]
    */
    void setMacroValue(int macroIndex, float value);

    /** Returns the current raw macro value (0.0 to 1.0). */
    float getMacroValue(int macroIndex) const;

    //==============================================================================
    /** Returns the total number of mapped targets across all macros. */
    int getNumTargets() const;

    /** Returns the computed output value for the given target index.
        This is the value after applying curve, invert, bipolar, and
        scaling.  Safe to call from the audio thread.
    */
    float getTargetValue(int targetIndex) const;

    //==============================================================================
    /** Resets all macro values back to 0.0 and recomputes targets. */
    void reset();

    //==============================================================================
    /** Creates an XML representation of the entire macro controller state
        including all macros, their names, and all mappings.
        Caller owns the returned object.
    */
    juce::XmlElement* createXml() const;

    /** Restores the macro controller state from an XML element
        previously created by createXml().
    */
    void loadFromXml(const juce::XmlElement& xml);

private:
    //==============================================================================
    /** Internal per-macro state. */
    struct Macro
    {
        juce::String name;
        std::atomic<float> value { 0.0f };
        std::vector<MacroMapping> mappings;
    };

    std::vector<Macro> macros_;

    //==============================================================================
    // Double-buffered target cache for lock-free audio-thread reads.
    //
    //   GUI thread (writer):   calls updateTargetCache() → writes to the
    //                           inactive buffer, then atomically flips
    //                           readCacheIdx_.
    //
    //   Audio thread (reader): reads from the active buffer via
    //                           readCacheIdx_.
    //
    // This guarantees that the audio thread always sees a fully-consistent
    // snapshot of all target values without any mutex or spinlock.

    mutable std::vector<float> targetCacheA_;
    mutable std::vector<float> targetCacheB_;
    mutable std::atomic<int> readCacheIdx_ { 0 };
    int totalTargets_ = 0;

    //==============================================================================
    // SIMD scratch buffers (reused to avoid per-call heap allocations).
    mutable std::vector<float> simdCurve_;
    mutable std::vector<float> simdScale_;
    mutable std::vector<float> simdOffset_;

    //==============================================================================
    /** Applies the curve shape and invert to the raw [0, 1] input.
        Returns a normalised value in [0, 1] (no min/max scaling).
    */
    float applyCurve(float input, const MacroMapping& mapping) const noexcept;

    /** Recomputes the entire target cache from the current macro values.
        Called after any macro value change or structural edit.
    */
    void updateTargetCache();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroController)
};

} // namespace ana
