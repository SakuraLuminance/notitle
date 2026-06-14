#pragma once
#include <vector>
#include <complex>
#include <juce_dsp/juce_dsp.h>
#include "AudioFileData.h"
#include "STFTConfig.h"
#include "SIMDSupport.h"

namespace ana {

class STFTAnalyzer
{
public:
    STFTAnalyzer();
    ~STFTAnalyzer();

    // Main analysis entry point (backward-compatible signature)
    std::vector<std::vector<std::complex<float>>> analyze(
        const AudioFileData& audio,
        const STFTConfig& config);

    // ---- New API ----

    /** Set the FFT size. Supported: 512, 1024, 2048, 4096. */
    void setFFTSize(int size);

    /** Set zero-padding factor (1, 2, or 4x). 1 = no padding. */
    void setZeroPadding(int factor);

    /** Return the latency incurred by the STFT overlap in samples
        (fftSize - hopSize). */
    int getLatencySamples() const;

private:
    // ---- SIMD-optimized kernels ----

    /** Multiply input frame by pre-computed window table.
        Uses AVX2 (8 floats) with SSE2 (4 floats) and scalar fallback. */
    void processWindowedFrame(const float* input,
                              float* windowed,
                              int fftSize);

    /** Extract complex spectrum from interleaved real/imag FFT output.
        Uses AVX2/SSE2 to de-interleave and store as complex<float>. */
    void extractSpectrum(const float* fftData,
                         std::complex<float>* spectrum,
                         int halfSize);

    /** Rebuild the window coefficient table. */
    void recomputeWindowTable(
        int fftSize,
        juce::dsp::WindowingFunction<float>::WindowingMethod method);

    // ---- Members ----

    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    std::vector<float> windowTable;  // pre-computed coefficients
    int currentFFTSize  = 2048;
    int currentHopSize  = 512;
    int zeroPadFactor   = 1;
};

} // namespace ana
