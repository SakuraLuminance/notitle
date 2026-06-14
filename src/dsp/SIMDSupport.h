#pragma once

// ============================================================================
// SIMD Support Utilities
// Provides SIMD detection macros and kernel type definitions for AnaPlug.
// ============================================================================

#include <type_traits>
#include <cstdint>
#include <cmath>

// SIMD header includes (platform-independent)
#if defined(__AVX2__) || defined(__AVX__)
#include <immintrin.h>
#elif defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#endif

// ARM NEON (Apple Silicon / ARM64)
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif

namespace ana {
namespace simd {

// ----------------------------------------------------------------------------
// SIMD level detection
// ----------------------------------------------------------------------------
enum class SIMDLevel
{
    Scalar,
    NEON,
    SSE2,
    AVX2
};

// Compile-time SIMD level detection
inline constexpr SIMDLevel getSIMDLevel() noexcept
{
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    return SIMDLevel::NEON;
#elif defined(__AVX2__)
    return SIMDLevel::AVX2;
#elif defined(__SSE2__) || defined(_M_X64)
    return SIMDLevel::SSE2;
#else
    return SIMDLevel::Scalar;
#endif
}

// ----------------------------------------------------------------------------
// Kernel type tags for compile-time dispatch
// ----------------------------------------------------------------------------
struct ScalarKernel  {};
struct NEONKernel    {};
struct SSE2Kernel    {};
struct AVX2Kernel    {};

// Select kernel type based on available SIMD at compile time
using DefaultKernel = std::conditional_t<
    getSIMDLevel() >= SIMDLevel::AVX2, AVX2Kernel,
    std::conditional_t<
        getSIMDLevel() >= SIMDLevel::SSE2, SSE2Kernel,
        std::conditional_t<
            getSIMDLevel() >= SIMDLevel::NEON, NEONKernel,
            ScalarKernel>>>;

// ----------------------------------------------------------------------------
// Alignment helpers
// ----------------------------------------------------------------------------
inline constexpr int kSIMDAlignmentBytes = 32; // 256-bit (AVX) / 128-bit (SSE2/NEON) alignment

// Returns true if pointer is aligned to kSIMDAlignmentBytes
inline bool isAligned(const void* ptr) noexcept
{
    return (reinterpret_cast<uintptr_t>(ptr) & (kSIMDAlignmentBytes - 1)) == 0;
}

} // namespace simd

// ----------------------------------------------------------------------------
// SIMD Kernel functions
// ----------------------------------------------------------------------------
namespace SIMDKernels {

/** Detect the highest SIMD level available at compile time. */
inline simd::SIMDLevel detectSIMDLevel() noexcept
{
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    return simd::SIMDLevel::NEON;
#elif defined(__AVX2__)
    return simd::SIMDLevel::AVX2;
#elif defined(__SSE2__) || defined(_M_X64)
    return simd::SIMDLevel::SSE2;
#else
    return simd::SIMDLevel::Scalar;
#endif
}

/** Multiply elements of src by coeffs, store to dest.
    Uses AVX2 (8 floats/iter), SSE2/NEON (4 floats/iter), or scalar fallback.
    Safe for any len >= 0; handles remainder via scalar tail.
    All pointers must be readable for len floats. */
inline void vectorMul(float* dest,
                      const float* src,
                      const float* coeffs,
                      int len) noexcept
{
    int i = 0;

#if defined(__AVX2__)
    for (; i + 8 <= len; i += 8)
    {
        _mm256_storeu_ps(dest + i,
            _mm256_mul_ps(_mm256_loadu_ps(src + i),
                          _mm256_loadu_ps(coeffs + i)));
    }
#endif

#if defined(__SSE2__)
    for (; i + 4 <= len; i += 4)
    {
        _mm_storeu_ps(dest + i,
            _mm_mul_ps(_mm_loadu_ps(src + i),
                       _mm_loadu_ps(coeffs + i)));
    }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    for (; i + 4 <= len; i += 4)
    {
        vst1q_f32(dest + i,
            vmulq_f32(vld1q_f32(src + i),
                      vld1q_f32(coeffs + i)));
    }
#endif

    // Scalar remainder
    for (; i < len; ++i)
        dest[i] = src[i] * coeffs[i];
}

/** Add elements of src to dest, element-wise.
    Uses AVX2 (8 floats/iter), SSE2/NEON (4 floats/iter), or scalar fallback.
    Safe for any len >= 0; handles remainder via scalar tail.
    All pointers must be readable for len floats. */
inline void vectorAdd(float* dest,
                      const float* src,
                      int len) noexcept
{
    int i = 0;

#if defined(__AVX2__)
    for (; i + 8 <= len; i += 8)
    {
        _mm256_storeu_ps(dest + i,
            _mm256_add_ps(_mm256_loadu_ps(dest + i),
                          _mm256_loadu_ps(src + i)));
    }
#endif

#if defined(__SSE2__)
    for (; i + 4 <= len; i += 4)
    {
        _mm_storeu_ps(dest + i,
            _mm_add_ps(_mm_loadu_ps(dest + i),
                       _mm_loadu_ps(src + i)));
    }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    for (; i + 4 <= len; i += 4)
    {
        vst1q_f32(dest + i,
            vaddq_f32(vld1q_f32(dest + i),
                      vld1q_f32(src + i)));
    }
#endif

    // Scalar remainder
    for (; i < len; ++i)
        dest[i] = dest[i] + src[i];
}

/** Fill a buffer with a constant value.
    Uses AVX2 (8 floats/iter), SSE2/NEON (4 floats/iter), or scalar fallback.
    Safe for any len >= 0; handles remainder via scalar tail. */
inline void vectorFill(float* dest, float value, int len) noexcept
{
    int i = 0;

#if defined(__AVX2__)
    {
    auto v = _mm256_set1_ps(value);
    for (; i + 8 <= len; i += 8) { _mm256_storeu_ps(dest + i, v); }
    }
#endif
#if defined(__SSE2__) && !defined(__AVX2__)
    {
    auto v = _mm_set1_ps(value);
    for (; i + 4 <= len; i += 4) { _mm_storeu_ps(dest + i, v); }
    }
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    {
    auto v = vdupq_n_f32(value);
    for (; i + 4 <= len; i += 4) { vst1q_f32(dest + i, v); }
    }
#endif

    // Scalar remainder
    for (; i < len; ++i)
        dest[i] = value;
}

/** Linearly interpolate between a and b: dest[i] = a[i]*(1-t) + b[i]*t.
    Uses AVX2 (8 floats/iter), SSE2/NEON (4 floats/iter), or scalar fallback.
    Safe for any len >= 0; handles remainder via scalar tail.
    All pointers must be readable for len floats. */
inline void vectorLerp(float* dest,
                       const float* a,
                       const float* b,
                       float t,
                       int len) noexcept
{
    int i = 0;

#if defined(__AVX2__)
    {
    auto one_minus_t = _mm256_set1_ps(1.0f - t);
    auto vt = _mm256_set1_ps(t);
    for (; i + 8 <= len; i += 8)
    {
        _mm256_storeu_ps(dest + i,
            _mm256_add_ps(
                _mm256_mul_ps(_mm256_loadu_ps(a + i), one_minus_t),
                _mm256_mul_ps(_mm256_loadu_ps(b + i), vt)));
    }
    }
#endif

#if defined(__SSE2__)
    {
    auto one_minus_t = _mm_set1_ps(1.0f - t);
    auto vt = _mm_set1_ps(t);
    for (; i + 4 <= len; i += 4)
    {
        _mm_storeu_ps(dest + i,
            _mm_add_ps(
                _mm_mul_ps(_mm_loadu_ps(a + i), one_minus_t),
                _mm_mul_ps(_mm_loadu_ps(b + i), vt)));
    }
    }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    {
    auto one_minus_t = vdupq_n_f32(1.0f - t);
    auto vt = vdupq_n_f32(t);
    for (; i + 4 <= len; i += 4)
    {
        vst1q_f32(dest + i,
            vmlaq_f32(
                vmulq_f32(vld1q_f32(a + i), one_minus_t),
                vld1q_f32(b + i),
                vt));
    }
    }
#endif

    // Scalar remainder
    for (; i < len; ++i)
        dest[i] = a[i] * (1.0f - t) + b[i] * t;
}

/** Compare elements of src against threshold, return bitmask of elements > threshold.
    Uses AVX2 (8 comparisons/iter), SSE2 (4 comparisons/iter), NEON (4 comparisons/iter),
    or scalar fallback.  Returns up to 32 bits (one per element).
    Safe for any len >= 0; bits beyond len remain 0. */
inline uint32_t vectorCompareMask(const float* src,
                                  float threshold,
                                  int len) noexcept
{
    uint32_t result = 0;
    int i = 0;
    int bit_offset = 0;

#if defined(__AVX2__)
    {
    auto vt = _mm256_set1_ps(threshold);
    for (; i + 8 <= len; i += 8)
    {
        auto cmp = _mm256_cmp_ps(_mm256_loadu_ps(src + i), vt, _CMP_GT_OQ);
        result |= (uint32_t)_mm256_movemask_ps(cmp) << bit_offset;
        bit_offset += 8;
    }
    }
#endif

#if defined(__SSE2__)
    {
    auto vt = _mm_set1_ps(threshold);
    for (; i + 4 <= len; i += 4)
    {
        auto cmp = _mm_cmpgt_ps(_mm_loadu_ps(src + i), vt);
        result |= (uint32_t)_mm_movemask_ps(cmp) << bit_offset;
        bit_offset += 4;
    }
    }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    {
    auto vt = vdupq_n_f32(threshold);
    for (; i + 4 <= len; i += 4)
    {
        auto cmp = vcgtq_f32(vld1q_f32(src + i), vt);
        uint32_t bits = 0;
        bits |= (vgetq_lane_u32(cmp, 0) >> 31) << 0;
        bits |= (vgetq_lane_u32(cmp, 1) >> 31) << 1;
        bits |= (vgetq_lane_u32(cmp, 2) >> 31) << 2;
        bits |= (vgetq_lane_u32(cmp, 3) >> 31) << 3;
        result |= bits << bit_offset;
        bit_offset += 4;
    }
    }
#endif

    // Scalar remainder
    for (; i < len; ++i, ++bit_offset)
        if (src[i] > threshold)
            result |= 1u << bit_offset;

    return result;
}

/** Compute sqrt(real[i]^2 + imag[i]^2) for each element (complex magnitude).
    Uses AVX2 (8 floats/iter), SSE2/NEON (4 floats/iter), or scalar fallback.
    Safe for any len >= 0; handles remainder via scalar tail.
    All pointers must be readable for len floats. */
inline void vectorSqrtSumSquares(float* dest,
                                 const float* real,
                                 const float* imag,
                                 int len) noexcept
{
    int i = 0;

#if defined(__AVX2__)
    for (; i + 8 <= len; i += 8)
    {
        auto r = _mm256_loadu_ps(real + i);
        auto im = _mm256_loadu_ps(imag + i);
        _mm256_storeu_ps(dest + i,
            _mm256_sqrt_ps(
                _mm256_add_ps(
                    _mm256_mul_ps(r, r),
                    _mm256_mul_ps(im, im))));
    }
#endif

#if defined(__SSE2__)
    for (; i + 4 <= len; i += 4)
    {
        auto r = _mm_loadu_ps(real + i);
        auto im = _mm_loadu_ps(imag + i);
        _mm_storeu_ps(dest + i,
            _mm_sqrt_ps(
                _mm_add_ps(
                    _mm_mul_ps(r, r),
                    _mm_mul_ps(im, im))));
    }
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    for (; i + 4 <= len; i += 4)
    {
        auto r = vld1q_f32(real + i);
        auto im = vld1q_f32(imag + i);
        vst1q_f32(dest + i,
            vsqrtq_f32(
                vaddq_f32(
                    vmulq_f32(r, r),
                    vmulq_f32(im, im))));
    }
#endif

    // Scalar remainder
    for (; i < len; ++i)
        dest[i] = sqrtf(real[i] * real[i] + imag[i] * imag[i]);
}

} // namespace SIMDKernels
} // namespace ana
