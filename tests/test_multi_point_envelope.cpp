#include <catch2/catch_all.hpp>
#include <cmath>
#include "dsp/MultiPointEnvelope.h"

using namespace ana;

//==============================================================================
// Helper: process envelope through full duration at 48kHz
static float processFull(MultiPointEnvelope& env, double sampleRate = 48000.0)
{
    env.prepare(sampleRate);
    env.trigger();

    // Process 10 seconds worth of samples
    const int totalSamples = static_cast<int>(sampleRate * 10.0);
    float lastVal = 0.0f;

    for (int i = 0; i < totalSamples && env.isActive(); ++i)
        lastVal = env.process(1);

    return lastVal;
}

// Helper: advance N samples and return value
static float advance(MultiPointEnvelope& env, int numSamples)
{
    return env.process(numSamples);
}

//==============================================================================
TEST_CASE("MultiPointEnvelope: breakpoint management", "[envelope][breakpoints]")
{
    MultiPointEnvelope env;
    env.prepare(44100.0);

    SECTION("starts empty")
    {
        REQUIRE(env.getNumBreakpoints() == 0);
    }

    SECTION("adds breakpoints sorted by time")
    {
        REQUIRE(env.addBreakpoint(1.0f, 0.5f, CurveType::Linear));
        REQUIRE(env.addBreakpoint(0.0f, 0.0f, CurveType::Linear));
        REQUIRE(env.addBreakpoint(0.5f, 0.8f, CurveType::Exponential));

        REQUIRE(env.getNumBreakpoints() == 3);
        REQUIRE(env.getBreakpoint(0).time == 0.0f);
        REQUIRE(env.getBreakpoint(1).time == 0.5f);
        REQUIRE(env.getBreakpoint(2).time == 1.0f);
    }

    SECTION("removes breakpoints")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.5f, 0.5f);
        env.addBreakpoint(1.0f, 1.0f);

        REQUIRE(env.removeBreakpoint(1));
        REQUIRE(env.getNumBreakpoints() == 2);
        REQUIRE(env.getBreakpoint(0).time == 0.0f);
        REQUIRE(env.getBreakpoint(1).time == 1.0f);
    }

    SECTION("remove returns false for invalid index")
    {
        REQUIRE_FALSE(env.removeBreakpoint(0));
        REQUIRE_FALSE(env.removeBreakpoint(-1));
    }

    SECTION("moves breakpoints")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);

        REQUIRE(env.moveBreakpoint(1, 2.0f, 0.5f));
        REQUIRE(env.getBreakpoint(1).time == 2.0f);
        REQUIRE(env.getBreakpoint(1).value == 0.5f);
    }

    SECTION("clears breakpoints")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        REQUIRE(env.getNumBreakpoints() == 2);

        env.clearBreakpoints();
        REQUIRE(env.getNumBreakpoints() == 0);
    }

    SECTION("clamps time to 0-10 seconds")
    {
        env.addBreakpoint(-1.0f, 0.0f);
        REQUIRE(env.getBreakpoint(0).time == 0.0f);

        env.addBreakpoint(20.0f, 1.0f);
        REQUIRE(env.getBreakpoint(1).time == 10.0f);
    }

    SECTION("clamps value to 0-1")
    {
        env.addBreakpoint(0.0f, -0.5f);
        REQUIRE(env.getBreakpoint(0).value == 0.0f);

        env.addBreakpoint(1.0f, 1.5f);
        REQUIRE(env.getBreakpoint(1).value == 1.0f);
    }

    SECTION("enforces maximum breakpoints")
    {
        for (int i = 0; i < MultiPointEnvelope::maxBreakpoints; ++i)
            REQUIRE(env.addBreakpoint(static_cast<float>(i) * 0.1f, 0.5f));

        REQUIRE_FALSE(env.addBreakpoint(10.0f, 0.5f));
        REQUIRE(env.getNumBreakpoints() == MultiPointEnvelope::maxBreakpoints);
    }

    SECTION("needs at least 2 breakpoints for trigger")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.trigger();
        REQUIRE_FALSE(env.isActive());
    }
}

//==============================================================================
TEST_CASE("MultiPointEnvelope: basic playback", "[envelope][playback]")
{
    MultiPointEnvelope env;

    SECTION("simple ramp 0->1")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.prepare(48000.0);
        env.trigger();

        // At 48000 Hz, 1 second = 48000 samples
        // After 24000 samples (0.5s), should be at ~0.5
        float val = advance(env, 24000);
        REQUIRE(val == Catch::Approx(0.5f).margin(0.01f));

        // After another 24000 samples (1.0s total), should be at ~1.0
        val = advance(env, 24000);
        REQUIRE(val == Catch::Approx(1.0f).margin(0.01f));

        // Envelope should now be inactive
        REQUIRE_FALSE(env.isActive());
    }

    SECTION("two-segment envelope")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.5f, 1.0f);
        env.addBreakpoint(1.0f, 0.0f);
        env.prepare(48000.0);
        env.trigger();

        // At 0.25s: halfway through first segment, should be ~0.5
        float val = advance(env, 12000);
        REQUIRE(val == Catch::Approx(0.5f).margin(0.01f));

        // At 0.5s: peak at 1.0
        val = advance(env, 12000);
        REQUIRE(val == Catch::Approx(1.0f).margin(0.01f));

        // At 0.75s: halfway down, should be ~0.5
        val = advance(env, 12000);
        REQUIRE(val == Catch::Approx(0.5f).margin(0.01f));

        // At 1.0s: back to 0
        val = advance(env, 12000);
        REQUIRE(val == Catch::Approx(0.0f).margin(0.01f));

        REQUIRE_FALSE(env.isActive());
    }

    SECTION("holds final value after completion")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.5f, 1.0f);
        env.prepare(48000.0);
        env.trigger();

        float val = processFull(env);
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
    }

    SECTION("getValue returns current without advancing")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.prepare(48000.0);
        env.trigger();

        REQUIRE(env.getValue() == Catch::Approx(0.0f));
        env.process(24000);
        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.01f));
    }

    SECTION("reset stops the envelope")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.prepare(48000.0);
        env.trigger();
        REQUIRE(env.isActive());

        env.reset();
        REQUIRE_FALSE(env.isActive());
    }

    SECTION("trigger restarts from beginning")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.prepare(48000.0);
        env.trigger();

        env.process(48000); // Play to end
        REQUIRE_FALSE(env.isActive());

        env.trigger();
        REQUIRE(env.isActive());
        REQUIRE(env.getValue() == Catch::Approx(0.0f));
    }
}

//==============================================================================
TEST_CASE("MultiPointEnvelope: curve interpolation", "[envelope][curves]")
{
    // Use a simple 0->1 ramp to verify curve shapes
    // At t=0.5, linear should be 0.5, exponential should be >0.5 (fast early),
    // s-curve should be exactly 0.5 (symmetric)
    MultiPointEnvelope env;
    env.addBreakpoint(0.0f, 0.0f);
    env.prepare(48000.0);

    SECTION("linear interpolation")
    {
        env.addBreakpoint(1.0f, 1.0f, CurveType::Linear);
        env.trigger();

        float val = advance(env, 24000); // 0.5s
        REQUIRE(val == Catch::Approx(0.5f).margin(0.001f));
    }

    SECTION("exponential curve: fast initial growth (early values > linear)")
    {
        env.addBreakpoint(1.0f, 1.0f, CurveType::Exponential);
        env.trigger();

        float valEarly = advance(env, 12000); // 0.25s, should overshoot linear 0.25
        REQUIRE(valEarly > 0.25f);
        REQUIRE(valEarly < 1.0f);

        float valMid = advance(env, 12000); // 0.5s, should overshoot linear 0.5
        REQUIRE(valMid > 0.5f);
        REQUIRE(valMid < 1.0f);
    }

    SECTION("s-curve: symmetric at midpoint")
    {
        env.addBreakpoint(1.0f, 1.0f, CurveType::SCurve);
        env.trigger();

        // S-curve is symmetric: at midpoint it should be exactly 0.5
        float valMid = advance(env, 24000); // 0.5s
        REQUIRE(valMid == Catch::Approx(0.5f).margin(0.001f));

        // Early: should be slower than linear (below 0.25 at 0.25s)
        env.trigger();
        float valEarly = advance(env, 12000); // 0.25s
        REQUIRE(valEarly < 0.25f);
        REQUIRE(valEarly > 0.0f);

        // Late: should be faster than linear at 0.75s
        env.trigger();
        advance(env, 36000); // advance to 0.75s
        float valLate = env.getValue();
        REQUIRE(valLate > 0.75f);
    }

    SECTION("per-segment curve types")
    {
        // First segment: exponential (fast rise)
        env.addBreakpoint(0.5f, 1.0f, CurveType::Exponential);
        // Second segment: linear (fall back)
        env.addBreakpoint(1.0f, 0.0f, CurveType::Linear);
        env.trigger();

        // Early in first segment: should be above linear
        float val1 = advance(env, 12000); // 0.25s
        REQUIRE(val1 > 0.25f);

        // At 0.5s: should be at peak
        advance(env, 12000);
        REQUIRE(env.getValue() == Catch::Approx(1.0f).margin(0.01f));

        // Midway through second segment (0.75s): linear descent, should be ~0.5
        advance(env, 12000);
        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.01f));
    }

    SECTION("exponential curve: reaching near-target at high t")
    {
        env.addBreakpoint(1.0f, 1.0f, CurveType::Exponential);
        env.trigger();

        // At 0.9s, exponential should be very close to 1.0
        float valLate = advance(env, 43200); // 0.9s
        REQUIRE(valLate == Catch::Approx(1.0f).margin(0.05f));
    }

    SECTION("exponential curve: non-zero start, non-one end")
    {
        env.addBreakpoint(1.0f, 0.5f, CurveType::Exponential);
        env.trigger();

        // At 0.25s: should be > 0.125 (linear midpoint for 0->0.5)
        float val = advance(env, 12000);
        REQUIRE(val == Catch::Approx(0.5f * (1.0f - std::exp(-4.0f * 0.25f))
                / (1.0f - std::exp(-4.0f))).margin(0.01f));
    }
}

//==============================================================================
TEST_CASE("MultiPointEnvelope: loop modes", "[envelope][loop]")
{
    SECTION("forward loop: cycles from loopStart back to loopStart")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.2f, 1.0f);
        env.addBreakpoint(0.4f, 0.5f);
        env.addBreakpoint(0.6f, 0.0f); // Not in loop section

        env.setLoopMode(LoopMode::Forward);
        env.setLoopStart(0);
        env.setLoopEnd(2); // Loop points 0-2 (0->1->0.5)

        env.prepare(48000.0);
        env.trigger();

        // Play through the 0.6s envelope fully (28800 samples) and continue
        // After 0.6s, should wrap back to loopStart (time=0)
        for (int i = 0; i < 5; ++i)
            advance(env, 28800); // 5 iterations

        // Should still be active (looping)
        REQUIRE(env.isActive());

        // Verify we can see the looping: at time 0.1s (4800 samples) within a cycle,
        // should be halfway up the first segment
        env.trigger(); // Reset
        advance(env, 4800); // 0.1s = halfway between 0 and 0.2
        float midRiseVal = env.getValue();
        REQUIRE(midRiseVal == Catch::Approx(0.5f).margin(0.01f));

        // After wrapping from 0.6 to 0 and coming up to 0.1 again:
        advance(env, 24000); // advance to 0.6s (end of first cycle)
        advance(env, 4800);  // 0.1s into second cycle
        float loopedVal = env.getValue();
        REQUIRE(loopedVal == Catch::Approx(midRiseVal).margin(0.01f));
    }

    SECTION("forward loop: stops if loop not configured")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.1f, 1.0f);

        env.setLoopMode(LoopMode::Forward);
        // loopStart=0, loopEnd not set (both default/unchanged)
        // loopEnd defaults to -1, which means loopEndSec = totalEndSec

        env.prepare(48000.0);
        env.trigger();
        advance(env, 48000); // 1s, well past 0.1s
        REQUIRE_FALSE(env.isActive());
    }

    SECTION("ping-pong: bounces between loop boundaries")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.2f, 1.0f);
        env.addBreakpoint(0.4f, 0.0f);

        env.setLoopMode(LoopMode::PingPong);
        env.setLoopStart(0);
        env.setLoopEnd(2);

        env.prepare(48000.0);
        env.trigger();

        // Play forward through 0.4s
        advance(env, 19200); // 0.4s, should be at end (value 0)

        // Should reverse and go back. At 0.2s in backward = 0.5s total
        // Going backward from point 2 (value 0) to point 1 (value 1)
        // At 0.1s backward = halfway to point 1
        advance(env, 4800); // go backward 0.1s
        float backVal = env.getValue();
        REQUIRE(backVal == Catch::Approx(0.5f).margin(0.05f));

        // Should still be active after many bounces
        for (int i = 0; i < 20; ++i)
            advance(env, 4800);

        REQUIRE(env.isActive());
    }

    SECTION("ping-pong: reverse direction check at boundaries")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.1f, 1.0f);
        env.addBreakpoint(0.2f, 0.0f);

        env.setLoopMode(LoopMode::PingPong);
        env.setLoopStart(0);
        env.setLoopEnd(2);

        env.prepare(48000.0);
        env.trigger();

        // Forward to end (0.2s)
        advance(env, 9600);
        REQUIRE(env.getValue() == Catch::Approx(0.0f).margin(0.01f));

        // Should now go backward. 0.1s backward = at point 1 (value 1)
        advance(env, 4800);
        REQUIRE(env.getValue() == Catch::Approx(1.0f).margin(0.05f));

        // Continue backward to start (0.0s, value 0)
        advance(env, 4800);
        REQUIRE(env.getValue() == Catch::Approx(0.0f).margin(0.01f));

        // Should bounce forward again. 0.05s forward = at middle (value 0.5)
        advance(env, 2400);
        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.05f));
    }

    SECTION("sustain: holds at loop end until release")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.1f, 1.0f);   // Attack peak
        env.addBreakpoint(0.3f, 0.5f);   // Decay to sustain level
        env.addBreakpoint(0.5f, 0.5f);   // Maintain sustain
        env.addBreakpoint(1.0f, 0.0f);   // Release to zero

        env.setLoopMode(LoopMode::Sustain);
        env.setLoopEnd(3); // Sustain at breakpoint 3 (time=0.5, value=0.5)

        env.prepare(48000.0);
        env.trigger();

        // Reach sustain point at 0.5s
        advance(env, 24000);
        float sustainVal = env.getValue();
        REQUIRE(sustainVal == Catch::Approx(0.5f).margin(0.01f));

        // Hold for a while
        advance(env, 48000); // 1 more second
        REQUIRE(env.isActive());
        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.01f));

        // Release and let it play through
        env.release();
        REQUIRE(env.isReleased());

        // Should now continue past sustain to end (from 0.5 to 1.0s)
        advance(env, 24000); // 0.5s more
        float releaseVal = env.getValue();
        REQUIRE(releaseVal == Catch::Approx(0.0f).margin(0.05f));
        REQUIRE_FALSE(env.isActive());
    }

    SECTION("sustain: release before reaching sustain continues past")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.3f, 0.5f);
        env.addBreakpoint(0.6f, 0.0f);

        env.setLoopMode(LoopMode::Sustain);
        env.setLoopEnd(1); // Sustain at breakpoint 1

        env.prepare(48000.0);
        env.trigger();

        // Release early (before reaching sustain point)
        env.release();
        advance(env, 9600); // 0.2s

        // Should play past sustain point since released flag is set
        REQUIRE(env.isActive());
        advance(env, 28800); // 0.6s more, should finish
        REQUIRE_FALSE(env.isActive());
    }

    SECTION("none: plays once and stops")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 1.0f);
        env.addBreakpoint(0.5f, 0.0f);
        env.setLoopMode(LoopMode::None);

        env.prepare(48000.0);
        env.trigger();
        advance(env, 48000); // 1s, well past 0.5s
        REQUIRE_FALSE(env.isActive());
    }
}

//==============================================================================
TEST_CASE("MultiPointEnvelope: trigger/release lifecycle", "[envelope][lifecycle]")
{
    MultiPointEnvelope env;
    env.addBreakpoint(0.0f, 0.0f);
    env.addBreakpoint(0.5f, 1.0f);
    env.addBreakpoint(1.0f, 0.0f);
    env.prepare(48000.0);

    SECTION("trigger starts envelope")
    {
        REQUIRE_FALSE(env.isActive());
        env.trigger();
        REQUIRE(env.isActive());
    }

    SECTION("release is tracked")
    {
        env.trigger();
        REQUIRE_FALSE(env.isReleased());
        env.release();
        REQUIRE(env.isReleased());
    }

    SECTION("multiple trigger calls restart")
    {
        env.trigger();
        advance(env, 24000);
        REQUIRE(env.getValue() == Catch::Approx(1.0f).margin(0.01f));

        env.trigger();
        REQUIRE(env.getValue() == Catch::Approx(0.0f));
    }

    SECTION("trigger after completion")
    {
        env.trigger();
        advance(env, 96000); // 2s, past end
        REQUIRE_FALSE(env.isActive());

        env.trigger();
        REQUIRE(env.isActive());
    }

    SECTION("process at 0 returns current value")
    {
        env.trigger();
        float val = env.process(0);
        REQUIRE(val == Catch::Approx(0.0f));
    }
}

//==============================================================================
TEST_CASE("MultiPointEnvelope: tempo sync", "[envelope][tempo][sync]")
{
    MultiPointEnvelope env;

    SECTION("sync off uses raw time")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.setSyncMode(false);

        env.prepare(48000.0);
        env.trigger();
        advance(env, 24000); // 0.5s

        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.01f));
    }

    SECTION("sync on scales time by bpm and division")
    {
        // At 120 BPM, beatDivision=1.0 (quarter notes):
        // 1 beat = 0.5 seconds
        // So a breakpoint at time=1.0 in beats = 0.5s in real time
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f); // 1 quarter note
        env.setTempo(120.0);
        env.setBeatDivision(1.0);
        env.setSyncMode(true);

        env.prepare(48000.0);
        env.trigger();
        advance(env, 12000); // 0.25s = half a beat

        // At half a beat through a 1-beat envelope = 50%
        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.01f));
    }

    SECTION("sync at different tempos changes timing")
    {
        // 60 BPM: 1 beat = 1 second
        // 120 BPM: 1 beat = 0.5 seconds
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.setBeatDivision(1.0);
        env.setSyncMode(true);

        env.setTempo(60.0);
        env.prepare(48000.0);
        env.trigger();
        advance(env, 24000); // 0.5s at 60BPM = 0.5 beats through 1 beat envelope
        float val60 = env.getValue();
        REQUIRE(val60 == Catch::Approx(0.5f).margin(0.01f));

        env.setTempo(120.0);
        env.prepare(48000.0);
        env.trigger();
        advance(env, 24000); // 0.5s at 120BPM = 1 full beat, finish
        float val120 = env.getValue();
        REQUIRE(val120 >= 0.99f);
    }

    SECTION("beat division changes timing")
    {
        // At 120 BPM, beatDivision=0.5 (eighth notes):
        // 1 eighth note = 0.25 seconds
        // Breakpoint at time=1.0 in eighth notes = 0.25s real time
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.setTempo(120.0);
        env.setSyncMode(true);

        env.setBeatDivision(0.5);
        env.prepare(48000.0);
        env.trigger();
        advance(env, 12000); // 0.25s = 1 eighth note
        REQUIRE(env.getValue() >= 0.99f); // Should have completed
    }
}

//==============================================================================
TEST_CASE("MultiPointEnvelope: edge cases", "[envelope][edge]")
{
    MultiPointEnvelope env;
    env.prepare(44100.0);

    SECTION("no breakpoints: process returns 0")
    {
        float val = env.process(64);
        REQUIRE(val == 0.0f);
        REQUIRE_FALSE(env.isActive());
    }

    SECTION("single breakpoint: cannot trigger, returns 0")
    {
        env.addBreakpoint(0.0f, 1.0f);
        env.trigger();
        REQUIRE_FALSE(env.isActive());
        REQUIRE(env.process(64) == 0.0f);
    }

    SECTION("all breakpoints at same time")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.0f, 1.0f);
        env.prepare(48000.0);
        env.trigger();

        // Zero-duration segment should immediately go to end value
        float val = env.process(1);
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
        REQUIRE_FALSE(env.isActive());
    }

    SECTION("breakpoints at max time (10s)")
    {
        REQUIRE(env.addBreakpoint(0.0f, 0.0f));
        REQUIRE(env.addBreakpoint(10.0f, 1.0f));
        env.prepare(100.0); // Low sample rate for fast test
        env.trigger();
        REQUIRE(env.isActive());

        // At 5s with 100Hz = 500 samples
        advance(env, 500);
        REQUIRE(env.getValue() == Catch::Approx(0.5f).margin(0.01f));
    }

    SECTION("process with large block sizes")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f);
        env.prepare(48000.0);
        env.trigger();

        float val = env.process(48000); // Jump 1 second at once
        REQUIRE(val == Catch::Approx(1.0f).margin(0.001f));
        REQUIRE_FALSE(env.isActive());
    }

    SECTION("add/remove clears loop indices safely")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.5f, 0.5f);
        env.addBreakpoint(1.0f, 1.0f);
        env.setLoopEnd(2);

        env.removeBreakpoint(2); // Remove the loop end point
        // Should not crash
        REQUIRE(env.getNumBreakpoints() == 2);

        env.clearBreakpoints();
        env.setLoopStart(0);
        // No crash expected
    }

    SECTION("interpolation at exact breakpoint times")
    {
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(0.5f, 0.7f);
        env.addBreakpoint(1.0f, 1.0f);
        env.prepare(48000.0);
        env.trigger();

        // At exactly 0.5s, should be 0.7
        advance(env, 24000);
        REQUIRE(env.getValue() == Catch::Approx(0.7f).margin(0.001f));

        // At exactly 0.5s in next segment boundary
        advance(env, 24000);
        REQUIRE(env.getValue() == Catch::Approx(1.0f).margin(0.001f));
    }
}

//==============================================================================
TEST_CASE("MultiPointEnvelope: specific curve verification", "[envelope][curves][math]")
{
    SECTION("linear: identity mapping at all points")
    {
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f, CurveType::Linear);
        env.prepare(96000.0);
        env.trigger();

        // Sample at multiple points
        for (int step = 1; step <= 10; ++step)
        {
            float expected = static_cast<float>(step) / 10.0f;
            float val = advance(env, 9600); // 0.1s each

            REQUIRE(val == Catch::Approx(expected).margin(0.001f));
        }
    }

    SECTION("exponential formula matches expected curve")
    {
        // Expected formula: (1 - e^(-4*t)) / (1 - e^(-4))
        constexpr float k = 4.0f;
        const float denom = 1.0f - std::exp(-k);

        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f, CurveType::Exponential);
        env.prepare(96000.0);
        env.trigger();

        for (int step = 1; step <= 10; ++step)
        {
            float t = static_cast<float>(step) / 10.0f;
            float expected = (1.0f - std::exp(-k * t)) / denom;
            float val = advance(env, 9600); // 0.1s each

            REQUIRE(val == Catch::Approx(expected).margin(0.01f));
        }
    }

    SECTION("s-curve formula matches smoothstep")
    {
        // Expected: 3t^2 - 2t^3
        MultiPointEnvelope env;
        env.addBreakpoint(0.0f, 0.0f);
        env.addBreakpoint(1.0f, 1.0f, CurveType::SCurve);
        env.prepare(96000.0);
        env.trigger();

        for (int step = 1; step <= 10; ++step)
        {
            float t = static_cast<float>(step) / 10.0f;
            float expected = t * t * (3.0f - 2.0f * t);
            float val = advance(env, 9600); // 0.1s each

            REQUIRE(val == Catch::Approx(expected).margin(0.001f));
        }
    }

    SECTION("all curve endpoints converge")
    {
        // All curves should hit the target value at t=1 regardless of type
        auto testCurve = [](CurveType type)
        {
            MultiPointEnvelope env;
            env.addBreakpoint(0.0f, 0.2f);
            env.addBreakpoint(1.0f, 0.8f, type);
            env.prepare(48000.0);
            env.trigger();

            env.process(48000); // 1s
            return env.getValue();
        };

        REQUIRE(testCurve(CurveType::Linear) == Catch::Approx(0.8f).margin(0.001f));
        REQUIRE(testCurve(CurveType::Exponential) == Catch::Approx(0.8f).margin(0.001f));
        REQUIRE(testCurve(CurveType::SCurve) == Catch::Approx(0.8f).margin(0.001f));
    }
}
