#include <catch2/catch_all.hpp>
#include "dsp/NeuralUpsampler.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 22050.0;

TEST_CASE("NeuralUpsampler - initial state", "[neural][init]")
{
    NeuralUpsampler nu;
    SUCCEED();
}

TEST_CASE("NeuralUpsampler - config", "[neural][config]")
{
    NeuralUpsampler nu;
    nu.setQuality(4);
    nu.setTargetSampleRate(44100.0);
    nu.setBandwidth(0.95f);
    nu.setHarmonicEnhancement(0.5f);
    nu.setNoiseReduction(0.2f);
    SUCCEED();
}

TEST_CASE("NeuralUpsampler - audio process", "[neural][process]")
{
    NeuralUpsampler nu;
    nu.setTargetSampleRate(44100.0);
    
    std::vector<float> input(512, 0.5f);
    nu.setInput(input, testSampleRate, 16);
    
    auto output = nu.process();
    REQUIRE(output.size() > 0);
}

TEST_CASE("NeuralUpsampler - partial process", "[neural][process]")
{
    NeuralUpsampler nu;
    nu.setTargetSampleRate(44100.0);
    
    PartialDataSIMD data;
    data.frequency[0] = 440.0f;
    data.amplitude[0] = 1.0f;
    data.activeCount = 1;
    
    nu.process(data);
    SUCCEED();
}
