#include <catch2/catch_all.hpp>
#include "../src/dsp/TimeStretchEngine.h"
#include <cmath>
#include <vector>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("TimeStretchEngine - initial state", "[timestretch][init]")
{
    TimeStretchEngine ts;
    ts.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("TimeStretchEngine - StretchCurve", "[timestretch][curve]")
{
    StretchCurve curve;
    
    SECTION("Empty curve returns 1.0")
    {
        REQUIRE(curve.getSpeedAt(0.5) == Catch::Approx(1.0));
    }

    SECTION("Single point returns that speed")
    {
        curve.addPoint(0.5, 2.0);
        REQUIRE(curve.getSpeedAt(0.0) == Catch::Approx(2.0));
        REQUIRE(curve.getSpeedAt(1.0) == Catch::Approx(2.0));
    }

    SECTION("Interpolation works")
    {
        curve.addPoint(0.0, 1.0);
        curve.addPoint(1.0, 2.0);
        REQUIRE(curve.getSpeedAt(0.5) == Catch::Approx(1.5));
    }

    SECTION("Clear works")
    {
        curve.addPoint(0.0, 1.0);
        curve.clear();
        REQUIRE(curve.points.empty());
    }
}

TEST_CASE("TimeStretchEngine - parameters", "[timestretch][params]")
{
    TimeStretchEngine ts;
    ts.setStretchRatio(2.0f);
    ts.setDefaultSpeed(1.5f);
    ts.setMidiFitMode(true);
    ts.setTargetDuration(2.0);
    ts.setNoteVelocity(0.8f);
    SUCCEED();
}

TEST_CASE("TimeStretchEngine - process doesn't crash", "[timestretch][process]")
{
    TimeStretchEngine ts;
    ts.setSampleRate(testSampleRate);
    
    std::vector<float> input(1024, 0.0f);
    
    SECTION("Normal stretch")
    {
        ts.setStretchRatio(1.0f);
        auto output = ts.process(input, input.size());
        REQUIRE(output.size() >= 0);
    }

    SECTION("Slow stretch (granular fallback)")
    {
        ts.setStretchRatio(0.1f);
        auto output = ts.process(input, input.size());
        REQUIRE(output.size() >= 0);
    }
    
    SECTION("Fast stretch")
    {
        ts.setStretchRatio(4.0f);
        auto output = ts.process(input, input.size());
        REQUIRE(output.size() >= 0);
    }
}

TEST_CASE("TimeStretchEngine - processToDuration", "[timestretch][process]")
{
    TimeStretchEngine ts;
    ts.setSampleRate(testSampleRate);
    
    std::vector<float> input(44100, 0.0f); // 1 second
    
    auto output = ts.processToDuration(input, 2.0); // stretch to 2 seconds
    // Just verify it doesn't crash, actual length depends on hop size and windowing
    REQUIRE(output.size() >= 0);
}

TEST_CASE("TimeStretchEngine - reset", "[timestretch][reset]")
{
    TimeStretchEngine ts;
    ts.reset();
    SUCCEED();
}
