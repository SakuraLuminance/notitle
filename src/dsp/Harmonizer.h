#pragma once

#include "PartialDataSIMD.h"
#include <cmath>
#include <vector>

namespace ana {

//==============================================================================
/**
    Harmonic Harmonizer — clones and transposes harmonic partials.

    Inspired by Harmor's harmoniser, this effect takes existing partials in
    each frame and generates spectral clones at transposed frequencies.
    Original partials are preserved; clones are added alongside up to the
    maximum partials limit.

    Two shift modes are supported:

        - Octave mode:   frequencies are multiplied by powers of 2
                         (musical semitone spacing via pow(2, shift/12)).
        - Harmonic mode: frequencies are multiplied by integer ratios
                         (1 + shift, 1 + 2*shift, …).

    A @c gap parameter adds a frequency offset per clone, producing
    slightly inharmonic / bell-like spectra when non-zero.
*/
class Harmonizer
{
public:
    Harmonizer();
    ~Harmonizer() = default;

    // --- Parameters ----------------------------------------------------------

    /** Mix between original and harmonised signal.
        0.0 = original only (no clones), 1.0 = full harmonisation. */
    void setAmount(float amount);

    /** How far upward to clone — determines the number of clones
        generated from each active partial.  0.0 to 1.0. */
    void setWidth(float width);

    /** Amplitude scaling of clones.
        Each clone's amplitude = original * pow(strength, cloneIndex).
        0.0 to 1.0. */
    void setStrength(float strength);

    /** Frequency offset per clone step.

        In octave mode this is a semitone offset applied to each clone
        (actual multiplier = pow(2, shift * cloneIndex / 12)).
        In harmonic mode this is an additive harmonic step
        (multiplier = 1 + shift * cloneIndex). */
    void setShift(float shift);

    /** Extra frequency gap (in Hz) between the first and higher clones.
        Added as an increasing offset per clone index to create
        inharmonic spectra when desired.  0.0 = purely harmonic. */
    void setGap(float gap);

    /** Select the shift calculation mode.
        @param useOctaves  true = octave/semitone mode,
                           false = harmonic-integer mode. */
    void setShiftMode(bool useOctaves);

    /** Set the sample rate (used for Nyquist clamping of clones). */
    void setSampleRate(double sr);

    // --- Processing ----------------------------------------------------------

    /** Process partial data in-place.

        Clones existing partials at transposed frequencies and appends
        them to the frame (originals are preserved). The total number
        of partials is capped at data.maxPartials.
    */
    void process(PartialDataSIMD& data);

    /** Reset internal state (currently a no-op; sub-class hook). */
    void reset();

private:
    /** Core harmonisation loop applied to a single frame. */
    void applyHarmonization(PartialDataSIMD& data);

    /** Compute the number of clones to generate based on width. */
    int calcCloneCount(float width, int originalCount, int maxPartials) const noexcept;

    /** Recompute shift ratio and strength power tables. */
    void recomputeTables();

    float amount_    = 0.5f;
    float width_     = 0.5f;
    float strength_  = 0.5f;
    float shift_     = 0.0f;
    float gap_       = 0.0f;
    bool  useOctaves_ = true;
    double sampleRate_ = 44100.0;

    // Precomputed tables for O(1) lookups in hot loop
    float shiftRatios_[13]  = {0.0f};  // shift ratios for clones 1..12
    float strengthPow_[13]  = {0.0f};  // strength powers for clones 1..12

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Harmonizer)
};

} // namespace ana
