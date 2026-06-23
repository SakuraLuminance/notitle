#include <catch2/catch_all.hpp>
#include "dsp/FrequencyShaper.h"
#include <cmath>
#include <vector>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("FrequencyShaper - initial state", "[shaper][init]")
{
    FrequencyShaper fs;
    SUCCEED();
}

TEST_CASE("FrequencyShaper - parameters", "[shaper][params]")
{
    FrequencyShaper fs;
    fs.setType(FrequencyShaper::ShaperType::Saturate);
    fs.setAmount(0.8f);
    fs.setThreshold(5000.0f);
    fs.setFoldBoundary(8000.0f);
    fs.setQuantization(24.0f);
    fs.setCenterFrequency(2000.0f);
    fs.setResonance(5.0f);
    fs.setBandwidth(2.0f);
    fs.setFormantShift(5.0f);
    fs.setFormantAmount(0.6f);
    fs.setHarmonicOrder(3);
    fs.setHarmonicMix(0.4f);
    fs.setShiftAmount(100.0f);
    fs.setPhaseWarp(0.5f);
    fs.setPhaseModFreq(1.0f);
    SUCCEED();
}

TEST_CASE("FrequencyShaper - process partials", "[shaper][process]")
{
    FrequencyShaper fs;
    
    PartialDataSIMD partials;
    partials.activeCount = 10;
    for (int i = 0; i < 10; ++i) {
        partials.frequency[i] = 440.0f * (i + 1);
        partials.amplitude[i] = 1.0f;
    }

    auto types = {
        FrequencyShaper::ShaperType::Saturate,
        FrequencyShaper::ShaperType::Fold,
        FrequencyShaper::ShaperType::Bitcrush,
        FrequencyShaper::ShaperType::Resonant,
        FrequencyShaper::ShaperType::FormantShift,
        FrequencyShaper::ShaperType::HarmonicExciter,
        FrequencyShaper::ShaperType::FrequencyShift,
        FrequencyShaper::ShaperType::PhaseDistortion
    };

    for (auto type : types)
    {
        fs.setType(type);
        // Process a copy to not accumulate changes across tests
        PartialDataSIMD pCopy = partials;
        fs.process(pCopy);
        SUCCEED();
    }
}

TEST_CASE("FrequencyShaper - processAudio", "[shaper][process]")
{
    FrequencyShaper fs;
    juce::AudioBuffer<float> buffer(1, 1024);
    buffer.clear();

    fs.setType(FrequencyShaper::ShaperType::FrequencyShift);
    fs.setShiftAmount(100.0f);
    fs.processAudio(buffer, testSampleRate);
    SUCCEED();
}

TEST_CASE("FrequencyShaper - reset", "[shaper][reset]")
{
    FrequencyShaper fs;
    fs.reset();
    SUCCEED();
}
