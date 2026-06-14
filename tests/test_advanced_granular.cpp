#include <catch2/catch_all.hpp>
#include "../src/dsp/AdvancedGranularEngine.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("AdvancedGranularEngine - initial state", "[granular][init]")
{
    AdvancedGranularEngine eng;
    SUCCEED();
}

TEST_CASE("AdvancedGranularEngine - config", "[granular][config]")
{
    AdvancedGranularEngine eng;
    eng.setNumGrains(100);
    eng.setDensity(20.0f);
    eng.setChaos(0.5f);
    eng.setPitchRandom(0.1f);
    eng.setPanRandom(0.2f);
    eng.setAmpRandom(0.1f);
    eng.setReverseProb(0.1f);
    eng.setStutterProb(0.1f);
    SUCCEED();
}

TEST_CASE("AdvancedGranularEngine - cloud generation", "[granular][cloud]")
{
    AdvancedGranularEngine eng;
    GrainCloud cloud;
    cloud.generateRandom(50, 1.0f, 0.2f);
    REQUIRE(cloud.grains.size() == 50);
    
    eng.setGrainCloud(cloud);
    SUCCEED();
}

TEST_CASE("AdvancedGranularEngine - process", "[granular][process]")
{
    AdvancedGranularEngine eng;
    std::vector<float> source(44100, 0.5f);
    eng.setSourceBuffer(source, testSampleRate);
    
    juce::AudioBuffer<float> output(2, 512);
    output.clear();
    
    eng.process(output);
    SUCCEED();
}
