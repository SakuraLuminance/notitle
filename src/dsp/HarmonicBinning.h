#pragma once

#include "PartialData.h"
#include "PartialDataSIMD.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace ana
{

//==============================================================================
/**
    Harmonic-bin axis for the time-varying harmonic image (P6 leftover).

    The STFT peak tracker emits each frame's partials sorted by frequency, so
    "partial index i" in frame N and frame N+1 only means "the i-th loudest peak
    by frequency" - not the same harmonic.  When a peak appears or disappears,
    every later index shifts by one and AdditiveBank's floor/ceil frame
    interpolation cross-fades unrelated harmonics (audible as sliding partials
    instead of timbre morphing).

    Binning every frame against ONE shared fundamental makes index i mean
    "harmonic i+1" in every frame:

        bin = lround(frequency / f0) - 1        (bin 0 = fundamental)

    The stored frequency stays the measured peak frequency, so inharmonicity is
    preserved; only the index correspondence becomes harmonic.  With no usable
    f0 (silence, noise, no peaks) the mapping degrades to today's
    copy-by-index behaviour.

    This header is header-only and depends on nothing but PartialData /
    PartialDataSIMD, so the test target can include it.
*/
struct HarmonicBinning
{
    /** A frame whose loudest peak is below this defines no fundamental. */
    static constexpr float kMinFundamentalAmplitude = 0.02f;

    /** A candidate fundamental must reach this share of the frame's loudest peak. */
    static constexpr float kDefaultMinShare = 0.25f;

    //==========================================================================
    /** Fundamental of one frame: the LOWEST peak that carries at least
        @a minShare of the frame's loudest peak.  Returns 0 when the frame has
        no usable peak. */
    static float estimateFundamental (const std::vector<Partial>& partials,
                                      float minShare = kDefaultMinShare)
    {
        float peak = 0.0f;
        for (const auto& p : partials)
            peak = std::max (peak, p.amplitude);

        if (peak < kMinFundamentalAmplitude)
            return 0.0f;

        const float threshold = peak * minShare;
        for (const auto& p : partials)
            if (p.frequency > 0.0f && p.amplitude >= threshold)
                return p.frequency;

        return 0.0f;
    }

    //==========================================================================
    /** One fundamental shared by every frame: the median of the per-frame
        estimates (the LOWER median for an even count - an f0 that drifts
        upward collapses neighbouring harmonics into one bin, while a slightly
        low f0 only spreads them over more rows).  A frame that misses its
        fundamental then cannot move the whole axis.  Returns 0 when no frame
        yields an estimate. */
    static float estimateSharedFundamental (const std::vector<PartialFrame>& frames,
                                            float minShare = kDefaultMinShare)
    {
        std::vector<float> estimates;
        estimates.reserve (frames.size());

        for (const auto& frame : frames)
        {
            const float f0 = estimateFundamental (frame.partials, minShare);
            if (f0 > 0.0f)
                estimates.push_back (f0);
        }

        if (estimates.empty())
            return 0.0f;

        const std::size_t mid = (estimates.size() - 1) / 2;
        std::nth_element (estimates.begin(), estimates.begin() + static_cast<std::ptrdiff_t> (mid),
                          estimates.end());
        return estimates[mid];
    }

    //==========================================================================
    /** Frequency implied by a row of the harmonic axis (row 0 = fundamental). */
    static float binFrequency (int binIndex, float f0) noexcept
    {
        return (binIndex >= 0 && f0 > 0.0f)
                   ? f0 * static_cast<float> (binIndex + 1)
                   : 0.0f;
    }

    /** Harmonic bin of @a frequency, or -1 when it is below half the
        fundamental.  Partials above the axis fold into the top bin so no
        energy is dropped when f0 is very low. */
    static int frequencyToBin (float frequency, float f0, int binCount) noexcept
    {
        if (f0 <= 0.0f || binCount <= 0 || frequency <= 0.0f)
            return -1;

        const float harmonic = frequency / f0;
        if (harmonic < 0.5f)
            return -1;

        const int bin = static_cast<int> (std::lround (static_cast<double> (harmonic))) - 1;
        if (bin < 0)
            return -1;

        return std::min (bin, binCount - 1);
    }

    //==========================================================================
    /** Writes @a partials into @a out on the harmonic axis.  On a bin
        collision the louder partial wins (with its own frequency and phase).
        With f0 <= 0 the partials are copied by index - the pre-binning
        behaviour - so non-harmonic material is unaffected. */
    static void binInto (const std::vector<Partial>& partials,
                         float f0,
                         int binCount,
                         PartialDataSIMD& out)
    {
        binCount = std::max (1, std::min (binCount, PartialDataSIMD::kMaxPartials));

        std::memset (out.frequency, 0, sizeof (out.frequency));
        std::memset (out.amplitude, 0, sizeof (out.amplitude));
        std::memset (out.phase,     0, sizeof (out.phase));

        if (f0 > 0.0f)
        {
            for (const auto& p : partials)
            {
                if (p.amplitude <= 0.0f)
                    continue;

                const int bin = frequencyToBin (p.frequency, f0, binCount);
                if (bin < 0)
                    continue;

                if (p.amplitude > out.amplitude[bin])
                {
                    out.amplitude[bin] = p.amplitude;
                    out.frequency[bin] = p.frequency;
                    out.phase[bin]     = p.phase;
                }
            }
        }
        else
        {
            const int count = std::min (static_cast<int> (partials.size()), binCount);
            for (int i = 0; i < count; ++i)
            {
                const auto& p = partials[static_cast<std::size_t> (i)];
                out.frequency[i] = p.frequency;
                out.amplitude[i] = p.amplitude;
                out.phase[i]     = p.phase;
            }
        }

        out.maxPartials = binCount;
        out.updateActiveMask();
    }
};

} // namespace ana
