#include <catch2/catch_all.hpp>
#include "../src/dsp/SubHarmonicGenerator.h"
#include <cmath>
#include <vector>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("SubHarmonicGenerator - initial state", "[sub][init]")
{
    SubHarmonicGenerator shg;
    shg.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("SubHarmonicGenerator - configuration", "[sub][config]")
{
    SubHarmonicGenerator shg;
    
    shg.setMode(SubHarmonicMode::Around);
    REQUIRE(shg.getConfig().mode == SubHarmonicMode::Around);

    shg.setSubLevel(0, 0.5f);
    REQUIRE(shg.getConfig().level1 == Catch::Approx(0.5f));

    SubHarmonicConfig cfg;
    cfg.mode = SubHarmonicMode::Below;
    cfg.level1 = 1.0f;
    cfg.level2 = 0.5f;
    cfg.level3 = 0.25f;
    shg.setConfig(cfg);
    
    REQUIRE(shg.getConfig().mode == SubHarmonicMode::Below);
    REQUIRE(shg.getConfig().level1 == Catch::Approx(1.0f));
}

TEST_CASE("SubHarmonicGenerator - generate Below", "[sub][generate]")
{
    SubHarmonicGenerator shg;
    shg.setSampleRate(testSampleRate);
    shg.setMode(SubHarmonicMode::Below);
    shg.setSubLevel(0, 1.0f);
    shg.setSubLevel(1, 1.0f);
    shg.setSubLevel(2, 1.0f);

    float freqs[3] = {0.0f};
    float amps[3]  = {0.0f};
    
    int numGen = shg.generate(440.0f, freqs, amps, 3);
    
    REQUIRE(numGen == 3);
    REQUIRE(freqs[0] == Catch::Approx(220.0f)); // 1 oct down
    REQUIRE(freqs[1] == Catch::Approx(110.0f)); // 2 oct down
    REQUIRE(freqs[2] == Catch::Approx(55.0f));  // 3 oct down
}

TEST_CASE("SubHarmonicGenerator - processInPlace", "[sub][process]")
{
    SubHarmonicGenerator shg;
    shg.setSampleRate(testSampleRate);
    shg.setMode(SubHarmonicMode::Below);
    shg.setSubLevel(0, 1.0f);
    
    float freqs[10] = {440.0f};
    float amps[10]  = {1.0f};
    int count = 1;

    shg.processInPlace(freqs, amps, &count, 440.0f);
    
    REQUIRE(count == 2);
    REQUIRE(freqs[1] == Catch::Approx(220.0f));
}
