#include "PartialTracker.h"
#include "STFTAnalyzer.h"
#include "PeakDetector.h"
#include <cmath>

namespace ana {

PartialTracker::PartialTracker()
{
}

PartialTracker::~PartialTracker()
{
}

PartialData PartialTracker::trackPartials(
    const AudioFileData& audio,
    const STFTConfig& config)
{
    STFTAnalyzer analyzer;
    PeakDetector detector;

    auto frames = analyzer.analyze(audio, config);

    PartialData result;
    result.maxPartials = config.maxPartials;
    result.sampleRate  = audio.sampleRate;
    result.hopSize     = config.hopSize;

    const double frameDuration = config.hopSize / audio.sampleRate;

    for (size_t i = 0; i < frames.size(); ++i)
    {
        auto peaks = detector.detectPeaks(frames[i], config, audio.sampleRate);

        PartialFrame frame;
        frame.partials  = std::move(peaks);
        frame.timestamp = i * frameDuration;

        result.frames.push_back(std::move(frame));
    }

    return result;
}

} // namespace ana
