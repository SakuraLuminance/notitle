#include <catch2/catch_all.hpp>
#include "../src/dsp/SpectralSequencer.h"
#include <cmath>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("SpectralSequencer - initial state", "[sequencer][init]")
{
    SpectralSequencer seq;
    seq.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("SpectralSequencer - setup", "[sequencer][setup]")
{
    SpectralSequencer seq;
    seq.setSampleRate(testSampleRate);
    seq.setBpm(120.0f);
    seq.setRunning(true);
    seq.setSyncMode(true);
    SUCCEED();
}

TEST_CASE("SpectralSequencer - sequence", "[sequencer][sequence]")
{
    SpectralSequencer seq;
    seq.setSampleRate(testSampleRate);
    
    SpectralSequencer::Step step;
    step.active = true;
    step.frequencyMultiplier = 1.5f;
    step.amplitudeMultiplier = 0.8f;
    
    seq.setStep(0, step);
    
    PartialDataSIMD data;
    data.frequency[0] = 440.0f;
    data.amplitude[0] = 1.0f;
    data.activeCount = 1;
    
    seq.process(data, 512); // advance 512 samples
    SUCCEED();
}
