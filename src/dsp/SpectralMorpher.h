#pragma once

#include <vector>
#include "PartialDataSIMD.h"

namespace ana {

//==============================================================================
/**
    SpectralMorpher — per-partial spectral morphing engine.

    Provides three morphing strategies for blending between additive partial
    spectra, operating on PartialDataSIMD (SoA, SIMD-friendly) buffers:

        - morphLinear:     Classic linear interpolation (t=0 -> A, t=1 -> B).
                           Uses SIMDKernels::vectorLerp for all three float
                           arrays (frequency, amplitude, phase).

        - morphWeighted:   Amplitude-weighted morph where higher-amplitude
                           partials transition toward B faster than quieter
                           ones.  The effective t for each partial is scaled
                           by the mean amplitude of A and B at that index.

        - morphMulti:      Weighted sum of multiple source spectra.  Total
                           weight sum is clamped to 1.0.  Takes source data
                           and weights via std::vector.

    All methods sanitize NaN/Inf values and recompute activeMask on output.
    No heap allocation inside any morph method.
*/
struct SpectralMorpher
{
    /** A -> B linear morph (t=0 -> A, t=1 -> B).
        Uses SIMDKernels::vectorLerp for freq, amp, and phase.
        @param output  Pre-allocated destination (must not alias a or b).
        @param a       Source spectrum A.
        @param b       Source spectrum B.
        @param t       Blend factor [0, 1].
    */
    static void morphLinear(PartialDataSIMD& output,
                            const PartialDataSIMD& a,
                            const PartialDataSIMD& b,
                            float t);

    /** A -> B amplitude-weighted morph.
        Each partial's effective morph factor is
            t_eff = t * (a.amplitude[i] + b.amplitude[i]) * 0.5f
        so partials with higher total energy transition faster.
        @param output  Pre-allocated destination (must not alias a or b).
        @param a       Source spectrum A.
        @param b       Source spectrum B.
        @param t       Blend factor [0, 1].
    */
    static void morphWeighted(PartialDataSIMD& output,
                              const PartialDataSIMD& a,
                              const PartialDataSIMD& b,
                              float t);

    /** Multi-point morph: weighted sum of N source spectra.
        For each partial:
            output.xxx[i] = sum_j(weights[j] * sources[j].xxx[i])
        Total weight sum is clamped to 1.0.
        All sources should have matching metadata (first source copied to output).
        @param output      Pre-allocated destination.
        @param sources     Source spectra to blend.
        @param weights     Blend weights per source [0, 1].
    */
    static void morphMulti(PartialDataSIMD& output,
                           const std::vector<PartialDataSIMD>& sources,
                           const std::vector<float>& weights);
};

} // namespace ana
