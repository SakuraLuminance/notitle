#include <catch2/catch_all.hpp>
#include "../src/dsp/VocoderMode.h"
#include <cmath>
#include <vector>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("VocoderMode - initial state", "[vocoder][init]")
{
    VocoderMode vocoder;
    vocoder.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("VocoderMode - parameters", "[vocoder][params]")
{
    VocoderMode vocoder;
    vocoder.setNumBands(16);
    vocoder.setMix(0.8f);
    vocoder.setFormantShift(2.0f);
    vocoder.setAttack(15.0f);
    vocoder.setRelease(100.0f);
    vocoder.setFftSize(1024);
    SUCCEED();
}

TEST_CASE("VocoderMode - setModulator", "[vocoder][modulator]")
{
    VocoderMode vocoder;
    std::vector<float> modAudio(1024, 0.5f);
    vocoder.setModulator(modAudio, testSampleRate);
    SUCCEED();
}

TEST_CASE("VocoderMode - process partials", "[vocoder][process]")
{
    VocoderMode vocoder;
    vocoder.setSampleRate(testSampleRate);
    
    std::vector<float> modAudio(1024, 0.5f);
    vocoder.setModulator(modAudio, testSampleRate);
    
    PartialDataSIMD partials;
    partials.frequency[0] = 440.0f;
    partials.amplitude[0] = 1.0f;
    partials.activeCount = 1;
    
    vocoder.process(partials);
    SUCCEED();
}

TEST_CASE("VocoderMode - process audio", "[vocoder][process]")
{
    VocoderMode vocoder;
    vocoder.setSampleRate(testSampleRate);
    
    juce::AudioBuffer<float> carrier(1, 1024);
    carrier.clear();
    juce::AudioBuffer<float> modulator(1, 1024);
    modulator.clear();
    
    SECTION("FilterBank mode")
    {
        vocoder.processAudio(carrier, modulator);
        SUCCEED();
    }
    
    SECTION("FFT mode")
    {
        vocoder.setFftSize(1024);
        vocoder.processAudio(carrier, modulator);
        SUCCEED();
    }
}

TEST_CASE("VocoderMode - reset", "[vocoder][reset]")
{
    VocoderMode vocoder;
    vocoder.reset();
    SUCCEED();
}
