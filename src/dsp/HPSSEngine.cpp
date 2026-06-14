#include "HPSSEngine.h"
#include <algorithm>
#include <cmath>

namespace ana {

HPSSEngine::HPSSEngine()
{
}

void HPSSEngine::medianFilter1D(const float* input, float* output, int length, int filterSize, int stride)
{
    int halfSize = filterSize / 2;
    std::vector<float> window(filterSize);

    for (int i = 0; i < length; ++i)
    {
        int winCount = 0;
        for (int j = -halfSize; j <= halfSize; ++j)
        {
            int idx = i + j;
            if (idx >= 0 && idx < length)
            {
                window[winCount++] = input[idx * stride];
            }
            else
            {
                // pad with zero or edge value (zero here)
                window[winCount++] = 0.0f;
            }
        }
        
        std::sort(window.begin(), window.begin() + winCount);
        output[i * stride] = window[winCount / 2];
    }
}

HPSSEngine::SeparationResult HPSSEngine::separate(
    const std::vector<float>& inputAudio, 
    double sampleRate,
    int filterLengthTime, 
    int filterLengthFreq)
{
    SeparationResult result;
    result.sampleRate = sampleRate;

    if (inputAudio.empty())
        return result;

    const int fftSize = 1024;
    const int hopSize = 256;
    const int numFrames = static_cast<int>(inputAudio.size()) / hopSize;
    if (numFrames < 2) return result;

    juce::dsp::FFT fft(static_cast<int>(std::log2(fftSize)));
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);

    // 1. Compute STFT
    int numBins = fftSize / 2 + 1;
    std::vector<float> stftReal(numFrames * numBins, 0.0f);
    std::vector<float> stftImag(numFrames * numBins, 0.0f);
    std::vector<float> mag(numFrames * numBins, 0.0f);

    std::vector<float> timeBuf(fftSize * 2, 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        int offset = f * hopSize;
        std::fill(timeBuf.begin(), timeBuf.end(), 0.0f);
        
        for (int i = 0; i < fftSize && offset + i < inputAudio.size(); ++i)
            timeBuf[i] = inputAudio[offset + i];

        window.multiplyWithWindowingTable(timeBuf.data(), fftSize);
        fft.performRealOnlyForwardTransform(timeBuf.data());

        for (int b = 0; b < numBins; ++b)
        {
            float re = timeBuf[b * 2];
            float im = timeBuf[b * 2 + 1];
            stftReal[f * numBins + b] = re;
            stftImag[f * numBins + b] = im;
            mag[f * numBins + b] = std::sqrt(re * re + im * im);
        }
    }

    // 2. Median filtering
    std::vector<float> harmonicMag(numFrames * numBins, 0.0f);
    std::vector<float> percussiveMag(numFrames * numBins, 0.0f);

    // Harmonic: median filter across time for each frequency bin
    for (int b = 0; b < numBins; ++b)
    {
        medianFilter1D(&mag[b], &harmonicMag[b], numFrames, filterLengthTime, numBins);
    }

    // Percussive: median filter across frequency for each time frame
    for (int f = 0; f < numFrames; ++f)
    {
        medianFilter1D(&mag[f * numBins], &percussiveMag[f * numBins], numBins, filterLengthFreq, 1);
    }

    // 3. Masking
    std::vector<float> hMask(numFrames * numBins, 0.0f);
    std::vector<float> pMask(numFrames * numBins, 0.0f);
    const float eps = 1e-6f;

    for (size_t i = 0; i < mag.size(); ++i)
    {
        float h2 = harmonicMag[i] * harmonicMag[i];
        float p2 = percussiveMag[i] * percussiveMag[i];
        
        hMask[i] = h2 / (h2 + p2 + eps);
        pMask[i] = p2 / (h2 + p2 + eps);
    }

    // 4. Resynthesis (Inverse STFT)
    result.harmonic.resize(inputAudio.size(), 0.0f);
    result.percussive.resize(inputAudio.size(), 0.0f);
    std::vector<float> winCorr(inputAudio.size(), 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        std::vector<float> hSpec(fftSize * 2, 0.0f);
        std::vector<float> pSpec(fftSize * 2, 0.0f);

        for (int b = 0; b < numBins; ++b)
        {
            int idx = f * numBins + b;
            float re = stftReal[idx];
            float im = stftImag[idx];
            
            hSpec[b * 2] = re * hMask[idx];
            hSpec[b * 2 + 1] = im * hMask[idx];
            
            pSpec[b * 2] = re * pMask[idx];
            pSpec[b * 2 + 1] = im * pMask[idx];
        }

        fft.performRealOnlyInverseTransform(hSpec.data());
        fft.performRealOnlyInverseTransform(pSpec.data());

        window.multiplyWithWindowingTable(hSpec.data(), fftSize);
        window.multiplyWithWindowingTable(pSpec.data(), fftSize);

        int offset = f * hopSize;
        for (int i = 0; i < fftSize && offset + i < inputAudio.size(); ++i)
        {
            result.harmonic[offset + i] += hSpec[i];
            result.percussive[offset + i] += pSpec[i];
            winCorr[offset + i] += 1.0f;
        }
    }

    // Normalize
    for (size_t i = 0; i < inputAudio.size(); ++i)
    {
        if (winCorr[i] > 0.0f)
        {
            result.harmonic[i] /= winCorr[i];
            result.percussive[i] /= winCorr[i];
        }
    }

    return result;
}

} // namespace ana
