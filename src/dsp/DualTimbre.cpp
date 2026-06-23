#include "DualTimbre.h"

namespace ana {

//==============================================================================
// Construction
//==============================================================================

DualTimbre::DualTimbre()
    : mix_(0.5f),
      mode_(TimbreBlendMode::Fade),
      pluckDecay_(0.1f),
      sampleRate_(44100.0)
{
    timbre1.clear();
    timbre2.clear();
    output_.clear();
}

//==============================================================================
// Setters
//==============================================================================

void DualTimbre::setTimbre1(const TimbrePart& timbre)
{
    timbre1 = timbre;
}

void DualTimbre::setTimbre2(const TimbrePart& timbre)
{
    timbre2 = timbre;
}

void DualTimbre::setMix(float mix)
{
    mix_ = std::max(0.0f, std::min(1.0f, mix));
}

void DualTimbre::setMode(TimbreBlendMode mode)
{
    mode_ = mode;
}

void DualTimbre::setPluckDecay(float time)
{
    pluckDecay_ = std::max(0.001f, time);
}

//==============================================================================
// Processing
//==============================================================================

void DualTimbre::process(TimbrePart& output)
{
    constexpr int count = TimbrePart::kMaxPartials;

    // Frequencies and phases are determined by timbre1 (primary timbre).
    std::memcpy(output.frequency, timbre1.frequency,
                static_cast<size_t>(count) * sizeof(float));
    std::memcpy(output.phase, timbre1.phase,
                static_cast<size_t>(count) * sizeof(float));

    // Blend amplitudes using the current mode and mix.
    blend(timbre1.amplitude, timbre2.amplitude,
          output.amplitude, count,
          mix_, mode_, pluckDecay_, static_cast<float>(sampleRate_));

    // Active count is the maximum of both timbres, since either may have
    // content in different partial slots (frequency array from timbre1
    // carries the spectral structure).
    output.activeCount = std::max(timbre1.activeCount, timbre2.activeCount);
}

//==============================================================================
// Static dispatch
//==============================================================================

void DualTimbre::blend(
    const float* amp1, const float* amp2,
    float* output, int count,
    float mix, TimbreBlendMode mode,
    float pluckDecay, float sampleRate)
{
    switch (mode)
    {
        case TimbreBlendMode::Fade:
            blendFade(amp1, amp2, output, count, mix);
            break;

        case TimbreBlendMode::Subtract:
            blendSubtract(amp1, amp2, output, count);
            break;

        case TimbreBlendMode::Multiply:
            blendMultiply(amp1, amp2, output, count);
            break;

        case TimbreBlendMode::Maximum:
            blendMaximum(amp1, amp2, output, count);
            break;

        case TimbreBlendMode::Minimum:
            blendMinimum(amp1, amp2, output, count);
            break;

        case TimbreBlendMode::Pluck:
            blendPluck(amp1, amp2, output, count, mix, pluckDecay, sampleRate);
            break;
    }
}

//==============================================================================
// Fade  :  dest = a * (1-mix) + b * mix
//==============================================================================

void DualTimbre::blendFade(const float* a, const float* b,
                           float* dest, int count, float mix)
{
    const float oneMinusMix = 1.0f - mix;
    int i = 0;

#if defined(__AVX2__)
    const __m256 mmix        = _mm256_set1_ps(mix);
    const __m256 moneMinusMix = _mm256_set1_ps(oneMinusMix);

    for (; i + 8 <= count; i += 8)
    {
        const __m256 va = _mm256_loadu_ps(a + i);
        const __m256 vb = _mm256_loadu_ps(b + i);

        const __m256 result = _mm256_add_ps(
            _mm256_mul_ps(va, moneMinusMix),
            _mm256_mul_ps(vb, mmix));

        _mm256_storeu_ps(dest + i, result);
    }
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    const __m128 mmix        = _mm_set1_ps(mix);
    const __m128 moneMinusMix = _mm_set1_ps(oneMinusMix);

    for (; i + 4 <= count; i += 4)
    {
        const __m128 va = _mm_loadu_ps(a + i);
        const __m128 vb = _mm_loadu_ps(b + i);

        const __m128 result = _mm_add_ps(
            _mm_mul_ps(va, moneMinusMix),
            _mm_mul_ps(vb, mmix));

        _mm_storeu_ps(dest + i, result);
    }
#endif

    // Scalar remainder
    for (; i < count; ++i)
        dest[i] = a[i] * oneMinusMix + b[i] * mix;
}

//==============================================================================
// Subtract  :  dest = max(0, a - b)
//==============================================================================

void DualTimbre::blendSubtract(const float* a, const float* b,
                               float* dest, int count)
{
    int i = 0;

#if defined(__AVX2__)
    const __m256 zero = _mm256_setzero_ps();

    for (; i + 8 <= count; i += 8)
    {
        const __m256 va = _mm256_loadu_ps(a + i);
        const __m256 vb = _mm256_loadu_ps(b + i);

        __m256 result = _mm256_sub_ps(va, vb);
        result = _mm256_max_ps(result, zero);

        _mm256_storeu_ps(dest + i, result);
    }
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    const __m128 zero = _mm_setzero_ps();

    for (; i + 4 <= count; i += 4)
    {
        const __m128 va = _mm_loadu_ps(a + i);
        const __m128 vb = _mm_loadu_ps(b + i);

        __m128 result = _mm_sub_ps(va, vb);
        result = _mm_max_ps(result, zero);

        _mm_storeu_ps(dest + i, result);
    }
#endif

    // Scalar remainder
    for (; i < count; ++i)
        dest[i] = std::max(0.0f, a[i] - b[i]);
}

//==============================================================================
// Multiply  :  dest = a * b
//==============================================================================

void DualTimbre::blendMultiply(const float* a, const float* b,
                               float* dest, int count)
{
    // Reuse the existing SIMD-enabled vectorMul kernel.
    SIMDKernels::vectorMul(dest, a, b, count);
}

//==============================================================================
// Maximum  :  dest = max(a, b)
//==============================================================================

void DualTimbre::blendMaximum(const float* a, const float* b,
                              float* dest, int count)
{
    int i = 0;

#if defined(__AVX2__)
    for (; i + 8 <= count; i += 8)
    {
        const __m256 va = _mm256_loadu_ps(a + i);
        const __m256 vb = _mm256_loadu_ps(b + i);

        _mm256_storeu_ps(dest + i, _mm256_max_ps(va, vb));
    }
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    for (; i + 4 <= count; i += 4)
    {
        const __m128 va = _mm_loadu_ps(a + i);
        const __m128 vb = _mm_loadu_ps(b + i);

        _mm_storeu_ps(dest + i, _mm_max_ps(va, vb));
    }
#endif

    // Scalar remainder
    for (; i < count; ++i)
        dest[i] = std::max(a[i], b[i]);
}

//==============================================================================
// Minimum  :  dest = min(a, b)
//==============================================================================

void DualTimbre::blendMinimum(const float* a, const float* b,
                              float* dest, int count)
{
    int i = 0;

#if defined(__AVX2__)
    for (; i + 8 <= count; i += 8)
    {
        const __m256 va = _mm256_loadu_ps(a + i);
        const __m256 vb = _mm256_loadu_ps(b + i);

        _mm256_storeu_ps(dest + i, _mm256_min_ps(va, vb));
    }
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    for (; i + 4 <= count; i += 4)
    {
        const __m128 va = _mm_loadu_ps(a + i);
        const __m128 vb = _mm_loadu_ps(b + i);

        _mm_storeu_ps(dest + i, _mm_min_ps(va, vb));
    }
#endif

    // Scalar remainder
    for (; i < count; ++i)
        dest[i] = std::min(a[i], b[i]);
}

//==============================================================================
// Pluck  :  dest = a * (1-mix) + b * mix * exp(-index * pluckFactor)
//==============================================================================

void DualTimbre::blendPluck(const float* a, const float* b,
                            float* dest, int count,
                            float mix, float pluckDecay, float sampleRate)
{
    const float oneMinusMix = 1.0f - mix;
    // Decay factor per-partial-index: higher partials decay faster.
    const float pluckFactor = 1.0f / (pluckDecay * sampleRate);

    for (int i = 0; i < count; ++i)
    {
        const float decay = std::exp(-static_cast<float>(i) * pluckFactor);
        dest[i] = a[i] * oneMinusMix + b[i] * mix * decay;
    }
}

} // namespace ana
