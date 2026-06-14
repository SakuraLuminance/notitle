#include <catch2/catch_all.hpp>
#include "../src/dsp/SpectralReverb.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("SpectralReverb - initial state", "[reverb][init]")
{
    SpectralReverb reverb;
    reverb.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("SpectralReverb - preset", "[reverb][preset]")
{
    SpectralReverb reverb;
    reverb.setSampleRate(testSampleRate);
    reverb.loadPreset(SpectralReverb::Preset::Hall);
    SUCCEED();
}

TEST_CASE("SpectralReverb - process", "[reverb][process]")
{
    SpectralReverb reverb;
    reverb.setSampleRate(testSampleRate);
    reverb.loadPreset(SpectralReverb::Preset::Room);
    
    PartialDataSIMD data;
    data.frequency[0] = 440.0f;
    data.amplitude[0] = 1.0f;
    data.activeCount = 1;
    
    reverb.process(data);
    SUCCEED();
}
