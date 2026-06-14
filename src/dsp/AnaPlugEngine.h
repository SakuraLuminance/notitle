#pragma once
#include <atomic>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include "AudioFileData.h"
#include "PartialData.h"
#include "STFTConfig.h"

namespace ana {

class AnaPlugEngine
{
public:
    AnaPlugEngine();
    ~AnaPlugEngine();

    bool loadSample(const juce::File& file);
    bool analyze(const STFTConfig& config = STFTConfig{});
    std::vector<float> resynthesize();

    const PartialData& getPartialData() const;
    const AudioFileData& getAudioData() const;

    bool isLoaded() const;
    bool isAnalyzed() const;

    // Root note support
    void setRootNote(int midiNote);         // 0-127, default 60 (C4)
    int getRootNote() const;
    void setRootFineTune(float cents);      // -50 to +50
    float getRootFineTune() const;
    float getCurrentRootFrequency() const;  // computed from rootNote + fineTune

    // Pitch flatten result
    void setFlattenedAudio(const std::vector<float>& audio, double sampleRate);
    const std::vector<float>& getFlattenedAudio() const;
    bool hasFlattenedAudio() const;

private:
    AudioFileData audioData;
    PartialData partialData;
    std::atomic<bool> loaded{ false };
    std::atomic<bool> analyzed{ false };

    int rootNote_ = 60;
    float rootFineTune_ = 0.0f;
    std::vector<float> flattenedAudio_;
    bool hasFlattenedAudio_ = false;
};

} // namespace ana
