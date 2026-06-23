#include <catch2/catch_all.hpp>
#include "dsp/Harmonizer.h"
#include <cmath>
#include <vector>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("Harmonizer - initial state", "[harmonizer][init]")
{
    Harmonizer harm;
    harm.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("Harmonizer - parameters", "[harmonizer][params]")
{
    Harmonizer harm;
    harm.setAmount(0.8f);
    harm.setWidth(0.5f);
    harm.setStrength(0.7f);
    harm.setShift(12.0f);
    harm.setGap(5.0f);
    harm.setShiftMode(true);
    SUCCEED();
}

TEST_CASE("Harmonizer - process", "[harmonizer][process]")
{
    Harmonizer harm;
    harm.setSampleRate(testSampleRate);
    harm.setAmount(1.0f);
    harm.setWidth(0.5f);
    harm.setShift(12.0f); // 1 octave
    harm.setShiftMode(true);
    
    PartialDataSIMD data;
    data.maxPartials = 512;
    data.activeCount = 1;
    data.frequency[0] = 440.0f;
    data.amplitude[0]  = 1.0f;
    data.activeMask[0] = 1; // Bit 0 active
    
    harm.process(data);
    
    REQUIRE(data.activeCount > 1); // Should have added clones
    // Due to implementation details, the new clone could be anywhere in the array,
    // but its frequency should be 880Hz.
    bool foundClone = false;
    for (int i = 1; i < data.maxPartials; ++i) {
        if (data.isActive(i) && std::abs(data.frequency[i] - 880.0f) < 1.0f) {
            foundClone = true;
            break;
        }
    }
    REQUIRE(foundClone);
}

TEST_CASE("Harmonizer - reset", "[harmonizer][reset]")
{
    Harmonizer harm;
    harm.reset();
    SUCCEED();
}
