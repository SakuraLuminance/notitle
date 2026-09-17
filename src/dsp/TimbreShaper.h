#pragma once

#include "PartialDataSIMD.h"

namespace ana
{

//==============================================================================
/** Non-destructive per-partial shaping parameters for one timbre (A or B).

    bright: 0..1, 0.5 = neutral. Values below/above tilt the spectrum down/up.
    hpfHz:  partials strictly below this frequency are removed.
    blur:   0..1 harmonic (partial-axis) blur amount.
*/
struct TimbreShapeParams
{
    float bright = 0.5f;
    float hpfHz  = 20.0f;
    float blur   = 0.0f;
};

//==============================================================================
/** Stateless helpers that shape a single-frame harmonic set (SoA) on the
    message thread.  Pair with DualTimbre for the A/B crossfade. */
class TimbreShaper
{
public:
    /** Spectral tilt: amp *= (f / 1kHz) ^ ((bright-0.5)*4). */
    static void applyBright(PartialDataSIMD& p, float bright01);

    /** Remove partials with frequency < cutoffHz. */
    static void applyHpf(PartialDataSIMD& p, float cutoffHz);

    /** Harmonic blur across the active partial list (via BlurEffect). */
    static void applyBlur(PartialDataSIMD& p, float amount01, double sampleRate);

    /** bright -> hpf -> blur, in that order. */
    static void shape(PartialDataSIMD& p, const TimbreShapeParams& params, double sampleRate);

    /** Amplitude-only Fade crossfade between two sets sharing the same partial
        indexing.  Frequencies and phases are inherited from `a`. */
    static PartialDataSIMD blend(const PartialDataSIMD& a,
                                 const PartialDataSIMD& b,
                                 float mix01);
};

} // namespace ana
