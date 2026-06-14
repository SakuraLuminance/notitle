#pragma once

#include <juce_core/juce_core.h>

namespace ana {

//==============================================================================
/**
    Operating modes for the sub-harmonic generator.

    Below  — All sub-harmonics are pitched below the fundamental
             (1, 2, and 3 octaves down), producing a traditional
             sub-bass stack.

    Around — Sub-harmonic 1 sits one octave down; sub-harmonics 2
             and 3 are the 3rd and 5th harmonics of sub-1, creating
             a wider, more coloured spectral spread.
*/
enum class SubHarmonicMode
{
    Around,  /**< Sub-harmonics 2 & 3 are 3x and 5x sub-1 (wider spread). */
    Below    /**< All sub-harmonics are integer octaves below the
                  fundamental (traditional). */
};

//==============================================================================
/**
    Configuration bundle for SubHarmonicGenerator.

    Each level controls the amplitude of one sub-harmonic voice,
    normalised to [0, 1].  The three voices are:

        level1 — always 1 octave below fundamental
        level2 — 2 octaves below (Below) or 3rd harmonic of sub-1 (Around)
        level3 — 3 octaves below (Below) or 5th harmonic of sub-1 (Around)
*/
struct SubHarmonicConfig
{
    SubHarmonicMode mode    = SubHarmonicMode::Below;
    float           level1 = 0.0f;  /**< Sub-harmonic 1 level (always 1 octave down). */
    float           level2 = 0.0f;  /**< Sub-harmonic 2 level. */
    float           level3 = 0.0f;  /**< Sub-harmonic 3 level. */
};

//==============================================================================
/**
    Sub-harmonic generator inspired by Harmor's sub-bass section.

    Generates 1–3 sub-harmonic partials below (or around) a given
    fundamental frequency.  Two modes control the spectral placement
    of voices 2 and 3.

    Usage:
        SubHarmonicGenerator gen;
        gen.setConfig(myConfig);

        float freqs[3], amps[3];
        int n = gen.generate(110.0f, freqs, amps, 3);
        // n == 3, freqs = { 55, 27.5, 13.75 } (Below mode)

    Thread-safety: the class is designed for single-threaded audio
    processing.  Parameters should be set from the message thread
    before the processing callback.
*/
class SubHarmonicGenerator
{
public:
    SubHarmonicGenerator();
    ~SubHarmonicGenerator() = default;

    // --- Configuration -------------------------------------------------------

    /** Replace the entire configuration bundle. */
    void setConfig(const SubHarmonicConfig& config);

    /** Select sub-harmonic placement mode. */
    void setMode(SubHarmonicMode mode);

    /**
        Set the level of a single sub-harmonic voice.

        @param index  0-based voice index (0 = sub1, 1 = sub2, 2 = sub3).
        @param level  Amplitude in [0, 1]; values outside are clamped.
    */
    void setSubLevel(int index, float level);

    /** Set the sample rate (used for any time-domain calculations). */
    void setSampleRate(double sr);

    // --- Processing ----------------------------------------------------------

    /**
        Generate sub-harmonic partials for a given fundamental frequency.

        Computes up to three sub-harmonic frequencies and amplitudes
        and writes them into the caller-provided arrays.

        @param fundamentalFreq  The base frequency in Hz.
        @param outFrequencies   Output array for sub-harmonic frequencies.
        @param outAmplitudes    Output array for sub-harmonic amplitudes.
        @param maxOutput        Capacity of the output arrays (max subs generated).
        @return                 Number of sub-harmonics written (0–3).
    */
    int generate(float fundamentalFreq,
                 float* outFrequencies,
                 float* outAmplitudes,
                 int maxOutput);

    /**
        Append sub-harmonics to an existing partial list in-place.

        Convenience wrapper around generate() that writes the new
        partials at the end of `frequencies` / `amplitudes` and
        increments `*activeCount`.

        @param frequencies     Existing frequency array (will be appended).
        @param amplitudes      Existing amplitude array (will be appended).
        @param activeCount     In/out: number of active partials (updated after).
        @param fundamentalFreq The base frequency in Hz.
    */
    void processInPlace(float* frequencies,
                        float* amplitudes,
                        int* activeCount,
                        float fundamentalFreq);

    // --- Accessors -----------------------------------------------------------

    /** Return the current configuration (read-only). */
    const SubHarmonicConfig& getConfig() const noexcept { return config_; }

private:
    SubHarmonicConfig config_;
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SubHarmonicGenerator)
};

} // namespace ana
