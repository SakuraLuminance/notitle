#pragma once

#include <cmath>
#include <algorithm>
#include <cstring>

#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/**
    Blend modes for combining two timbre partial sets.

    Each mode defines a different way to combine the amplitude arrays
    of two TimbrePart objects on a per-partial-index basis.
*/
enum class TimbreBlendMode
{
    Fade,      // Standard crossfade: t1 * (1-mix) + t2 * mix
    Subtract,  // t1 - t2  (clamped to zero)
    Multiply,  // t1 * t2
    Maximum,   // max(t1, t2)
    Minimum,   // min(t1, t2)
    Pluck      // Blend with frequency-dependent decay
};

//==============================================================================
/**
    A SIMD-friendly collection of partial data (SoA layout).

    Holds frequency, amplitude, and phase arrays for up to 512 partials,
    aligned to 32 bytes for AVX2 compatibility.
*/
struct TimbrePart
{
    static constexpr int kMaxPartials = 512;

    alignas(32) float frequency[kMaxPartials] = {};
    alignas(32) float amplitude[kMaxPartials] = {};
    alignas(32) float phase[kMaxPartials]     = {};
    int activeCount = 0;

    /** Zero all arrays and reset active count. */
    void clear()
    {
        std::memset(frequency, 0, sizeof(frequency));
        std::memset(amplitude, 0, sizeof(amplitude));
        std::memset(phase, 0, sizeof(phase));
        activeCount = 0;
    }

    /** Returns true if the partial at `index` has non-trivial amplitude. */
    bool isActive(int index) const
    {
        return amplitude[index] > 1e-6f;
    }
};

//==============================================================================
/**
    Dual-timbre blending processor.

    Maintains two independent TimbrePart objects and blends their amplitude
    arrays using one of six blend modes. Frequencies and phases are inherited
    from timbre 1 (the primary timbre).
*/
class DualTimbre
{
public:
    DualTimbre();
    ~DualTimbre() = default;

    // --- Setters -------------------------------------------------------------

    void setTimbre1(const TimbrePart& timbre);
    void setTimbre2(const TimbrePart& timbre);
    void setMix(float mix);               // 0.0 to 1.0
    void setMode(TimbreBlendMode mode);
    void setPluckDecay(float time);       // Pluck mode decay time in seconds

    // --- Processing ----------------------------------------------------------

    /** Blend timbre1 and timbre2 into `output` using the current mix/mode. */
    void process(TimbrePart& output);

    /**
        Directly blend two amplitude arrays.

        @param amp1       Amplitude array from timbre 1
        @param amp2       Amplitude array from timbre 2
        @param output     Destination amplitude array
        @param count      Number of floats to blend
        @param mix        Blend factor (0 = all timbre1, 1 = all timbre2)
        @param mode       Blend mode selector
        @param pluckDecay Decay time constant for Pluck mode (seconds)
        @param sampleRate Sample rate for Pluck mode decay calculation
    */
    static void blend(
        const float* amp1, const float* amp2,
        float* output, int count,
        float mix, TimbreBlendMode mode,
        float pluckDecay = 0.0f, float sampleRate = 44100.0f);

private:
    TimbrePart timbre1;
    TimbrePart timbre2;
    TimbrePart output_;
    float mix_ = 0.5f;
    TimbreBlendMode mode_ = TimbreBlendMode::Fade;
    float pluckDecay_ = 0.1f;
    double sampleRate_ = 44100.0;

    // --- Per-mode SIMD-accelerated kernels -----------------------------------

    static void blendFade(const float* a, const float* b,
                          float* dest, int count, float mix);
    static void blendSubtract(const float* a, const float* b,
                              float* dest, int count);
    static void blendMultiply(const float* a, const float* b,
                              float* dest, int count);
    static void blendMaximum(const float* a, const float* b,
                             float* dest, int count);
    static void blendMinimum(const float* a, const float* b,
                             float* dest, int count);
    static void blendPluck(const float* a, const float* b,
                           float* dest, int count,
                           float mix, float pluckDecay, float sampleRate);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DualTimbre)
};

} // namespace ana
