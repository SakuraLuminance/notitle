#include "ResynthesisEngine.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>

namespace ana {

ResynthesisEngine::ResynthesisEngine()
{
}

ResynthesisEngine::~ResynthesisEngine()
{
}

std::vector<float> ResynthesisEngine::resynthesize(
    const PartialData& partialData,
    const STFTConfig& config)
{
    const int fftSize = config.fftSize;
    const int hopSize = config.hopSize;
    const size_t numFrames = partialData.frames.size();

    // Calculate output length
    const size_t outputLength = (numFrames - 1) * static_cast<size_t>(hopSize) + static_cast<size_t>(fftSize);
    std::vector<float> output(outputLength, 0.0f);
    std::vector<float> windowCorrection(outputLength, 0.0f);

    // Initialize FFT
    const int fftOrder = static_cast<int>(std::log2(fftSize));
    juce::dsp::FFT fft(fftOrder);

    // Create synthesis window (same as analysis)
    juce::dsp::WindowingFunction<float> window(
        fftSize, juce::dsp::WindowingFunction<float>::hann);

    for (size_t frameIdx = 0; frameIdx < numFrames; ++frameIdx)
    {
        const auto& frame = partialData.frames[frameIdx];

        // Create empty spectrum
        std::vector<float> spectrum(fftSize * 2, 0.0f);

        // Write partials to spectrum
        for (const auto& partial : frame.partials)
        {
            float binF = partial.frequency * fftSize / static_cast<float>(partialData.sampleRate);
            int bin = static_cast<int>(std::round(binF));

            if (bin >= 0 && bin < fftSize / 2 + 1)
            {
                // Write amplitude and phase to complex bin
                spectrum[bin * 2]     = partial.amplitude * std::cos(partial.phase);  // real
                spectrum[bin * 2 + 1] = partial.amplitude * std::sin(partial.phase);  // imaginary
            }
        }

        // Perform inverse FFT
        fft.performRealOnlyInverseTransform(spectrum.data());

        // Apply synthesis window
        window.multiplyWithWindowingTable(spectrum.data(), fftSize);

        // Overlap-add into output using SIMD-accelerated JUCE functions
        const size_t offset = frameIdx * static_cast<size_t>(hopSize);
        if (offset + fftSize <= outputLength)
        {
            juce::FloatVectorOperations::add(output.data() + offset, spectrum.data(), fftSize);
            juce::FloatVectorOperations::add(windowCorrection.data() + offset, 1.0f, fftSize);
        }
    }

    // Normalize by window correction factor
    for (size_t i = 0; i < outputLength; ++i)
    {
        if (windowCorrection[i] > 0.0f)
        {
            output[i] /= windowCorrection[i];
        }
    }

    // Final normalization to [-1.0, 1.0]
    float maxAbs = 0.0f;
    for (float sample : output)
    {
        maxAbs = std::max(maxAbs, std::abs(sample));
    }

    if (maxAbs > 0.0f)
    {
        for (float& sample : output)
        {
            sample /= maxAbs;
        }
    }

    return output;
}

} // namespace ana
