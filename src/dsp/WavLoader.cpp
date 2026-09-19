#include "WavLoader.h"
#include <cmath>
#include <exception>

namespace ana {

namespace
{
    // Every size below is derived from bytes inside the file, so each one needs a
    // ceiling that comes from the plugin instead.  AudioBuffer's heap block throws
    // on a failed allocation and nothing on this path catches it, so an unchecked
    // header field is a way to terminate the host from a handful of bytes.
    constexpr double        kMinSampleRate = 1000.0;
    constexpr double        kMaxSampleRate = 768000.0;
    constexpr int           kMaxChannels   = 8;
    constexpr juce::int64   kMaxSamples    = 32ll * 1024ll * 1024ll;   // 32 M frames
}

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

    // JUCE's WAV reader takes lengthInSamples from the data chunk's declared size
    // and numChannels from the fmt chunk, and only requires both to be non-zero.
    // A 60-byte file declaring 0x8000 channels and a 512 MB data chunk therefore
    // reaches AudioBuffer with values that describe a 64 GB allocation.
    const juce::int64 declaredSamples  = reader->lengthInSamples;
    const juce::int64 declaredChannels = reader->numChannels;
    const double      declaredRate     = reader->sampleRate;

    if (declaredChannels < 1 || declaredChannels > kMaxChannels)
    {
        DBG("WavLoader: Refusing " << file.getFullPathName()
            << ": " << declaredChannels << " channels");
        return std::nullopt;
    }

    if (! (declaredRate >= kMinSampleRate && declaredRate <= kMaxSampleRate))
    {
        DBG("WavLoader: Refusing " << file.getFullPathName()
            << ": sample rate " << declaredRate);
        return std::nullopt;
    }

    if (declaredSamples <= 0 || declaredSamples > kMaxSamples)
    {
        DBG("WavLoader: Refusing " << file.getFullPathName()
            << ": " << declaredSamples << " samples");
        return std::nullopt;
    }

    // A real payload needs at least one byte per frame, so a declared length above
    // the file's own size is a header that is lying about what follows it.
    if (declaredSamples > file.getSize())
    {
        DBG("WavLoader: Refusing " << file.getFullPathName()
            << ": declares " << declaredSamples << " samples in "
            << file.getSize() << " bytes");
        return std::nullopt;
    }

    const int numSamples  = static_cast<int> (declaredSamples);
    const int numChannels = static_cast<int> (declaredChannels);

    try
    {
        AudioFileData data;
        data.sampleRate = declaredRate;
        data.numChannels = 1;  // always convert to mono
        data.durationSeconds = static_cast<double> (numSamples) / data.sampleRate;

        // Read all channels into temporary buffer
        juce::AudioBuffer<float> tempBuffer(numChannels, numSamples);
        reader->read(&tempBuffer, 0, numSamples, 0, true, true);

        // Convert to mono by averaging channels
        data.samples.resize(static_cast<size_t> (numSamples));
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                sum += tempBuffer.getSample(ch, i);
            }
            data.samples[static_cast<size_t> (i)] = sum / static_cast<float> (numChannels);
        }

        // Resample if loaded sample rate differs from target
        if (std::abs(data.sampleRate - targetSampleRate) > 0.001)
        {
            const double ratio = data.sampleRate / targetSampleRate;

            // The resampled length is the file's length scaled by the ratio of the
            // two rates: a file claiming 1 Hz grows by 44100x, and the double->int
            // conversion of a value above INT_MAX is undefined rather than large.
            const double scaled = std::ceil(static_cast<double> (numSamples) / ratio);
            if (! (scaled >= 1.0) || scaled > static_cast<double> (kMaxSamples))
            {
                DBG("WavLoader: Refusing to resample " << file.getFullPathName()
                    << " to " << scaled << " samples");
                return std::nullopt;
            }

            const int newNumSamples = static_cast<int> (scaled);

            juce::AudioBuffer<float> monoBuffer(1, numSamples);
            monoBuffer.copyFrom(0, 0, data.samples.data(), numSamples);

            juce::MemoryAudioSource source(monoBuffer, true, false);

            juce::ResamplingAudioSource resampler(&source, false, 1);
            resampler.prepareToPlay(newNumSamples, targetSampleRate);
            resampler.setResamplingRatio(ratio);

            juce::AudioBuffer<float> resampledBuffer(1, newNumSamples);
            juce::AudioSourceChannelInfo info(resampledBuffer);
            resampler.getNextAudioBlock(info);

            data.samples.assign(resampledBuffer.getReadPointer(0),
                                resampledBuffer.getReadPointer(0) + newNumSamples);
            data.sampleRate = targetSampleRate;
            data.durationSeconds = static_cast<double> (newNumSamples) / targetSampleRate;
        }

        return data;
    }
    catch (const std::exception& e)
    {
        // Out of memory for a header we accepted, or a decoder that threw: a failed
        // load is a message, an escaping exception is the host going down.
        DBG("WavLoader: " << file.getFullPathName() << " failed: " << e.what());
        return std::nullopt;
    }
}

} // namespace ana
