#include <catch2/catch_all.hpp>
#include "../src/dsp/SpectralFreezeEngine.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("SpectralFreezeEngine - initial state", "[freeze][init]")
{
    SpectralFreezeEngine freeze;
    freeze.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("SpectralFreezeEngine - basic setup", "[freeze][setup]")
{
    SpectralFreezeEngine freeze;
    freeze.setSampleRate(testSampleRate);
    freeze.setFftSize(2048);
    freeze.setFreezeMode(SpectralFreezeEngine::FreezeMode::Snapshot);
    freeze.setMix(0.5f);
    SUCCEED();
}

TEST_CASE("SpectralFreezeEngine - freeze trigger", "[freeze][trigger]")
{
    SpectralFreezeEngine freeze;
    freeze.setSampleRate(testSampleRate);
    
    PartialDataSIMD data;
    data.maxPartials = 10;
    
    freeze.triggerFreeze();
    freeze.process(data, 0);
    SUCCEED();
}

TEST_CASE("SpectralFreezeEngine - process audio", "[freeze][audio]")
{
    SpectralFreezeEngine freeze;
    freeze.setSampleRate(testSampleRate);
    
    juce::AudioBuffer<float> input(2, 512);
    juce::AudioBuffer<float> output(2, 512);
    
    input.clear();
    output.clear();
    
    freeze.processAudio(input, output);
    SUCCEED();
}
