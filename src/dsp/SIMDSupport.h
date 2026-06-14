#pragma once

// ============================================================================
// SIMD Support Utilities
// Provides SIMD detection macros and kernel type definitions for AnaPlug.
// ============================================================================

#include <type_traits>
#include <cstdint>

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

} // namespace SIMDKernels
} // namespace ana
