#pragma once
#include <optional>
#include <juce_audio_formats/juce_audio_formats.h>
#include "AudioFileData.h"

namespace ana {

class WavLoader
{
public:
    WavLoader();
    ~WavLoader();

    std::optional<AudioFileData> loadWav(const juce::File& file, double targetSampleRate = 44100.0);

private:
    juce::AudioFormatManager formatManager;
};

} // namespace ana
