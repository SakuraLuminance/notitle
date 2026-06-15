#include "WavetableEngine.h"
#include "WavLoader.h"
#include "STFTAnalyzer.h"
#include "PeakDetector.h"
#include "SpectralMorpher.h"
#include <algorithm>

namespace ana {

//==============================================================================
// Internal: build partial frames from raw audio via STFT + peak detection
//==============================================================================
namespace {

std::vector<PartialDataSIMD> buildFramesFromAudio(
    const std::vector<float>& samples,
    double sampleRate)
{
    // Build AudioFileData for STFTAnalyzer
    AudioFileData audio;
    audio.samples     = samples;
    audio.sampleRate  = sampleRate;
    audio.numChannels = 1;

    // STFT configuration
    STFTConfig config;
    config.fftSize = 2048;
    config.hopSize = 512;

    // Analyse
    STFTAnalyzer analyzer;
    auto spectrumFrames = analyzer.analyze(audio, config);
    if (spectrumFrames.empty())
        return {};

    // Peak detection per frame → PartialDataSIMD
    PeakDetector peakDetector;
    std::vector<PartialDataSIMD> frames;
    frames.reserve(spectrumFrames.size());

    for (const auto& spectrum : spectrumFrames)
    {
        auto peaks = peakDetector.detectPeaks(spectrum, config, sampleRate);

        PartialDataSIMD simd;
        simd.sampleRate = sampleRate;
        simd.hopSize    = config.hopSize;

        const int count = std::min(static_cast<int>(peaks.size()),
                                   static_cast<int>(simd.maxPartials));
        for (int i = 0; i < count; ++i)
        {
            simd.frequency[i] = peaks[i].frequency;
            simd.amplitude[i] = peaks[i].amplitude;
            simd.phase[i]     = peaks[i].phase;
        }

        simd.updateActiveMask();
        frames.push_back(std::move(simd));
    }

    return frames;
}

} // namespace

//==============================================================================
bool WavetableEngine::loadWavetable(const juce::File& file)
{
    // 1. Load audio file
    WavLoader loader;
    auto audioOpt = loader.loadWav(file);
    if (!audioOpt.has_value())
        return false;

    const auto& audio = audioOpt.value();
    return loadWavetable(audio.samples, audio.sampleRate);
}

bool WavetableEngine::loadWavetable(const std::vector<float>& audio, double sampleRate)
{
    auto frames = buildFramesFromAudio(audio, sampleRate);
    if (frames.empty())
        return false;

    frames_ = std::move(frames);
    position_.store(0.0f);
    loaded_ = true;
    return true;
}

bool WavetableEngine::loadFromPartials(const std::vector<PartialDataSIMD>& frames)
{
    if (frames.empty())
    {
        clear();
        return false;
    }

    frames_ = frames;
    position_.store(0.0f);
    loaded_ = true;
    return true;
}

//==============================================================================
void WavetableEngine::setPosition(float pos)
{
    position_.store(std::max(0.0f, std::min(1.0f, pos)));
}

float WavetableEngine::getPosition() const
{
    return position_.load();
}

//==============================================================================
PartialDataSIMD WavetableEngine::getCurrentFrame() const
{
    // Return empty frame if no data loaded
    if (frames_.empty())
        return PartialDataSIMD{};

    if (frames_.size() == 1)
        return frames_[0];

    // Map position [0, 1] to continuous frame index
    const float pos = position_.load();
    const float maxIdx = static_cast<float>(static_cast<int>(frames_.size()) - 1);
    const float frameIdx = pos * maxIdx;
    const int idxA = static_cast<int>(frameIdx);
    const int idxB = std::min(idxA + 1, static_cast<int>(frames_.size()) - 1);
    const float frac = frameIdx - static_cast<float>(idxA);

    // Use SIMD-optimized linear morph via SpectralMorpher
    PartialDataSIMD result;
    SpectralMorpher::morphLinear(result, frames_[idxA], frames_[idxB], frac);
    return result;
}

PartialDataSIMD WavetableEngine::getFrame(int index) const
{
    if (index < 0 || index >= static_cast<int>(frames_.size()))
        return PartialDataSIMD{};

    return frames_[static_cast<size_t>(index)];
}

//==============================================================================
int WavetableEngine::getNumFrames() const
{
    return static_cast<int>(frames_.size());
}

bool WavetableEngine::isLoaded() const
{
    return loaded_;
}

void WavetableEngine::clear()
{
    frames_.clear();
    frames_.shrink_to_fit();
    position_.store(0.0f);
    loaded_ = false;
}

} // namespace ana
