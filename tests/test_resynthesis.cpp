#include <catch2/catch_all.hpp>
#include "../src/dsp/ResynthesisEngine.h"
#include <cmath>

TEST_CASE("ResynthesisEngine - resynthesize empty data", "[resynth]")
{
    ana::ResynthesisEngine engine;
    ana::PartialData data;
    ana::STFTConfig config;

    auto result = engine.resynthesize(data, config);

    REQUIRE(result.empty());
}

TEST_CASE("ResynthesisEngine - resynthesize single partial", "[resynth]")
{
    ana::ResynthesisEngine engine;
    ana::PartialData data;
    ana::STFTConfig config;

    // Create a single partial at 440Hz
    data.sampleRate = 44100.0;
    data.hopSize = 512.0;
    data.maxPartials = 512;

    // Create 10 frames with a 440Hz partial
    for (int i = 0; i < 10; ++i)
    {
        ana::PartialFrame frame;
        frame.timestamp = i * 512.0 / 44100.0;
        frame.partials.push_back({440.0f, 0.5f, 0.0f});
        data.frames.push_back(frame);
    }

    auto result = engine.resynthesize(data, config);

    REQUIRE_FALSE(result.empty());
    // Output should be normalized to [-1, 1]
    float maxVal = 0.0f;
    for (float sample : result)
    {
        maxVal = std::max(maxVal, std::abs(sample));
    }
    REQUIRE(maxVal <= 1.0f);
}

TEST_CASE("ResynthesisEngine - no NaN or Inf", "[resynth]")
{
    ana::ResynthesisEngine engine;
    ana::PartialData data;
    ana::STFTConfig config;

    data.sampleRate = 44100.0;
    data.hopSize = 512.0;

    ana::PartialFrame frame;
    frame.partials.push_back({440.0f, 1.0f, 0.0f});
    data.frames.push_back(frame);

    auto result = engine.resynthesize(data, config);

    for (float sample : result)
    {
        REQUIRE_FALSE(std::isnan(sample));
        REQUIRE_FALSE(std::isinf(sample));
    }
}

TEST_CASE("ResynthesisEngine - output length", "[resynth]")
{
    ana::ResynthesisEngine engine;
    ana::PartialData data;
    ana::STFTConfig config;

    data.sampleRate = 44100.0;
    data.hopSize = 512.0;

    // 5 frames
    for (int i = 0; i < 5; ++i)
    {
        ana::PartialFrame frame;
        frame.partials.push_back({440.0f, 0.5f, 0.0f});
        data.frames.push_back(frame);
    }

    auto result = engine.resynthesize(data, config);

    // Expected length: (5-1) * 512 + 2048 = 4096
    int expectedLength = 4 * 512 + 2048;
    REQUIRE(static_cast<int>(result.size()) == expectedLength);
}
