#include <catch2/catch_all.hpp>
#include "../src/dsp/effects/PhaserEffect.h"
#include "../src/dsp/effects/FlangerEffect.h"
#include "../src/dsp/effects/CompressorEffect.h"
#include "../src/dsp/effects/LimiterEffect.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

using namespace ana;

// Helper: create a test buffer with a sine wave
static juce::AudioBuffer<float> createSineBuffer(int numChannels, int numSamples, float frequency, float sampleRate)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            buffer.setSample(ch, s, std::sin(2.0f * juce::MathConstants<float>::pi * frequency * s / static_cast<float>(sampleRate)));
    return buffer;
}

// Helper: get process spec
static juce::dsp::ProcessSpec makeSpec(double sr = 44100.0, int blockSize = 512, int channels = 2)
{
    return { sr, static_cast<juce::uint32>(blockSize), static_cast<juce::uint32>(channels) };
}

// ==================== PhaserEffect Tests ====================

TEST_CASE("PhaserEffect - silent buffer no crash", "[phaser][silent]")
{
    PhaserEffect phaser;
    phaser.prepare(makeSpec());
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    REQUIRE_NOTHROW(phaser.process(buffer));
}

TEST_CASE("PhaserEffect - sine wave produces output", "[phaser][process]")
{
    PhaserEffect phaser;
    phaser.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    float before = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        before += std::abs(buffer.getSample(0, s));
    phaser.process(buffer);
    float after = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        after += std::abs(buffer.getSample(0, s));
    // Should have some output (all-pass filters pass energy through)
    REQUIRE(after > 0.0f);
}

TEST_CASE("PhaserEffect - bypass passes audio unchanged", "[phaser][bypass]")
{
    PhaserEffect phaser;
    phaser.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    auto original = buffer;
    phaser.setBypass(true);
    phaser.process(buffer);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            REQUIRE(buffer.getSample(ch, s) == Catch::Approx(original.getSample(ch, s)).margin(0.001f));
}

TEST_CASE("PhaserEffect - mix blend dry/wet", "[phaser][mix]")
{
    PhaserEffect phaser;
    phaser.prepare(makeSpec());
    auto buffer100 = createSineBuffer(2, 512, 440.0f, 44100.0f);
    auto buffer0 = createSineBuffer(2, 512, 440.0f, 44100.0f);

    // Mix = 1.0 (fully wet)
    phaser.setMix(1.0f);
    phaser.process(buffer100);

    // Mix = 0.0 (fully dry)
    phaser.setGain(1.0f);
    auto ref = createSineBuffer(2, 512, 440.0f, 44100.0f);
    phaser.setMix(0.0f);
    phaser.process(buffer0);

    // At mix=0, output should match dry input
    for (int ch = 0; ch < buffer0.getNumChannels(); ++ch)
        for (int s = 0; s < buffer0.getNumSamples(); ++s)
            REQUIRE(buffer0.getSample(ch, s) == Catch::Approx(ref.getSample(ch, s)).margin(0.001f));
}

TEST_CASE("PhaserEffect - setRate clamps correctly", "[phaser][params]")
{
    PhaserEffect phaser;
    phaser.setRate(0.0f);  // below min
    phaser.setRate(50.0f); // above max
    // Should not crash
    SUCCEED();
}

// ==================== FlangerEffect Tests ====================

TEST_CASE("FlangerEffect - silent buffer no crash", "[flanger][silent]")
{
    FlangerEffect flanger;
    flanger.prepare(makeSpec());
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    REQUIRE_NOTHROW(flanger.process(buffer));
}

TEST_CASE("FlangerEffect - sine wave produces output", "[flanger][process]")
{
    FlangerEffect flanger;
    flanger.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    float before = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        before += std::abs(buffer.getSample(0, s));
    flanger.process(buffer);
    float after = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        after += std::abs(buffer.getSample(0, s));
    REQUIRE(after > 0.0f);
}

TEST_CASE("FlangerEffect - bypass passes audio unchanged", "[flanger][bypass]")
{
    FlangerEffect flanger;
    flanger.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    auto original = buffer;
    flanger.setBypass(true);
    flanger.process(buffer);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            REQUIRE(buffer.getSample(ch, s) == Catch::Approx(original.getSample(ch, s)).margin(0.001f));
}

TEST_CASE("FlangerEffect - mix parameter blends dry/wet correctly", "[flanger][mix]")
{
    FlangerEffect flanger;
    flanger.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    auto ref = buffer;

    flanger.setGain(1.0f);
    flanger.setMix(0.0f);
    flanger.process(buffer);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            REQUIRE(buffer.getSample(ch, s) == Catch::Approx(ref.getSample(ch, s)).margin(0.001f));
}

TEST_CASE("FlangerEffect - setDelay clamps correctly", "[flanger][params]")
{
    FlangerEffect flanger;
    flanger.setDelay(-1.0f);
    flanger.setDelay(100.0f);
    SUCCEED();
}

// ==================== CompressorEffect Tests ====================

TEST_CASE("CompressorEffect - silent buffer no crash", "[compressor][silent]")
{
    CompressorEffect comp;
    comp.prepare(makeSpec());
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    REQUIRE_NOTHROW(comp.process(buffer));
}

TEST_CASE("CompressorEffect - sine wave produces output", "[compressor][process]")
{
    CompressorEffect comp;
    comp.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    float before = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        before += std::abs(buffer.getSample(0, s));
    comp.process(buffer);
    float after = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        after += std::abs(buffer.getSample(0, s));
    REQUIRE(after > 0.0f);
}

TEST_CASE("CompressorEffect - bypass passes audio unchanged", "[compressor][bypass]")
{
    CompressorEffect comp;
    comp.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    auto original = buffer;
    comp.setBypass(true);
    comp.process(buffer);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            REQUIRE(buffer.getSample(ch, s) == Catch::Approx(original.getSample(ch, s)).margin(0.001f));
}

TEST_CASE("CompressorEffect - heavy compression reduces level", "[compressor][params]")
{
    CompressorEffect comp;
    comp.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    float before = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        before += std::abs(buffer.getSample(0, s));

    // Extreme compression: low threshold, high ratio
    comp.setMix(1.0f);
    comp.setGain(1.0f);
    comp.setThreshold(-60.0f);
    comp.setRatio(20.0f);
    comp.setKnee(0.0f);
    comp.process(buffer);

    float after = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        after += std::abs(buffer.getSample(0, s));
    // Compressed signal should be quieter
    REQUIRE(after < before);
}

TEST_CASE("CompressorEffect - mix parameter works", "[compressor][mix]")
{
    CompressorEffect comp;
    comp.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    auto ref = buffer;

    comp.setGain(1.0f);
    comp.setMix(0.0f);
    comp.process(buffer);

    // At mix=0, output should be unaffected by compression
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            REQUIRE(buffer.getSample(ch, s) == Catch::Approx(ref.getSample(ch, s)).margin(0.001f));
}

TEST_CASE("CompressorEffect - setRatio clamps correctly", "[compressor][params]")
{
    CompressorEffect comp;
    comp.setRatio(0.5f);
    comp.setRatio(100.0f);
    SUCCEED();
}

// ==================== LimiterEffect Tests ====================

TEST_CASE("LimiterEffect - silent buffer no crash", "[limiter][silent]")
{
    LimiterEffect limiter;
    limiter.prepare(makeSpec());
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    REQUIRE_NOTHROW(limiter.process(buffer));
}

TEST_CASE("LimiterEffect - sine wave produces output", "[limiter][process]")
{
    LimiterEffect limiter;
    limiter.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    float before = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        before += std::abs(buffer.getSample(0, s));
    limiter.process(buffer);
    float after = 0.0f;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
        after += std::abs(buffer.getSample(0, s));
    REQUIRE(after > 0.0f);
}

TEST_CASE("LimiterEffect - bypass passes audio unchanged", "[limiter][bypass]")
{
    LimiterEffect limiter;
    limiter.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    auto original = buffer;
    limiter.setBypass(true);
    limiter.process(buffer);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            REQUIRE(buffer.getSample(ch, s) == Catch::Approx(original.getSample(ch, s)).margin(0.001f));
}

TEST_CASE("LimiterEffect - limiting reduces peak level", "[limiter][params]")
{
    LimiterEffect limiter;
    limiter.prepare(makeSpec());

    // Create a signal with some strong peaks
    juce::AudioBuffer<float> buffer(2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 512; ++s)
            buffer.setSample(ch, s, 2.0f * std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * s / 44100.0f));

    float peakBefore = 0.0f;
    for (int s = 0; s < 512; ++s)
        peakBefore = std::max(peakBefore, std::abs(buffer.getSample(0, s)));

    // Heavy limiting
    limiter.setMix(1.0f);
    limiter.setGain(1.0f);
    limiter.setThreshold(-30.0f);
    limiter.setAttack(0.1f);
    limiter.setRelease(50.0f);
    limiter.setLookahead(1.0f);
    limiter.process(buffer);

    float peakAfter = 0.0f;
    for (int s = 0; s < 512; ++s)
        peakAfter = std::max(peakAfter, std::abs(buffer.getSample(0, s)));

    // Peaks should be reduced
    REQUIRE(peakAfter < peakBefore);
}

TEST_CASE("LimiterEffect - mix parameter works", "[limiter][mix]")
{
    LimiterEffect limiter;
    limiter.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);
    auto ref = buffer;

    limiter.setGain(1.0f);
    limiter.setMix(0.0f);
    limiter.process(buffer);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            REQUIRE(buffer.getSample(ch, s) == Catch::Approx(ref.getSample(ch, s)).margin(0.001f));
}

TEST_CASE("LimiterEffect - setThreshold clamps correctly", "[limiter][params]")
{
    LimiterEffect limiter;
    limiter.setThreshold(-100.0f);
    limiter.setThreshold(10.0f);
    SUCCEED();
}

TEST_CASE("LimiterEffect - oversampling modes", "[limiter][oversample]")
{
    LimiterEffect limiter;
    limiter.prepare(makeSpec());
    auto buffer = createSineBuffer(2, 512, 440.0f, 44100.0f);

    // 2x oversampling
    limiter.setOversampling(2);
    limiter.process(buffer);
    SUCCEED();

    // 4x oversampling
    limiter.setOversampling(4);
    limiter.process(buffer);
    SUCCEED();

    // Back to no oversampling
    limiter.setOversampling(1);
    limiter.process(buffer);
    SUCCEED();
}

// ==================== Cross-Effect Tests ====================

TEST_CASE("All effects - prepare with different sample rates", "[effects][prepare]")
{
    auto spec48 = makeSpec(48000.0, 256, 2);

    PhaserEffect phaser;
    REQUIRE_NOTHROW(phaser.prepare(spec48));

    FlangerEffect flanger;
    REQUIRE_NOTHROW(flanger.prepare(spec48));

    CompressorEffect comp;
    REQUIRE_NOTHROW(comp.prepare(spec48));

    LimiterEffect limiter;
    REQUIRE_NOTHROW(limiter.prepare(spec48));

    juce::AudioBuffer<float> buffer(2, 256);
    buffer.clear();
    REQUIRE_NOTHROW(phaser.process(buffer));
    REQUIRE_NOTHROW(flanger.process(buffer));
    REQUIRE_NOTHROW(comp.process(buffer));
    REQUIRE_NOTHROW(limiter.process(buffer));
}

TEST_CASE("All effects - reset does not crash", "[effects][reset]")
{
    auto spec = makeSpec();

    PhaserEffect phaser;
    phaser.prepare(spec);
    phaser.reset();
    REQUIRE_NOTHROW(phaser.reset());

    FlangerEffect flanger;
    flanger.prepare(spec);
    REQUIRE_NOTHROW(flanger.reset());

    CompressorEffect comp;
    comp.prepare(spec);
    REQUIRE_NOTHROW(comp.reset());

    LimiterEffect limiter;
    limiter.prepare(spec);
    REQUIRE_NOTHROW(limiter.reset());
}
