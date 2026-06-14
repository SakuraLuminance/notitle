#include "WavLoader.h"
#include <cmath>

namespace ana {

WavLoader::WavLoader()
{
    formatManager.registerBasicFormats();
}

WavLoader::~WavLoader()
{
}

std::optional<AudioFileData> WavLoader::loadWav(const juce::File& file, double targetSampleRate)
{
    if (!file.existsAsFile())
    {
        DBG("WavLoader: File not found: " << file.getFullPathName());
        return std::nullopt;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
    {
        DBG("WavLoader: Could not read file: " << file.getFullPathName());
        return std::nullopt;
    }

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);

    if (numSamples <= 0)
    {
        DBG("WavLoader: Empty file: " << file.getFullPathName());
        return std::nullopt;
    }

    AudioFileData data;
    data.sampleRate = reader->sampleRate;
    data.numChannels = 1;  // always convert to mono
    data.durationSeconds = numSamples / data.sampleRate;

    // Read all channels into temporary buffer
    juce::AudioBuffer<float> tempBuffer(numChannels, numSamples);
    reader->read(&tempBuffer, 0, numSamples, 0, true, true);

    // Convert to mono by averaging channels
    data.samples.resize(numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            sum += tempBuffer.getSample(ch, i);
        }
        data.samples[i] = sum / numChannels;
    }

    // Resample if loaded sample rate differs from target
    if (std::abs(data.sampleRate - targetSampleRate) > 0.001)
    {
        juce::AudioBuffer<float> monoBuffer(1, numSamples);
        monoBuffer.copyFrom(0, 0, data.samples.data(), numSamples);

        juce::MemoryAudioSource source(monoBuffer, true, false);

        const double ratio = data.sampleRate / targetSampleRate;
        const int newNumSamples = static_cast<int>(
            std::ceil(static_cast<double>(numSamples) / ratio));

        juce::ResamplingAudioSource resampler(&source, false, 1);
        resampler.prepareToPlay(newNumSamples, targetSampleRate);
        resampler.setResamplingRatio(ratio);

        juce::AudioBuffer<float> resampledBuffer(1, newNumSamples);
        juce::AudioSourceChannelInfo info(resampledBuffer);
        resampler.getNextAudioBlock(info);

        data.samples.assign(resampledBuffer.getReadPointer(0),
                            resampledBuffer.getReadPointer(0) + newNumSamples);
        data.sampleRate = targetSampleRate;
        data.durationSeconds = static_cast<double>(newNumSamples) / targetSampleRate;
    }

    return data;
}

} // namespace ana
