#include <catch2/catch_all.hpp>
#include "../src/dsp/SpectralDynamics.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("SpectralDynamics - initial state", "[dynamics][init]")
{
    SpectralDynamics dyn;
    SUCCEED();
}

TEST_CASE("SpectralDynamics - config", "[dynamics][config]")
{
    SpectralDynamics dyn;
    dyn.setMode(SpectralDynamics::Mode::Compressor);
    dyn.setDetection(SpectralDynamics::Detection::Envelope);
    dyn.setThreshold(-20.0f);
    dyn.setRatio(4.0f);
    dyn.setAttack(10.0f);
    dyn.setRelease(50.0f);
    SUCCEED();
}

TEST_CASE("SpectralDynamics - process", "[dynamics][process]")
{
    SpectralDynamics dyn;
    dyn.setMode(SpectralDynamics::Mode::Compressor);
    dyn.setThreshold(-40.0f);
    dyn.setRatio(4.0f);
    dyn.setAttack(1.0f);
    dyn.setRelease(10.0f);
    
    PartialDataSIMD data;
    data.frequency[0] = 440.0f;
    data.amplitude[0] = 1.0f; // 0 dB, should compress
    data.activeCount = 1;
    
    dyn.process(data);
    SUCCEED();
}
