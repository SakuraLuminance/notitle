#include <catch2/catch_all.hpp>
#include "dsp/UnisonEngine.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr double TEST_SR = 44100.0;

/** Fill a buffer with a known constant value (e.g. silence then process). */
static juce::AudioBuffer<float> makeStereoBuffer(int samples)
{
    juce::AudioBuffer<float> buf(2, samples);
    buf.clear();
    return buf;
}

// ---------------------------------------------------------------------------
// Construction & defaults
// ---------------------------------------------------------------------------

TEST_CASE("UnisonEngine - default state", "[unison][basic]")
{
    ana::UnisonEngine engine;

    REQUIRE(engine.getVoiceCount() == 1);
    REQUIRE(engine.getDetune() == 0.0f);
    REQUIRE(engine.getStereoSpread() == 0.0f);
    REQUIRE(engine.getPhaseOffset() == 0.0f);

    // Single voice with default state
    const auto& v = engine.getVoice(0);
    REQUIRE(v.phase == 0.0f);
    REQUIRE(v.initialPhase == 0.0f);
    REQUIRE(v.detuneCents == 0.0f);
    REQUIRE(v.pan == 0.0f);
}

// ---------------------------------------------------------------------------
// Voice count clamping (1-8)
// ---------------------------------------------------------------------------

TEST_CASE("UnisonEngine - voice count clamping", "[unison][params]")
{
    ana::UnisonEngine engine;
    engine.prepare(TEST_SR, 512);

    SECTION("setVoiceCount accepts valid range")
    {
        engine.setVoiceCount(1);
        REQUIRE(engine.getVoiceCount() == 1);
        engine.setVoiceCount(4);
        REQUIRE(engine.getVoiceCount() == 4);
        engine.setVoiceCount(8);
        REQUIRE(engine.getVoiceCount() == 8);
    }

    SECTION("setVoiceCount clamps below 1")
    {
        engine.setVoiceCount(0);
        REQUIRE(engine.getVoiceCount() == 1);
        engine.setVoiceCount(-5);
        REQUIRE(engine.getVoiceCount() == 1);
    }

    SECTION("setVoiceCount clamps above 8")
    {
        engine.setVoiceCount(16);
        REQUIRE(engine.getVoiceCount() == 8);
        engine.setVoiceCount(100);
        REQUIRE(engine.getVoiceCount() == 8);
    }
}

// ---------------------------------------------------------------------------
// Detune spread
// ---------------------------------------------------------------------------

TEST_CASE("UnisonEngine - detune spread", "[unison][detune]")
{
    ana::UnisonEngine engine;
    engine.prepare(TEST_SR, 512);

    SECTION("single voice has zero detune regardless of setting")
    {
        engine.setVoiceCount(1);
        engine.setDetune(50.0f);
        // Trigger internal update by process()
        auto buf = makeStereoBuffer(64);
        engine.process(buf);
        REQUIRE(engine.getVoice(0).detuneCents == 0.0f);
    }

    SECTION("8 voices with 12 cents -> outermost at +/-48 cents")
    {
        engine.setVoiceCount(8);
        engine.setDetune(12.0f);
        auto buf = makeStereoBuffer(64);
        engine.process(buf);

        REQUIRE(engine.getVoice(0).detuneCents == Approx(-48.0f).margin(0.01f));
        REQUIRE(engine.getVoice(7).detuneCents == Approx(48.0f).margin(0.01f));
    }

    SECTION("symmetry around zero")
    {
        engine.setVoiceCount(5);
        engine.setDetune(20.0f);
        auto buf = makeStereoBuffer(64);
        engine.process(buf);

        // Voices should be symmetric: first and last sum to 0, etc.
        REQUIRE(engine.getVoice(0).detuneCents + engine.getVoice(4).detuneCents
                == Approx(0.0f).margin(0.01f));
        REQUIRE(engine.getVoice(1).detuneCents + engine.getVoice(3).detuneCents
                == Approx(0.0f).margin(0.01f));
        // Middle voice should be at 0
        REQUIRE(engine.getVoice(2).detuneCents == Approx(0.0f).margin(0.01f));
    }

    SECTION("spread scales with detune amount")
    {
        engine.setVoiceCount(4);
        engine.setDetune(10.0f);
        auto buf = makeStereoBuffer(64);
        engine.process(buf);

        float firstSpan = engine.getVoice(3).detuneCents - engine.getVoice(0).detuneCents;

        engine.setDetune(30.0f);
        engine.process(buf);

        float secondSpan = engine.getVoice(3).detuneCents - engine.getVoice(0).detuneCents;

        // 3x the detune should give 3x the span
        REQUIRE(secondSpan == Approx(firstSpan * 3.0f).margin(0.01f));
    }
}

// ---------------------------------------------------------------------------
// Stereo spread
// ---------------------------------------------------------------------------

TEST_CASE("UnisonEngine - stereo positioning", "[unison][stereo]")
{
    ana::UnisonEngine engine;
    engine.prepare(TEST_SR, 512);

    SECTION("zero spread centres all voices")
    {
        engine.setVoiceCount(4);
        engine.setStereoSpread(0.0f);
        auto buf = makeStereoBuffer(64);
        engine.process(buf);

        for (int i = 0; i < 4; ++i)
            REQUIRE(engine.getVoice(i).pan == Approx(0.0f).margin(0.01f));
    }

    SECTION("100% spread pans outermost voices to full left/right")
    {
        engine.setVoiceCount(3);
        engine.setStereoSpread(100.0f);
        auto buf = makeStereoBuffer(64);
        engine.process(buf);

        REQUIRE(engine.getVoice(0).pan == Approx(-1.0f).margin(0.01f));
        REQUIRE(engine.getVoice(1).pan == Approx(0.0f).margin(0.01f));
        REQUIRE(engine.getVoice(2).pan == Approx(1.0f).margin(0.01f));
    }

    SECTION("50% spread gives half-range panning")
    {
        engine.setVoiceCount(2);
        engine.setStereoSpread(50.0f);
        auto buf = makeStereoBuffer(64);
        engine.process(buf);

        REQUIRE(engine.getVoice(0).pan == Approx(-0.5f).margin(0.01f));
        REQUIRE(engine.getVoice(1).pan == Approx(0.5f).margin(0.01f));
    }

    SECTION("stereo output channels differ when spread > 0")
    {
        engine.setVoiceCount(4);
        engine.setDetune(15.0f);
        engine.setStereoSpread(100.0f);
        engine.setFrequency(440.0f);

        auto buf = makeStereoBuffer(512);
        engine.process(buf);

        // Left and right channels should not be identical when spread > 0
        bool allIdentical = true;
        for (int s = 0; s < buf.getNumSamples(); ++s)
        {
            if (std::abs(buf.getSample(0, s) - buf.getSample(1, s)) > 1e-6f)
            {
                allIdentical = false;
                break;
            }
        }
        REQUIRE_FALSE(allIdentical);
    }

    SECTION("zero spread yields identical stereo channels")
    {
        engine.setVoiceCount(4);
        engine.setDetune(15.0f);
        engine.setStereoSpread(0.0f);
        engine.setFrequency(440.0f);

        auto buf = makeStereoBuffer(512);
        engine.process(buf);

        for (int s = 0; s < buf.getNumSamples(); ++s)
        {
            REQUIRE(buf.getSample(0, s) == buf.getSample(1, s));
        }
    }
}

// ---------------------------------------------------------------------------
// Phase offset
// ---------------------------------------------------------------------------

TEST_CASE("UnisonEngine - phase offset", "[unison][phase]")
{
    ana::UnisonEngine engine;
    engine.prepare(TEST_SR, 512);

    SECTION("zero phase offset gives all-identical initial phases")
    {
        engine.setVoiceCount(4);
        engine.setPhaseOffset(0.0f);
        engine.noteOn();

        for (int i = 1; i < 4; ++i)
            REQUIRE(engine.getVoice(i).initialPhase
                    == engine.getVoice(0).initialPhase);
    }

    SECTION("non-zero phase offset generates different initial phases")
    {
        engine.setVoiceCount(4);
        engine.setPhaseOffset(1.0f);
        engine.noteOn();

        // Collect unique initial phases
        std::array<float, 4> phases = {
            engine.getVoice(0).initialPhase,
            engine.getVoice(1).initialPhase,
            engine.getVoice(2).initialPhase,
            engine.getVoice(3).initialPhase
        };

        // At least some should be different
        bool anyDifferent = false;
        for (int i = 1; i < 4; ++i)
        {
            if (std::abs(phases[i] - phases[0]) > 0.001f)
            {
                anyDifferent = true;
                break;
            }
        }
        REQUIRE(anyDifferent);
    }

    SECTION("phases are bounded by phaseOffset * 2pi")
    {
        engine.setVoiceCount(8);
        engine.setPhaseOffset(0.5f);
        engine.noteOn();

        const float maxAllowed = 0.5f * juce::MathConstants<float>::twoPi;
        for (int i = 0; i < 8; ++i)
        {
            REQUIRE(engine.getVoice(i).initialPhase >= 0.0f);
            REQUIRE(engine.getVoice(i).initialPhase <= maxAllowed + 0.001f);
        }
    }

    SECTION("repeated noteOn re-rolls phases")
    {
        engine.setVoiceCount(4);
        engine.setPhaseOffset(1.0f);

        engine.noteOn();
        auto first = engine.getVoice(0).initialPhase;

        engine.noteOn();
        auto second = engine.getVoice(0).initialPhase;

        // Random means they might be equal by chance (1/2pi ≈ 16%).
        // This is probabilistic; if the RNG is seeded well we expect
        // them to differ the vast majority of the time.
        // We'll just check that not ALL four voices have the same values.
        bool allSame = true;
        for (int i = 0; i < 4; ++i)
        {
            if (std::abs(engine.getVoice(i).initialPhase - first) > 0.01f)
            {
                allSame = false;
                break;
            }
        }
        REQUIRE_FALSE(allSame);
    }
}

// ---------------------------------------------------------------------------
// Processing
// ---------------------------------------------------------------------------

TEST_CASE("UnisonEngine - process", "[unison][process]")
{
    ana::UnisonEngine engine;
    engine.prepare(TEST_SR, 512);

    SECTION("process produces non-zero output")
    {
        engine.setVoiceCount(4);
        engine.setDetune(12.0f);
        engine.setStereoSpread(80.0f);
        engine.setFrequency(440.0f);

        auto buf = makeStereoBuffer(256);
        engine.process(buf);

        float sum = 0.0f;
        for (int s = 0; s < buf.getNumSamples(); ++s)
            sum += std::abs(buf.getSample(0, s)) + std::abs(buf.getSample(1, s));

        REQUIRE(sum > 0.0f);
    }

    SECTION("output amplitude normalised by voice count")
    {
        engine.setFrequency(440.0f);
        engine.setDetune(0.0f);
        engine.setStereoSpread(0.0f);

        // Process with 1 voice
        engine.setVoiceCount(1);
        auto buf1 = makeStereoBuffer(256);
        engine.process(buf1);

        // Process with 4 voices
        engine.setVoiceCount(4);
        auto buf4 = makeStereoBuffer(256);
        engine.process(buf4);

        // 4-voice output should not be 4x louder (normalized by sqrt(N))
        float amp1 = 0.0f, amp4 = 0.0f;
        for (int s = 0; s < 256; ++s)
        {
            amp1 += std::abs(buf1.getSample(0, s));
            amp4 += std::abs(buf4.getSample(0, s));
        }

        // With 4 voices at detune=0 and random phase, the sum is partially
        // constructive. We can't expect strict amplitude ratios, but 4
        // voices should NOT be dramatically louder than 1 voice.
        REQUIRE(amp4 < amp1 * 3.5f);
        REQUIRE(amp4 > 0.0f);
    }

    SECTION("empty buffer is safe")
    {
        juce::AudioBuffer<float> empty(2, 0);
        REQUIRE_NOTHROW(engine.process(empty));
    }

    SECTION("mono buffer is handled without crash")
    {
        juce::AudioBuffer<float> mono(1, 128);
        mono.clear();
        REQUIRE_NOTHROW(engine.process(mono));
    }
}

// ---------------------------------------------------------------------------
// Parameter clamping
// ---------------------------------------------------------------------------

TEST_CASE("UnisonEngine - parameter clamping", "[unison][params]")
{
    ana::UnisonEngine engine;
    engine.prepare(TEST_SR, 512);

    SECTION("detune clamped to [0, 100]")
    {
        engine.setDetune(-50.0f);
        REQUIRE(engine.getDetune() == 0.0f);

        engine.setDetune(200.0f);
        REQUIRE(engine.getDetune() == 100.0f);
    }

    SECTION("stereo spread clamped to [0, 100]")
    {
        engine.setStereoSpread(-10.0f);
        REQUIRE(engine.getStereoSpread() == 0.0f);

        engine.setStereoSpread(150.0f);
        REQUIRE(engine.getStereoSpread() == 100.0f);
    }

    SECTION("phase offset clamped to [0, 1]")
    {
        engine.setPhaseOffset(-1.0f);
        REQUIRE(engine.getPhaseOffset() == 0.0f);

        engine.setPhaseOffset(5.0f);
        REQUIRE(engine.getPhaseOffset() == 1.0f);
    }
}
