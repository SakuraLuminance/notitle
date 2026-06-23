#include <catch2/catch_all.hpp>
#include "dsp/BlurEffect.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("BlurEffect - initial state", "[blur][init]")
{
    BlurEffect blur;
    blur.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("BlurEffect - parameters", "[blur][params]")
{
    BlurEffect blur;
    blur.setAttackBlur(100.0f);
    blur.setDecayBlur(200.0f);
    blur.setHarmonicBlur(0.8f);
    blur.setTopTension(0.7f);
    blur.setBottomTension(0.3f);
    blur.setMix(0.5f);
    SUCCEED();
}

TEST_CASE("BlurEffect - process", "[blur][process]")
{
    BlurEffect blur;
    blur.setSampleRate(testSampleRate);
    blur.setAttackBlur(50.0f);
    blur.setHarmonicBlur(0.5f);
    
    PartialDataSIMD data;
    data.maxPartials = 10;
    data.sampleRate = testSampleRate;
    
    // First frame
    blur.process(data);
    
    // Second frame to test temporal blur
    blur.process(data);
    SUCCEED();
}

TEST_CASE("BlurEffect - reset", "[blur][reset]")
{
    BlurEffect blur;
    blur.reset();
    SUCCEED();
}
