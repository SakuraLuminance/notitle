#include <catch2/catch_all.hpp>
#include "dsp/SpectralFreezeEngine.h"
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

// The freeze must HOLD its captured spectrum: once frozen, feeding silence must
// keep producing sound (previously the audio path read a moving delay line, so
// the frozen sound drained away with the live input).
TEST_CASE("SpectralFreezeEngine - frozen audio holds while input goes silent", "[freeze][audio][hold]")
{
    SpectralFreezeEngine freeze;
    freeze.setSampleRate(testSampleRate);
    freeze.setFftSize(2048);
    freeze.setFreezeMode(SpectralFreezeEngine::FreezeMode::Snapshot);
    freeze.setMix(1.0f);
    freeze.setDryHP(20.0f);
    freeze.setWetLP(20000.0f);

    constexpr int block = 512;
    juce::AudioBuffer<float> in(1, block);
    juce::AudioBuffer<float> out(1, block);

    const double inc = juce::MathConstants<double>::twoPi * 440.0 / testSampleRate;
    double phase = 0.0;

    // Build up history with a tone.
    for (int blk = 0; blk < 16; ++blk)
    {
        for (int s = 0; s < block; ++s)
        {
            in.setSample(0, s, static_cast<float>(std::sin(phase)));
            phase += inc;
        }
        freeze.processAudio(in, out);
    }

    freeze.setFreeze(true);

    // The internal crossfade ramps over ~440 blocks; keep feeding silence.
    float frozenRms = 0.0f;
    for (int blk = 0; blk < 900; ++blk)
    {
        in.clear();
        freeze.processAudio(in, out);
        if (blk >= 890)
            frozenRms = out.getRMSLevel(0, 0, block);
    }

    INFO("frozen rms: " << frozenRms);
    REQUIRE(frozenRms > 0.05f);

    // Releasing the freeze must crossfade back to the (silent) live input.
    freeze.setFreeze(false);
    for (int blk = 0; blk < 900; ++blk)
    {
        in.clear();
        freeze.processAudio(in, out);
    }

    REQUIRE(out.getRMSLevel(0, 0, block) < 0.02f);
}
