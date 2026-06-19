#pragma once
#include "PartialDataSIMD.h"
#include <juce_core/juce_core.h>

namespace ana {

//==============================================================================
/**
    Per-partial LFO/envelope modulation system.

    Each active partial receives independent LFO and ADSR envelope modulation.
    The LFO value and envelope level are combined to modulate the partial amplitude.

    Usage:
        PartialModulator mod;
        mod.prepare(44100.0);

        PartialModulator::Config config;
        config.lfoRate  = 2.0f;
        config.lfoDepth = 0.5f;
        config.attack   = 0.01f;
        config.decay    = 0.1f;
        config.sustain  = 0.7f;
        config.release  = 0.3f;

        mod.process(partialData, config);
*/
class PartialModulator
{
public:
    PartialModulator() = default;

    //==============================================================================
    /** Configuration struct for the modulator. */
    struct Config
    {
        float lfoRate  = 1.0f;  //!< LFO rate in Hz
        float lfoDepth = 0.0f;  //!< LFO depth (0-1)
        float attack   = 0.01f; //!< Attack time in seconds
        float decay    = 0.1f;  //!< Decay time in seconds
        float sustain  = 0.7f;  //!< Sustain level (0-1)
        float release  = 0.3f;  //!< Release time in seconds
        bool   perPartialPhase = true; //!< Each partial gets an independent LFO phase
    };

    //==============================================================================
    /** Initialises the modulator.
        Must be called before process(). Stores the sample rate.
    */
    void prepare(double sampleRate);

    /** Processes all active partials in the given PartialDataSIMD buffer.
        For each active partial:
        1. Advance LFO phase (per-partial independent phase if enabled)
        2. Compute LFO value = sin(2pi * lfoPhase) * lfoDepth
        3. Update envelope state machine (ADSR)
        4. Apply modulation: amplitude[i] *= (1.0f + lfoValue * envLevel)

        @param partials  The SIMD partial data to modulate (modified in-place)
        @param config    Modulation configuration
    */
    void process(PartialDataSIMD& partials, const Config& config, int numSamples);

    /** Resets all per-partial states (LFO phase, envelope level, envelope state). */
    void reset();

private:
    struct PartialState
    {
        float lfoPhase  = 0.0f;
        float envLevel  = 0.0f;
        int   envState  = 0; // 0=attack, 1=decay, 2=sustain, 3=release
    };

    std::array<PartialState, 512> states_{};
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PartialModulator)
};

} // namespace ana
