#include <catch2/catch_all.hpp>
#include "dsp/EnvelopeEditOps.h"

namespace
{
void makeAdsrLike(ana::MultiPointEnvelope& env)
{
    env.prepare(48000.0);
    env.addBreakpoint(0.0f, 0.0f);
    env.addBreakpoint(0.1f, 1.0f);
    env.addBreakpoint(0.3f, 0.5f);
    env.addBreakpoint(0.6f, 0.0f);
    env.setLoopMode(ana::LoopMode::Sustain);
    env.setLoopEnd(2);
}
}

TEST_CASE("EnvelopeEditOps: derived ADSR matches the drawn shape", "[envelope][editops]")
{
    ana::MultiPointEnvelope env;
    makeAdsrLike(env);

    const auto adsr = ana::EnvelopeEditOps::deriveADSR(env);
    REQUIRE(adsr.attack  == Catch::Approx(0.1f).margin(0.001f));
    REQUIRE(adsr.decay   == Catch::Approx(0.2f).margin(0.001f));
    REQUIRE(adsr.sustain == Catch::Approx(0.5f).margin(0.001f));
    REQUIRE(adsr.release == Catch::Approx(0.3f).margin(0.001f));

    SECTION("without a HOLD marker the last point is the sustain")
    {
        env.setLoopEnd(-1);
        const auto adsr2 = ana::EnvelopeEditOps::deriveADSR(env);
        REQUIRE(adsr2.sustain == Catch::Approx(0.0f));
        REQUIRE(adsr2.release == Catch::Approx(0.0f).margin(0.001f));
    }
}

TEST_CASE("EnvelopeEditOps: move is clamped between neighbours and 0..1", "[envelope][editops]")
{
    ana::MultiPointEnvelope env;
    makeAdsrLike(env);

    REQUIRE(ana::EnvelopeEditOps::movePoint(env, 1, -5.0f, 2.0f));
    REQUIRE(env.getBreakpoint(1).time  == Catch::Approx(0.0f).margin(0.001f));
    REQUIRE(env.getBreakpoint(1).value == Catch::Approx(1.0f));

    REQUIRE(ana::EnvelopeEditOps::movePoint(env, 1, 5.0f, -1.0f));
    REQUIRE(env.getBreakpoint(1).time  == Catch::Approx(0.3f).margin(0.001f));
    REQUIRE(env.getBreakpoint(1).value == Catch::Approx(0.0f));

    REQUIRE_FALSE(ana::EnvelopeEditOps::movePoint(env, 99, 1.0f, 1.0f));
}

TEST_CASE("EnvelopeEditOps: remove keeps at least two points", "[envelope][editops]")
{
    ana::MultiPointEnvelope env;
    makeAdsrLike(env);

    REQUIRE(ana::EnvelopeEditOps::removePoint(env, 1));
    REQUIRE(env.getNumBreakpoints() == 3);
    REQUIRE(ana::EnvelopeEditOps::removePoint(env, 0));
    REQUIRE(ana::EnvelopeEditOps::removePoint(env, 0));
    REQUIRE(env.getNumBreakpoints() == 2);
    REQUIRE_FALSE(ana::EnvelopeEditOps::removePoint(env, 0));
}

TEST_CASE("EnvelopeEditOps: addPoint respects the breakpoint cap", "[envelope][editops]")
{
    ana::MultiPointEnvelope env;
    makeAdsrLike(env);

    for (int i = 0; i < ana::MultiPointEnvelope::maxBreakpoints; ++i)
        ana::EnvelopeEditOps::addPoint(env, 9.0f, 0.5f, ana::CurveType::Linear);

    REQUIRE(env.getNumBreakpoints() == ana::MultiPointEnvelope::maxBreakpoints);
}

TEST_CASE("EnvelopeEditOps: coordinate mapping and hit testing", "[envelope][editops]")
{
    ana::MultiPointEnvelope env;
    env.addBreakpoint(0.0f, 0.0f);
    env.addBreakpoint(1.0f, 1.0f);

    const juce::Rectangle<float> area(10.0f, 20.0f, 200.0f, 100.0f);

    REQUIRE(ana::EnvelopeEditOps::valueToY(0.0f, area) == Catch::Approx(120.0f));
    REQUIRE(ana::EnvelopeEditOps::valueToY(1.0f, area) == Catch::Approx(20.0f));
    REQUIRE(ana::EnvelopeEditOps::yToValue(70.0f, area) == Catch::Approx(0.5f).margin(0.01f));
    REQUIRE(ana::EnvelopeEditOps::timeToX(env, 0.0f, area) == Catch::Approx(10.0f));

    const float xMid = ana::EnvelopeEditOps::timeToX(env, 0.5f, area);
    REQUIRE(ana::EnvelopeEditOps::xToTime(env, xMid, area) == Catch::Approx(0.5f).margin(0.01f));

    const juce::Point<float> onFirst(10.0f, 120.0f);
    REQUIRE(ana::EnvelopeEditOps::hitTestPoint(env, onFirst, area, 8.0f) == 0);
    REQUIRE(ana::EnvelopeEditOps::hitTestPoint(env, {150.0f, 60.0f}, area, 8.0f) == -1);
    REQUIRE(ana::EnvelopeEditOps::hitTestMarker(env, onFirst, area, 8.0f) == 0);
}

