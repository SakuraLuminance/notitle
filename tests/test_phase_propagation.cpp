#include <catch2/catch_all.hpp>
#include <juce_core/juce_core.h>
#include "dsp/PhasePropagation.h"
#include <cmath>

TEST_CASE("PhasePropagation - propagate empty data", "[phase]")
{
    ana::PhasePropagation propagator;
    ana::PartialData data;
    ana::STFTConfig config;

    propagator.propagatePhases(data, config);

    REQUIRE(data.frames.empty());
}

TEST_CASE("PhasePropagation - propagate single frame", "[phase]")
{
    ana::PhasePropagation propagator;
    ana::PartialData data;
    ana::STFTConfig config;

    data.sampleRate = 44100.0;
    data.hopSize = 512.0;

    ana::PartialFrame frame;
    frame.partials.push_back({440.0f, 0.5f, 0.0f});
    data.frames.push_back(frame);

    propagator.propagatePhases(data, config);

    // Single frame should not be modified
    REQUIRE(data.frames[0].partials[0].phase == Catch::Approx(0.0f));
}

TEST_CASE("PhasePropagation - phase continuity", "[phase]")
{
    ana::PhasePropagation propagator;
    ana::PartialData data;
    ana::STFTConfig config;

    data.sampleRate = 44100.0;
    data.hopSize = 512.0;

    // Create 10 frames with a 440Hz partial
    for (int i = 0; i < 10; ++i)
    {
        ana::PartialFrame frame;
        frame.partials.push_back({440.0f, 0.5f, 0.0f});
        data.frames.push_back(frame);
    }

    propagator.propagatePhases(data, config);

    // Check that phases are accumulated
    // Phase increment = 2π * 440 * 512 / 44100 ≈ 31.8 radians
    float expectedIncrement = 2.0f * juce::MathConstants<float>::pi * 440.0f * 512.0f / 44100.0f;

    for (int i = 1; i < 10; ++i)
    {
        float phaseDiff = data.frames[i].partials[0].phase - data.frames[i-1].partials[0].phase;
        // Phase should be wrapped to [-π, π]
        REQUIRE(std::abs(phaseDiff) <= juce::MathConstants<float>::pi);
    }
}

TEST_CASE("PhasePropagation - phase wrapping", "[phase]")
{
    ana::PhasePropagation propagator;
    ana::PartialData data;
    ana::STFTConfig config;

    data.sampleRate = 44100.0;
    data.hopSize = 512.0;

    // Create frames with a high frequency that will cause phase wrapping
    for (int i = 0; i < 100; ++i)
    {
        ana::PartialFrame frame;
        frame.partials.push_back({10000.0f, 0.5f, 0.0f});
        data.frames.push_back(frame);
    }

    propagator.propagatePhases(data, config);

    // All phases should be in [-π, π]
    for (const auto& frame : data.frames)
    {
        for (const auto& partial : frame.partials)
        {
            REQUIRE(partial.phase >= -juce::MathConstants<float>::pi);
            REQUIRE(partial.phase <= juce::MathConstants<float>::pi);
        }
    }
}
