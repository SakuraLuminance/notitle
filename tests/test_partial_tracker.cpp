#include <catch2/catch_all.hpp>
#include "../src/dsp/PartialTracker.h"
#include <cmath>

TEST_CASE("PartialTracker - track silence", "[tracker]")
{
    ana::PartialTracker tracker;
    ana::AudioFileData audio;
    ana::STFTConfig config;

    // Create 1 second of silence
    audio.samples.resize(44100, 0.0f);
    audio.sampleRate = 44100.0;

    auto result = tracker.trackPartials(audio, config);

    REQUIRE_FALSE(result.frames.empty());
    REQUIRE(result.sampleRate == 44100.0);
    REQUIRE(result.hopSize == 512.0);

    // All frames should have empty partials
    for (const auto& frame : result.frames)
    {
        REQUIRE(frame.partials.empty());
    }
}

TEST_CASE("PartialTracker - frame timestamps", "[tracker]")
{
    ana::PartialTracker tracker;
    ana::AudioFileData audio;
    ana::STFTConfig config;

    audio.samples.resize(44100, 0.0f);
    audio.sampleRate = 44100.0;

    auto result = tracker.trackPartials(audio, config);

    // Check timestamps are correctly calculated
    double expectedDuration = config.hopSize / audio.sampleRate;
    for (size_t i = 0; i < result.frames.size(); ++i)
    {
        REQUIRE(result.frames[i].timestamp == Approx(i * expectedDuration));
    }
}

TEST_CASE("PartialTracker - max partials setting", "[tracker]")
{
    ana::PartialTracker tracker;
    ana::AudioFileData audio;
    ana::STFTConfig config;

    audio.samples.resize(44100, 0.0f);
    audio.sampleRate = 44100.0;

    auto result = tracker.trackPartials(audio, config);

    REQUIRE(result.maxPartials == 512);
}

TEST_CASE("PartialTracker - frame count matches STFT", "[tracker]")
{
    ana::PartialTracker tracker;
    ana::AudioFileData audio;
    ana::STFTConfig config;

    audio.samples.resize(44100, 0.0f);
    audio.sampleRate = 44100.0;

    auto result = tracker.trackPartials(audio, config);

    // Expected frames: (44100 - 2048) / 512 + 1
    int expectedFrames = (44100 - 2048) / 512 + 1;
    REQUIRE(static_cast<int>(result.frames.size()) == expectedFrames);
}
