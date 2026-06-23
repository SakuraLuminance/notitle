#include <catch2/catch_all.hpp>
#include "dsp/DualTimbre.h"
#include <cmath>
#include <vector>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("TimbrePart - basics", "[dual][part]")
{
    TimbrePart tp;
    tp.clear();
    REQUIRE(tp.activeCount == 0);
    REQUIRE(!tp.isActive(0));

    tp.amplitude[0] = 1.0f;
    tp.activeCount = 1;
    REQUIRE(tp.isActive(0));
}

TEST_CASE("DualTimbre - initial state", "[dual][init]")
{
    DualTimbre dt;
    SUCCEED();
}

TEST_CASE("DualTimbre - process modes", "[dual][process]")
{
    DualTimbre dt;
    
    TimbrePart t1;
    t1.activeCount = 2;
    t1.frequency[0] = 440.0f; t1.amplitude[0] = 1.0f;
    t1.frequency[1] = 880.0f; t1.amplitude[1] = 0.5f;

    TimbrePart t2;
    t2.activeCount = 2;
    t2.frequency[0] = 440.0f; t2.amplitude[0] = 0.0f;
    t2.frequency[1] = 880.0f; t2.amplitude[1] = 1.0f;

    dt.setTimbre1(t1);
    dt.setTimbre2(t2);
    dt.setMix(0.5f);

    TimbrePart output;
    
    auto modes = {
        TimbreBlendMode::Fade,
        TimbreBlendMode::Subtract,
        TimbreBlendMode::Multiply,
        TimbreBlendMode::Maximum,
        TimbreBlendMode::Minimum,
        TimbreBlendMode::Pluck
    };

    for (auto mode : modes)
    {
        dt.setMode(mode);
        dt.process(output);
        REQUIRE(output.activeCount == 2);
    }
}

TEST_CASE("DualTimbre - static blend", "[dual][static]")
{
    float a1[4] = {1.0f, 0.8f, 0.5f, 0.0f};
    float a2[4] = {0.0f, 0.2f, 0.5f, 1.0f};
    float out[4] = {0.0f};

    DualTimbre::blend(a1, a2, out, 4, 0.5f, TimbreBlendMode::Fade);
    
    REQUIRE(out[0] == Catch::Approx(0.5f));
    REQUIRE(out[1] == Catch::Approx(0.5f));
    REQUIRE(out[2] == Catch::Approx(0.5f));
    REQUIRE(out[3] == Catch::Approx(0.5f));
}
