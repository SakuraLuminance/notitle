// ============================================================================
// AnaPlug DSP Performance Benchmark Suite
//
// Measures the performance of critical DSP kernels, voice management,
// data structure conversion, and memory access patterns.
//
// Each TEST_CASE is standalone and uses Catch2's BENCHMARK macro with
// iteration counts specified in the REQUIREMENTS document.
//
// Build: cmake --build . --target AnaPlugTests
// Run:   ctest --benchmark -R "^benchmark$"   or
//        ./AnaPlugTests [benchmark]
// ============================================================================

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_all.hpp>

#include <juce_dsp/juce_dsp.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

#include "dsp/PartialData.h"
#include "dsp/PartialDataSIMD.h"
#include "dsp/SIMDSupport.h"

// Intrinsics for SIMD benchmark kernels
// SIMDSupport.h includes immintrin.h / emmintrin.h conditionally.
// We include again here in case the build doesn't pull SIMDSupport.h first.
#if defined(__AVX2__) || defined(__AVX__)
#include <immintrin.h>
#elif defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#endif

// ============================================================================
// Helper utilities
// ============================================================================
namespace {

/// Deterministic RNG seeded with a fixed value for reproducible benchmarks.
std::mt19937& rng() noexcept
{
    static std::mt19937 rng{42};
    return rng;
}

/// Fill a vector with uniform random floats in [-1, 1].
std::vector<float> randomFloats(size_t n)
{
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> v(n);
    for (auto& x : v)
        x = dist(rng());
    return v;
}

/// Aligned heap allocation (MSVC-compatible).
template <typename T>
T* alignedAlloc(size_t count, size_t alignment = 32)
{
#ifdef _MSC_VER
    return static_cast<T*>(_aligned_malloc(count * sizeof(T), alignment));
#else
    return static_cast<T*>(std::aligned_alloc(alignment,
                                              count * sizeof(T)));
#endif
}

/// Free memory allocated with alignedAlloc.
void alignedFree(void* ptr) noexcept
{
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

// ---------------------------------------------------------------------------
// SIMD kernels matching the codebase style
// ---------------------------------------------------------------------------

/// Multiply `input` element-wise by `window` coefficients.
/// Mirror of STFTAnalyzer::processWindowedFrame.
void applyWindow(const float* input,
                 const float* window,
                 float* output,
                 int size) noexcept
{
    int i = 0;
#if defined(__AVX2__)
    for (; i + 8 <= size; i += 8)
    {
        __m256 in  = _mm256_loadu_ps(input + i);
        __m256 w   = _mm256_loadu_ps(window + i);
        _mm256_storeu_ps(output + i, _mm256_mul_ps(in, w));
    }
#endif
#if defined(__SSE2__)
    for (; i + 4 <= size; i += 4)
    {
        __m128 in  = _mm_loadu_ps(input + i);
        __m128 w   = _mm_loadu_ps(window + i);
        _mm_storeu_ps(output + i, _mm_mul_ps(in, w));
    }
#endif
    for (; i < size; ++i)
        output[i] = input[i] * window[i];
}

/// Element-wise add: c[i] = a[i] + b[i]
void vectorAddKernel(const float* a,
                     const float* b,
                     float* c,
                     int size) noexcept
{
    int i = 0;
#if defined(__AVX2__)
    for (; i + 8 <= size; i += 8)
    {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        _mm256_storeu_ps(c + i, _mm256_add_ps(va, vb));
    }
#endif
#if defined(__SSE2__)
    for (; i + 4 <= size; i += 4)
    {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        _mm_storeu_ps(c + i, _mm_add_ps(va, vb));
    }
#endif
    for (; i < size; ++i)
        c[i] = a[i] + b[i];
}

/// Element-wise multiply: c[i] = a[i] * b[i]
void vectorMulKernel(const float* a,
                     const float* b,
                     float* c,
                     int size) noexcept
{
    int i = 0;
#if defined(__AVX2__)
    for (; i + 8 <= size; i += 8)
    {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        _mm256_storeu_ps(c + i, _mm256_mul_ps(va, vb));
    }
#endif
#if defined(__SSE2__)
    for (; i + 4 <= size; i += 4)
    {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        _mm_storeu_ps(c + i, _mm_mul_ps(va, vb));
    }
#endif
    for (; i < size; ++i)
        c[i] = a[i] * b[i];
}

/// Horitzontal sum of all elements.
float vectorSumKernel(const float* a, int size) noexcept
{
    float total = 0.0f;
    int i = 0;
#if defined(__AVX2__)
    __m256 vsum = _mm256_setzero_ps();
    for (; i + 8 <= size; i += 8)
        vsum = _mm256_add_ps(vsum, _mm256_loadu_ps(a + i));
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, vsum);
    for (int j = 0; j < 8; ++j)
        total += tmp[j];
#endif
#if defined(__SSE2__) && !defined(__AVX2__)
    __m128 vsum = _mm_setzero_ps();
    for (; i + 4 <= size; i += 4)
        vsum = _mm_add_ps(vsum, _mm_loadu_ps(a + i));
    alignas(16) float tmp[4];
    _mm_store_ps(tmp, vsum);
    for (int j = 0; j < 4; ++j)
        total += tmp[j];
#endif
    for (; i < size; ++i)
        total += a[i];
    return total;
}

// ---------------------------------------------------------------------------
// Inline voice pool for the voice-allocation benchmark
//
// Mirrors the allocation/release logic of ana::VoiceManager but lives
// entirely in this translation unit so no linker dependency on
// VoiceManager.cpp is needed.
// ---------------------------------------------------------------------------

/// Simplified voice states for the benchmark pool.
enum class BenchVoiceState : uint8_t
{
    free    = 0,
    active  = 1,
    release = 2,
    idle    = 3
};

/// A single voice slot in the benchmark pool.
struct BenchVoice
{
    BenchVoiceState state      = BenchVoiceState::free;
    int             note       = -1;
    uint64_t        allocOrder = 0; ///< Monotonically increasing allocation id
};

/// Simple round-robin voice pool.
class BenchVoicePool
{
public:
    static constexpr int kMaxVoices = 32;

    BenchVoicePool() { voices = {}; }

    /// Allocate a voice for note-on. Returns slot index or -1 on steal.
    int noteOn(int note)
    {
        // 1) Find a free slot
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const int idx = (nextSlot + i) % kMaxVoices;
            if (voices[idx].state == BenchVoiceState::free)
            {
                voices[idx] = {};
                voices[idx].state      = BenchVoiceState::active;
                voices[idx].note       = note;
                voices[idx].allocOrder = ++counter;
                nextSlot = (idx + 1) % kMaxVoices;
                return idx;
            }
        }
        // 2) No free slot — steal oldest active note
        {
            int oldestIdx  = 0;
            uint64_t oldest = UINT64_MAX;
            for (int i = 0; i < kMaxVoices; ++i)
            {
                if (voices[i].allocOrder < oldest &&
                    voices[i].state != BenchVoiceState::free)
                {
                    oldest   = voices[i].allocOrder;
                    oldestIdx = i;
                }
            }
            voices[oldestIdx] = {};
            voices[oldestIdx].state      = BenchVoiceState::active;
            voices[oldestIdx].note       = note;
            voices[oldestIdx].allocOrder = ++counter;
            nextSlot = (oldestIdx + 1) % kMaxVoices;
            return oldestIdx;
        }
    }

    /// Release all voices matching the given note.
    void noteOff(int note)
    {
        for (auto& v : voices)
        {
            if (v.state != BenchVoiceState::free && v.note == note)
                v.state = BenchVoiceState::release;
        }
    }

    /// Advance one audio sample — transition release voices to idle,
    /// then recycle idle voices back to free.
    void tick()
    {
        // Advance envelopes (simplified: immediate release -> idle)
        for (auto& v : voices)
        {
            if (v.state == BenchVoiceState::release)
                v.state = BenchVoiceState::idle;
            else if (v.state == BenchVoiceState::idle)
                v.state = BenchVoiceState::free;
        }
    }

    int numActive() const noexcept
    {
        int n = 0;
        for (auto& v : voices)
            if (v.state != BenchVoiceState::free)
                ++n;
        return n;
    }

private:
    std::array<BenchVoice, kMaxVoices> voices{};
    int      nextSlot = 0;
    uint64_t counter  = 0;
};

// ---------------------------------------------------------------------------
// SoA vs AoS comparison structures for the memory-access benchmark
// ---------------------------------------------------------------------------

/// AoS: Array of Structures — 4 float fields interleaved per element.
struct ParticleAoS
{
    float x, y, z, w;
};

/// SoA: Structure of Arrays — each field lives in its own contiguous span.
struct alignas(32) ParticleSoA
{
    float x[1024];
    float y[1024];
    float z[1024];
    float w[1024];
};

} // anonymous namespace

// ============================================================================
// Benchmark 1 — STFT Windowing
//
// Measures SIMD-accelerated Hann-window multiply for 2048-sample frames,
// repeated 100 times per measurement to amplify the kernel cost.
// ============================================================================
TEST_CASE("STFT Windowing", "[benchmark]")
{
    constexpr int fftSize = 2048;

    const auto input    = randomFloats(fftSize);
    auto       output   = randomFloats(fftSize);

    // Pre-compute a Hann window table (same method as STFTAnalyzer)
    juce::dsp::WindowingFunction<float> windowFunc(
        fftSize, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> windowTable(fftSize, 1.0f);
    windowFunc.multiplyWithWindowingTable(windowTable.data(), fftSize);

    BENCHMARK("windowing 2048 samples x100")
    {
        for (int iter = 0; iter < 100; ++iter)
            applyWindow(input.data(), windowTable.data(),
                        output.data(), fftSize);
        // Return a value that depends on the computation so the compiler
        // cannot elide the loop.
        return output[fftSize / 2];
    };
}

// ============================================================================
// Benchmark 2 — Partial Data Conversion (AoS ↔ SoA)
//
// Times conversion between the legacy PartialData (AoS / std::vector
// of Partial frames) and the new PartialDataSIMD (SoA with explicit
// SIMD alignment).  Also times the reverse direction.
// ============================================================================
TEST_CASE("Partial Data Conversion", "[benchmark]")
{
    std::uniform_real_distribution<float> freqDist(20.0f, 8000.0f);
    std::uniform_real_distribution<float> ampDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, 6.2832f);

    constexpr int numPartials = 512;
    constexpr int numFrames   = 10;

    // Build a PartialData with several frames of rich partial data
    ana::PartialData pd;
    pd.maxPartials = numPartials;
    pd.sampleRate  = 44100.0;
    pd.hopSize     = 512.0;

    for (int f = 0; f < numFrames; ++f)
    {
        ana::PartialFrame frame;
        frame.timestamp = static_cast<double>(f) * 0.01;
        frame.partials.reserve(static_cast<size_t>(numPartials));
        for (int i = 0; i < numPartials; ++i)
        {
            ana::Partial p;
            p.frequency = freqDist(rng());
            p.amplitude = ampDist(rng());
            p.phase     = phaseDist(rng());
            frame.partials.push_back(p);
        }
        pd.frames.push_back(std::move(frame));
    }

    BENCHMARK("PartialData -> PartialDataSIMD")
    {
        return ana::PartialDataSIMD::fromPartialData(pd);
    };

    // Prepare the SIMD data for the reverse conversion
    auto simd = ana::PartialDataSIMD::fromPartialData(pd);

    BENCHMARK("PartialDataSIMD -> PartialData")
    {
        return simd.toPartialData();
    };
}

// ============================================================================
// Benchmark 3 — Vector Add / Multiply / Sum
//
// 10 000 iterations of SIMD-accelerated add, mul, and horizontal sum on
// 1024-element vectors.  Each kernel mirrors the coding style used in
// STFTAnalyzer (AVX2 → SSE2 → scalar fallback).
// ============================================================================
TEST_CASE("Vector Add/Mul/Sum", "[benchmark]")
{
    constexpr int numElements    = 1024;
    constexpr int numIterations  = 10000;

    // Allocate aligned memory as the production code expects
    auto* a = alignedAlloc<float>(numElements);
    auto* b = alignedAlloc<float>(numElements);
    auto* c = alignedAlloc<float>(numElements);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    {
        const auto va = randomFloats(numElements);
        const auto vb = randomFloats(numElements);
        std::memcpy(a, va.data(), numElements * sizeof(float));
        std::memcpy(b, vb.data(), numElements * sizeof(float));
        std::memset(c, 0, numElements * sizeof(float));
    }

    // --- add ---
    BENCHMARK("vector add 1024 x10000")
    {
        for (int iter = 0; iter < numIterations; ++iter)
            vectorAddKernel(a, b, c, numElements);
        return c[0]; // prevent dead-code elimination
    };

    // --- mul ---
    BENCHMARK("vector mul 1024 x10000")
    {
        for (int iter = 0; iter < numIterations; ++iter)
            vectorMulKernel(a, b, c, numElements);
        return c[numElements - 1];
    };

    // --- sum ---
    BENCHMARK("vector sum 1024 x10000")
    {
        float sum = 0.0f;
        for (int iter = 0; iter < numIterations; ++iter)
            sum += vectorSumKernel(a, numElements);
        return sum;
    };

    alignedFree(a);
    alignedFree(b);
    alignedFree(c);
}

// ============================================================================
// Benchmark 4 — Voice Allocation
//
// Simulates 1000 note-on / note-off cycles (including voice stealing)
// on a 32-voice pool using a simplified inline voice manager that
// mirrors the allocation strategy of ana::VoiceManager.
// ============================================================================
TEST_CASE("Voice Allocation", "[benchmark]")
{
    constexpr int numCycles = 1000;
    BenchVoicePool pool;

    // Fill the pool to capacity first
    for (int i = 0; i < BenchVoicePool::kMaxVoices; ++i)
        pool.noteOn(60 + i);

    BENCHMARK("voice alloc 1000 note-on/off cycles")
    {
        for (int cycle = 0; cycle < numCycles; ++cycle)
        {
            const int note = 60 + (cycle % 48);         // stay in MIDI range
            pool.noteOff(note);                         // release existing
            for (int t = 0; t < 10; ++t) pool.tick();   // advance envelope
            pool.noteOn((note + 5) % 128);              // allocate a new note
        }
        return pool.numActive();
    };
}

// ============================================================================
// Benchmark 5 — Memory Access: SoA vs AoS
//
// Compares sum-of-fields access throughput for 1024 particles stored
// in Array-of-Structures vs Structure-of-Arrays layouts.  Repeated
// 10 000 times to amplify cache / prefetch effects.
// ============================================================================
TEST_CASE("Memory Access SoA vs AoS", "[benchmark]")
{
    constexpr int numIterations = 10000;

    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    // --- AoS setup ---
    auto aos = std::vector<ParticleAoS>(1024);
    for (auto& p : aos)
        p = { dist(rng()), dist(rng()), dist(rng()), dist(rng()) };

    // --- SoA setup ---
    auto* soa = alignedAlloc<ParticleSoA>(1);
    REQUIRE(soa != nullptr);
    for (int i = 0; i < 1024; ++i)
    {
        soa->x[i] = dist(rng());
        soa->y[i] = dist(rng());
        soa->z[i] = dist(rng());
        soa->w[i] = dist(rng());
    }

    BENCHMARK("AoS sum x10000")
    {
        float sum = 0.0f;
        for (int iter = 0; iter < numIterations; ++iter)
            for (const auto& p : aos)
                sum += p.x + p.y + p.z + p.w;
        return sum;
    };

    BENCHMARK("SoA sum x10000")
    {
        float sum = 0.0f;
        for (int iter = 0; iter < numIterations; ++iter)
            for (int i = 0; i < 1024; ++i)
                sum += soa->x[i] + soa->y[i] + soa->z[i] + soa->w[i];
        return sum;
    };

    alignedFree(soa);
}
