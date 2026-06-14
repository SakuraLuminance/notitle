#include "AnaPlugEngine.h"
#include "WavLoader.h"
#include "PartialTracker.h"
#include "ResynthesisEngine.h"
#include "PhasePropagation.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace ana {

AnaPlugEngine::AnaPlugEngine()
{
}

AnaPlugEngine::~AnaPlugEngine()
{
}

bool AnaPlugEngine::loadSample(const juce::File& file)
{
    WavLoader loader;
    auto result = loader.loadWav(file);

    if (!result.has_value())
    {
        DBG("AnaPlugEngine: Failed to load file: " << file.getFullPathName());
        loaded = false;
        analyzed = false;
        return false;
    }

    audioData = std::move(*result);
    loaded = true;
    analyzed = false;

    DBG("AnaPlugEngine: Loaded " << audioData.durationSeconds << "s at "
        << audioData.sampleRate << "Hz");

    return true;
}

bool AnaPlugEngine::analyze(const STFTConfig& config)
{
    if (!loaded)
    {
        DBG("AnaPlugEngine: Cannot analyze - no file loaded");
        return false;
    }

    PartialTracker tracker;
    partialData = tracker.trackPartials(audioData, config);

    PhasePropagation propagator;
    propagator.propagatePhases(partialData, config);

    analyzed = true;

    DBG("AnaPlugEngine: Analyzed " << partialData.frames.size() << " frames");

    return true;
}

std::vector<float> AnaPlugEngine::resynthesize()
{
    if (!analyzed)
    {
        DBG("AnaPlugEngine: Cannot resynthesize - not analyzed");
        return {};
    }

    STFTConfig config;
    config.maxPartials = partialData.maxPartials;
    config.hopSize = static_cast<int>(partialData.hopSize);

    ResynthesisEngine engine;
    return engine.resynthesize(partialData, config);
}

const PartialData& AnaPlugEngine::getPartialData() const
{
    return partialData;
}

const AudioFileData& AnaPlugEngine::getAudioData() const
{
    return audioData;
}

bool AnaPlugEngine::isLoaded() const
{
    return loaded;
}

bool AnaPlugEngine::isAnalyzed() const
{
    return analyzed;
}

//==============================================================================
// Root note support
//==============================================================================

void AnaPlugEngine::setRootNote(int midiNote)
{
    rootNote_ = juce::jlimit(0, 127, midiNote);
}

int AnaPlugEngine::getRootNote() const
{
    return rootNote_;
}

void AnaPlugEngine::setRootFineTune(float cents)
{
    rootFineTune_ = juce::jlimit(-50.0f, 50.0f, cents);
}

float AnaPlugEngine::getRootFineTune() const
{
    return rootFineTune_;
}

float AnaPlugEngine::getCurrentRootFrequency() const
{
    // MIDI note 69 = A4 = 440 Hz
    return 440.0f * std::pow(2.0f, (rootNote_ - 69.0f + rootFineTune_ / 100.0f) / 12.0f);
}

//==============================================================================
// Pitch flatten result
//==============================================================================

void AnaPlugEngine::setFlattenedAudio(const std::vector<float>& audio, double sampleRate)
{
    flattenedAudio_ = audio;
    hasFlattenedAudio_ = true;
    juce::ignoreUnused(sampleRate);
}

const std::vector<float>& AnaPlugEngine::getFlattenedAudio() const
{
    return flattenedAudio_;
}

bool AnaPlugEngine::hasFlattenedAudio() const
{
    return hasFlattenedAudio_;
}

} // namespace ana
