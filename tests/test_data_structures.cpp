#include <catch2/catch_all.hpp>
#include "dsp/PartialData.h"
#include "dsp/STFTConfig.h"
#include "dsp/AudioFileData.h"

TEST_CASE("PartialData struct construction", "[data]")
{
    ana::Partial partial;
    REQUIRE(partial.frequency == 0.0f);
    REQUIRE(partial.amplitude == 0.0f);
    REQUIRE(partial.phase == 0.0f);

    partial.frequency = 440.0f;
    partial.amplitude = 0.5f;
    partial.phase = 1.57f;
    REQUIRE(partial.frequency == 440.0f);
    REQUIRE(partial.amplitude == 0.5f);
    REQUIRE(partial.phase == Approx(1.57f));
}

TEST_CASE("PartialFrame struct construction", "[data]")
{
    ana::PartialFrame frame;
    REQUIRE(frame.partials.empty());
    REQUIRE(frame.timestamp == 0.0);

    frame.timestamp = 0.5;
    frame.partials.push_back({440.0f, 0.8f, 0.0f});
    REQUIRE(frame.timestamp == 0.5);
    REQUIRE(frame.partials.size() == 1);
}

TEST_CASE("PartialData struct construction", "[data]")
{
    ana::PartialData data;
    REQUIRE(data.frames.empty());
    REQUIRE(data.maxPartials == 512);
    REQUIRE(data.sampleRate == 44100.0);
    REQUIRE(data.hopSize == 512.0);
}

TEST_CASE("STFTConfig defaults", "[data]")
{
    ana::STFTConfig config;
    REQUIRE(config.fftSize == 2048);
    REQUIRE(config.hopSize == 512);
    REQUIRE(config.windowType == ana::STFTConfig::WindowType::Hann);
    REQUIRE(config.peakThresholdDB == -60.0f);
    REQUIRE(config.maxPartials == 512);
}

TEST_CASE("AudioFileData struct construction", "[data]")
{
    ana::AudioFileData data;
    REQUIRE(data.samples.empty());
    REQUIRE(data.sampleRate == 44100.0);
    REQUIRE(data.numChannels == 1);
    REQUIRE(data.durationSeconds == 0.0);
}
