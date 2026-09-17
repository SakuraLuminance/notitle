#include <catch2/catch_all.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/EnvelopeCanvas.h"
#include "dsp/EnvelopeEditOps.h"

namespace
{
void makeEnv(ana::MultiPointEnvelope& env)
{
    env.prepare(48000.0);
    env.addBreakpoint(0.0f, 0.0f);
    env.addBreakpoint(0.2f, 1.0f);
    env.addBreakpoint(0.5f, 0.4f);
    env.addBreakpoint(1.0f, 0.0f);
    env.setLoopMode(ana::LoopMode::Sustain);
    env.setLoopEnd(2);
}
}

TEST_CASE("EnvelopeCanvas: paint is headless safe", "[ui][envelope]")
{
    ana::MultiPointEnvelope env;
    makeEnv(env);

    ana::EnvelopeCanvas canvas;
    canvas.setBounds(0, 0, 400, 200);
    canvas.setEnvelope(&env);
    canvas.setPlayhead(0.3, 0.8);

    juce::Image img(juce::Image::ARGB, 400, 200, true);
    juce::Graphics g(img);

    REQUIRE_NOTHROW(canvas.paint(g));

    SECTION("sync + all loop modes paint")
    {
        env.setLoopMode(ana::LoopMode::PingPong);
        env.setSyncMode(true);
        env.setTempo(140.0);
        REQUIRE_NOTHROW(canvas.paint(g));

        env.setLoopMode(ana::LoopMode::Forward);
        REQUIRE_NOTHROW(canvas.paint(g));

        env.setLoopMode(ana::LoopMode::None);
        REQUIRE_NOTHROW(canvas.paint(g));
    }

    SECTION("empty envelope and null envelope paint")
    {
        ana::MultiPointEnvelope empty;
        canvas.setEnvelope(&empty);
        REQUIRE_NOTHROW(canvas.paint(g));

        canvas.setEnvelope(nullptr);
        REQUIRE_NOTHROW(canvas.paint(g));
    }

    SECTION("tiny bounds do not crash")
    {
        canvas.setBounds(0, 0, 1, 1);
        REQUIRE_NOTHROW(canvas.paint(g));
    }
}

TEST_CASE("EnvelopeCanvas: programmatic edits stay coherent", "[ui][envelope]")
{
    ana::MultiPointEnvelope env;
    makeEnv(env);

    ana::EnvelopeCanvas canvas;
    canvas.setBounds(0, 0, 400, 200);
    canvas.setEnvelope(&env);

    int editCount = 0;
    canvas.onEdited = [&editCount] { ++editCount; };

    // Add -> move -> remove through the same ops the mouse handlers use.
    REQUIRE(ana::EnvelopeEditOps::addPoint(env, 0.7f, 0.8f, ana::CurveType::SCurve));
    REQUIRE(env.getNumBreakpoints() == 5);

    const auto plot = juce::Rectangle<float>(10.0f, 12.0f, 380.0f, 176.0f);
    const float x = ana::EnvelopeEditOps::timeToX(env, 0.7f, plot);
    const float y = ana::EnvelopeEditOps::valueToY(0.3f, plot);
    REQUIRE(ana::EnvelopeEditOps::movePoint(env, 3, ana::EnvelopeEditOps::xToTime(env, x, plot),
                                            ana::EnvelopeEditOps::yToValue(y, plot)));
    REQUIRE(env.getBreakpoint(3).value == Catch::Approx(0.3f).margin(0.02f));

    const auto adsr = ana::EnvelopeEditOps::deriveADSR(env);
    REQUIRE(adsr.sustain >= 0.0f);
    REQUIRE(adsr.release >= 0.0f);

    juce::Image img(juce::Image::ARGB, 400, 200, true);
    juce::Graphics g(img);
    REQUIRE_NOTHROW(canvas.paint(g));
    REQUIRE(editCount == 0); // direct ops bypass the canvas callback; no spurious edits
}
