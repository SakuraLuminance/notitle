#include "STFTAnalyzer.h"
#include "ParallelProcessor.h"
#include <cmath>
#include <cstring>

namespace ana {

// ============================================================================
// Construction / Destruction
// ============================================================================

STFTAnalyzer::STFTAnalyzer()
{
}

STFTAnalyzer::~STFTAnalyzer()
{
}

// ============================================================================
// New API
// ============================================================================

void STFTAnalyzer::setFFTSize(int size)
{
    jassert(size == 512 || size == 1024 || size == 2048 || size == 4096);
    currentFFTSize = size;
}

void STFTAnalyzer::setZeroPadding(int factor)
{
    jassert(factor == 1 || factor == 2 || factor == 4);
    zeroPadFactor = factor;
}

int STFTAnalyzer::getLatencySamples() const
{
    return currentFFTSize - currentHopSize;
}

// ============================================================================
// Window table pre-computation
// ============================================================================

void STFTAnalyzer::recomputeWindowTable(
    int fftSize,
    juce::dsp::WindowingFunction<float>::WindowingMethod method)
{
    windowTable.resize(fftSize, 1.0f);
    juce::dsp::WindowingFunction<float> temp(fftSize, method);
    temp.multiplyWithWindowingTable(windowTable.data(), fftSize);
}

// ============================================================================
// SIMD-optimized: windowed frame copy
//
// Multiplies each input sample by the pre-computed window coefficient.
// AVX2  path: 8 floats per iteration
// SSE2  path: 4 floats per iteration
// Scalar tail: remaining samples
// ============================================================================

void STFTAnalyzer::processWindowedFrame(const float* input,
                                        float* windowed,
                                        int fftSize)
{
    SIMDKernels::vectorMul(windowed, input, windowTable.data(), fftSize);
}

// ============================================================================
// SIMD-optimized: complex spectrum extraction from interleaved FFT output
//
// JUCE's performRealOnlyForwardTransform stores the result in-place as
// interleaved real/imag pairs: [r0, i0, r1, i1, ..., rN, iN].
// This method extracts those into std::complex<float> objects.
//
// AVX2  path: 4 complex values (8 floats) per iteration via lane shuffles
// SSE2  path: 2 complex values (4 floats) per iteration
// Scalar tail: remaining values
// ============================================================================

void STFTAnalyzer::extractSpectrum(const float* fftData,
                                   std::complex<float>* spectrum,
                                   int halfSize)
{
    int i = 0;

#if defined(__AVX2__)
    // Process 4 complex values at once (8 interleaved floats)
    for (; i + 4 <= halfSize; i += 4)
    {
        // Load 8 floats: [r0, i0, r1, i1, r2, i2, r3, i3]
        __m256 d = _mm256_loadu_ps(fftData + i * 2);

        // Split into low/high 128-bit lanes
        __m128 lo = _mm256_castps256_ps128(d);          // [r0, i0, r1, i1]
        __m128 hi = _mm256_extractf128_ps(d, 1);        // [r2, i2, r3, i3]

        // De-interleave: real = [r0, r1, r2, r3], imag = [i0, i1, i2, i3]
        __m128 real = _mm_shuffle_ps(lo, hi, _MM_SHUFFLE(2, 0, 2, 0));
        __m128 imag = _mm_shuffle_ps(lo, hi, _MM_SHUFFLE(3, 1, 3, 1));

        // Spill to temp and construct complex values
        float re[4], im[4];
        _mm_storeu_ps(re, real);
        _mm_storeu_ps(im, imag);

        spectrum[i + 0] = std::complex<float>(re[0], im[0]);
        spectrum[i + 1] = std::complex<float>(re[1], im[1]);
        spectrum[i + 2] = std::complex<float>(re[2], im[2]);
        spectrum[i + 3] = std::complex<float>(re[3], im[3]);
    }
#endif

#if defined(__SSE2__)
    // Process 2 complex values at once (4 interleaved floats)
    for (; i + 2 <= halfSize; i += 2)
    {
        __m128 d = _mm_loadu_ps(fftData + i * 2);   // [r0, i0, r1, i1]
        float arr[4];
        _mm_storeu_ps(arr, d);

        spectrum[i + 0] = std::complex<float>(arr[0], arr[1]);
        spectrum[i + 1] = std::complex<float>(arr[2], arr[3]);
    }
#endif

    // Scalar remainder
    for (; i < halfSize; ++i)
    {
        spectrum[i] = std::complex<float>(
            fftData[i * 2],
            fftData[i * 2 + 1]);
    }
}

// ============================================================================
// Main analysis
// ============================================================================

std::vector<std::vector<std::complex<float>>> STFTAnalyzer::analyze(
    const AudioFileData& audio,
    const STFTConfig& config)
{
    const int fftSize     = config.fftSize;
    const int hopSize     = config.hopSize;
    const int numSamples  = static_cast<int>(audio.samples.size());
    const int paddedSize  = fftSize * zeroPadFactor;

    // Initialize FFT (use padded size when zero-padding is active)
    const int fftOrder = static_cast<int>(std::log2(paddedSize));
    fft  = std::make_unique<juce::dsp::FFT>(fftOrder);

    // Resolve window type from config
    using WinMethod = juce::dsp::WindowingFunction<float>::WindowingMethod;
    WinMethod winType = WinMethod::hann;

    if (config.windowType == STFTConfig::WindowType::BlackmanHarris)
        winType = WinMethod::blackmanHarris;
    else if (config.windowType == STFTConfig::WindowType::Hamming)
        winType = WinMethod::hamming;

    // Pre-compute window coefficients once per analysis run
    recomputeWindowTable(fftSize, winType);

    // Sync internal state
    currentFFTSize = fftSize;
    currentHopSize = hopSize;

    const int halfSize = paddedSize / 2 + 1;
    const int numFrames = (numSamples - fftSize) / hopSize + 1;
    if (numFrames <= 0) return {};

    std::vector<std::vector<std::complex<float>>> frames(numFrames);

    ParallelProcessor pp;
    pp.init(); // Auto-detect threads

    pp.parallelFor(0, numFrames, [&](int startFrame, int endFrame) {
        // Thread-local FFT engine
        juce::dsp::FFT localFft(fftOrder);
        
        // Reusable buffers for this thread's chunk
        std::vector<float> fftBuffer(paddedSize, 0.0f);
        std::vector<std::complex<float>> spectrum(halfSize);

        for (int i = startFrame; i < endFrame; ++i)
        {
            const int pos = i * hopSize;

            // Zero-initialise the padded tail if zero-padding is active
            if (paddedSize > fftSize)
            {
                std::memset(fftBuffer.data() + fftSize, 0, (paddedSize - fftSize) * sizeof(float));
            }

            // SIMD-optimized: window the frame into the start of the buffer
            processWindowedFrame(audio.samples.data() + pos,
                                 fftBuffer.data(),
                                 fftSize);

            // Forward FFT (in-place, interleaved real/imag result)
            localFft.performRealOnlyForwardTransform(fftBuffer.data());

            // SIMD-optimized: extract complex spectrum
            extractSpectrum(fftBuffer.data(), spectrum.data(), halfSize);

            frames[i] = spectrum;
        }
    });

    return frames;
}

} // namespace ana
