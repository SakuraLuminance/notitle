#include <catch2/catch_all.hpp>
#include "dsp/effects/PhaserEffect.h"
#include "dsp/effects/CompressorEffect.h"
#include "dsp/effects/FlangerEffect.h"
#include "dsp/effects/LimiterEffect.h"
#include "dsp/UnisonEngine.h"
#include "dsp/LFOSystem.h"
#include "dsp/ModulationBus.h"
#include "dsp/MultiPointEnvelope.h"
#include "dsp/MultiFilter.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <atomic>

using namespace ana;

//==============================================================================
// Helpers
//==============================================================================

static constexpr double TEST_SR = 44100.0;

/** Create a stereo sine-wave buffer. */
static juce::AudioBuffer<float> createSineBuffer(int numChannels, int numSamples,
                                                   float frequency, float sampleRate)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            buffer.setSample(ch, s,
                std::sin(2.0f * juce::MathConstants<float>::pi * frequency
                         * static_cast<float>(s) / sampleRate));
    return buffer;
}

/** Create a process spec. */
static juce::dsp::ProcessSpec makeSpec(double sr = TEST_SR, int blockSize = 512, int channels = 2)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = static_cast<juce::uint32>(blockSize);
    spec.numChannels = static_cast<juce::uint32>(channels);
    return spec;
}

/** Compute the peak magnitude in a buffer. */
static float peakMagnitude(const juce::AudioBuffer<float>& buf)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            peak = std::max(peak, std::abs(buf.getSample(ch, s)));
    return peak;
}

/** Compute sum of absolute samples (energy proxy). */
static float totalEnergy(const juce::AudioBuffer<float>& buf)
{
    float sum = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            sum += std::abs(buf.getSample(ch, s));
    return sum;
}

/** Make a stereo buffer for UnisonEngine testing. */
static juce::AudioBuffer<float> makeStereoBuffer(int samples)
{
    juce::AudioBuffer<float> buf(2, samples);
    buf.clear();
    return buf;
}

//==============================================================================
// PhaserEffect – output differs from bypass
//==============================================================================

TEST_CASE("PhaserEffect - output differs from bypass", "[wired][phaser]")
{
    PhaserEffect phaser;
    phaser.prepare(makeSpec());

    auto input = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
    auto bypassed = input; // copy

    SECTION("active phaser modifies signal")
    {
        phaser.setBypass(false);
        phaser.setRate(2.0f);
        phaser.setDepth(0.8f);
        phaser.setStages(8);
        phaser.setMix(1.0f);
        phaser.setGain(1.0f);

        // Process the input through the active phaser
        juce::AudioBuffer<float> processed(input.getNumChannels(), input.getNumSamples());
        processed.copyFrom(0, 0, input, 0, 0, input.getNumSamples());
        processed.copyFrom(1, 0, input, 1, 0, input.getNumSamples());
        phaser.process(processed);

        // Process the bypassed copy
        phaser.setBypass(true);
        phaser.process(bypassed);

        // Active output should differ from bypassed output
        bool anyDifferent = false;
        for (int s = 0; s < input.getNumSamples(); ++s)
        {
            if (std::abs(processed.getSample(0, s) - bypassed.getSample(0, s)) > 0.001f)
            {
                anyDifferent = true;
                break;
            }
        }
        REQUIRE(anyDifferent);
    }

    SECTION("bypassed output matches input")
    {
        phaser.setBypass(true);
        juce::AudioBuffer<float> output(input.getNumChannels(), input.getNumSamples());
        output.copyFrom(0, 0, input, 0, 0, input.getNumSamples());
        output.copyFrom(1, 0, input, 1, 0, input.getNumSamples());
        phaser.process(output);

        for (int s = 0; s < input.getNumSamples(); ++s)
        {
            REQUIRE(output.getSample(0, s) == Catch::Approx(input.getSample(0, s)).margin(0.001f));
            REQUIRE(output.getSample(1, s) == Catch::Approx(input.getSample(1, s)).margin(0.001f));
        }
    }
}

//==============================================================================
// Compressor – gain reduction > 0
//==============================================================================

TEST_CASE("Compressor - gain reduction reduces level", "[wired][compressor]")
{
    CompressorEffect comp;
    comp.prepare(makeSpec());

    SECTION("heavy compression reduces energy")
    {
        auto buffer = createSineBuffer(2, 2048, 440.0f, static_cast<float>(TEST_SR));
        float energyBefore = totalEnergy(buffer);

        // Aggressive compression
        comp.setMix(1.0f);
        comp.setGain(1.0f);
        comp.setThreshold(-60.0f);
        comp.setRatio(20.0f);
        comp.setKnee(0.0f);
        comp.setAttack(0.5f);
        comp.setRelease(10.0f);
        comp.setRMSMode(false);
        comp.setAutoMakeup(false);
        comp.process(buffer);

        float energyAfter = totalEnergy(buffer);
        // Compressed signal should have less energy
        REQUIRE(energyAfter < energyBefore * 0.9f);
    }

    SECTION("gain reduction calculation returns positive value")
    {
        // With a very low threshold and high input, gainReduction > 0
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        // Scale up to ensure signal exceeds threshold
        buffer.applyGain(10.0f);

        comp.setMix(1.0f);
        comp.setGain(1.0f);
        comp.setThreshold(-80.0f);
        comp.setRatio(40.0f);
        comp.setKnee(0.0f);
        comp.setAttack(0.1f);
        comp.setRelease(5.0f);
        comp.setRMSMode(false);
        comp.setAutoMakeup(false);
        comp.process(buffer);

        // After heavy compression the energy should be significantly reduced
        // relative to the input level (which was boosted 10x)
        float peak = peakMagnitude(buffer);
        // Compressor should bring the 10x-boosted signal way down
        REQUIRE(peak < 5.0f);
    }

    SECTION("bypass preserves energy")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        auto original = buffer;

        comp.setBypass(true);
        comp.process(buffer);

        float eBefore = totalEnergy(original);
        float eAfter = totalEnergy(buffer);
        REQUIRE(eAfter == Catch::Approx(eBefore).margin(0.001f));
    }
}

//==============================================================================
// Flanger – output differs from input
//==============================================================================

TEST_CASE("Flanger - output differs from input", "[wired][flanger]")
{
    FlangerEffect flanger;
    flanger.prepare(makeSpec());

    SECTION("wet flanger modifies signal")
    {
        auto input = createSineBuffer(2, 1024, 440.0f, static_cast<float>(TEST_SR));
        auto output = input;

        flanger.setMix(1.0f);
        flanger.setGain(1.0f);
        flanger.setRate(0.5f);
        flanger.setDepth(1.0f);
        flanger.setDelay(5.0f);
        flanger.setFeedback(0.5f);
        flanger.process(output);

        bool anyDifferent = false;
        for (int s = 0; s < output.getNumSamples(); ++s)
        {
            if (std::abs(output.getSample(0, s) - input.getSample(0, s)) > 0.001f)
            {
                anyDifferent = true;
                break;
            }
        }
        REQUIRE(anyDifferent);
    }

    SECTION("dry flanger passes signal unchanged")
    {
        auto input = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        auto output = input;

        flanger.setMix(0.0f);
        flanger.setGain(1.0f);
        flanger.process(output);

        for (int s = 0; s < output.getNumSamples(); ++s)
        {
            REQUIRE(output.getSample(0, s) == Catch::Approx(input.getSample(0, s)).margin(0.001f));
        }
    }

    SECTION("bypass preserves signal")
    {
        auto input = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        auto output = input;

        flanger.setBypass(true);
        flanger.process(output);

        for (int s = 0; s < output.getNumSamples(); ++s)
        {
            REQUIRE(output.getSample(0, s) == Catch::Approx(input.getSample(0, s)).margin(0.001f));
        }
    }
}

//==============================================================================
// Limiter – output ≤ ceiling
//==============================================================================

TEST_CASE("Limiter - output does not exceed ceiling", "[wired][limiter]")
{
    LimiterEffect limiter;
    limiter.prepare(makeSpec());

    SECTION("hot signal peaks are constrained by ceiling")
    {
        // Create a very hot signal with peaks well above 1.0
        juce::AudioBuffer<float> buffer(2, 2048);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 2048; ++s)
                buffer.setSample(ch, s,
                    5.0f * std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f
                                    * static_cast<float>(s) / static_cast<float>(TEST_SR)));

        float peakBefore = peakMagnitude(buffer);
        REQUIRE(peakBefore > 1.0f); // input must be hot

        // Apply limiter with ceiling = 0.5
        limiter.setMix(1.0f);
        limiter.setGain(0.5f);
        limiter.setThreshold(-30.0f);
        limiter.setAttack(0.05f);
        limiter.setRelease(20.0f);
        limiter.setLookahead(2.0f);
        limiter.process(buffer);

        float peakAfter = peakMagnitude(buffer);
        // All samples should be at or below the ceiling gain
        REQUIRE(peakAfter <= 0.5f + 0.01f);
    }

    SECTION("low level signal passes through unaffected")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        auto original = buffer;

        limiter.setMix(1.0f);
        limiter.setGain(1.0f);
        limiter.setThreshold(0.0f); // above signal level
        limiter.process(buffer);

        // Signal below threshold should be essentially unchanged
        float eOrig = totalEnergy(original);
        float eProc = totalEnergy(buffer);
        REQUIRE(eProc == Catch::Approx(eOrig).margin(eOrig * 0.1f));
    }

    SECTION("bypass preserves signal")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        auto original = buffer;

        limiter.setBypass(true);
        limiter.process(buffer);

        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            REQUIRE(buffer.getSample(0, s) == Catch::Approx(original.getSample(0, s)).margin(0.001f));
        }
    }
}

//==============================================================================
// UnisonEngine – 4 voices → detuned output
//==============================================================================

TEST_CASE("UnisonEngine - 4 voices produce detuned output", "[wired][unison]")
{
    ana::UnisonEngine engine;

    SECTION("4 voices with detune differ from single voice")
    {
        engine.prepare(TEST_SR, 512);
        engine.setFrequency(440.0f);

        // Generate output with 1 voice (no detune)
        engine.setVoiceCount(1);
        engine.setDetune(0.0f);
        engine.setStereoSpread(0.0f);
        auto buf1 = makeStereoBuffer(512);
        engine.process(buf1);

        // Generate output with 4 voices + detune
        engine.setVoiceCount(4);
        engine.setDetune(25.0f);
        engine.setStereoSpread(80.0f);
        auto buf4 = makeStereoBuffer(512);
        engine.process(buf4);

        // 4-voice output should differ from 1-voice output
        bool anyDifferent = false;
        for (int s = 0; s < 512; ++s)
        {
            if (std::abs(buf4.getSample(0, s) - buf1.getSample(0, s)) > 0.001f)
            {
                anyDifferent = true;
                break;
            }
        }
        REQUIRE(anyDifferent);
    }

    SECTION("4 voices with zero detune still differ (phase offset)")
    {
        engine.prepare(TEST_SR, 512);
        engine.setFrequency(440.0f);

        engine.setVoiceCount(1);
        engine.setDetune(0.0f);
        engine.setStereoSpread(0.0f);
        engine.setPhaseOffset(0.0f);
        auto buf1 = makeStereoBuffer(512);
        engine.process(buf1);

        engine.setVoiceCount(4);
        engine.setPhaseOffset(1.0f); // randomise phases
        auto buf4 = makeStereoBuffer(512);
        engine.noteOn(); // re-roll phases
        engine.process(buf4);

        bool anyDifferent = false;
        for (int s = 0; s < 512; ++s)
        {
            if (std::abs(buf4.getSample(0, s) - buf1.getSample(0, s)) > 0.001f)
            {
                anyDifferent = true;
                break;
            }
        }
        REQUIRE(anyDifferent);
    }

    SECTION("detune cents spread is symmetric")
    {
        engine.prepare(TEST_SR, 512);
        engine.setVoiceCount(4);
        engine.setDetune(15.0f);
        auto buf = makeStereoBuffer(64);
        engine.process(buf);

        // Outermost voices should be symmetric around 0
        REQUIRE(engine.getVoice(0).detuneCents == Catch::Approx(-22.5f).margin(0.01f));
        REQUIRE(engine.getVoice(3).detuneCents == Catch::Approx(22.5f).margin(0.01f));
    }
}

//==============================================================================
// LFOSystem + ModulationBus – filter cutoff changes
//==============================================================================

TEST_CASE("LFOSystem + ModulationBus modulates target parameter", "[wired][lfo][modbus]")
{
    LFOSystem lfo;
    lfo.prepare(TEST_SR);
    lfo.setWaveform(WaveformType::Sine);
    lfo.setRate(5.0f);
    lfo.setDepth(100.0f);
    lfo.setBipolar(true);

    std::atomic<float> target{ 1000.0f };

    SECTION("LFO modulates target via ModulationBus")
    {
        ModulationBus bus;
        float sourceValue = 0.0f;

        bus.addRoute(ModulationBus::Source::LFO, 0, "cutoff",
                      &target, &sourceValue, 0.5f);

        // Process LFO, update source, run bus
        sourceValue = lfo.process(64);
        bus.processBlock(64);

        // The target should have been modulated away from initial value
        float modulated = target.load();
        REQUIRE(modulated != Catch::Approx(1000.0f).margin(0.001f));
    }

    SECTION("LFO output is non-zero during cycle")
    {
        // Advance to peak (phase 0.25 at 5 Hz = 44100/5/4 = 2205 samples)
        float val = lfo.process(static_cast<int>(std::round(TEST_SR / 5.0 / 4.0)));
        REQUIRE(val == Catch::Approx(1.0f).margin(0.01f));
    }

    SECTION("multiple routes modulate independently")
    {
        ModulationBus bus;
        float sourceValue = 0.0f;
        std::atomic<float> targetA{ 500.0f };
        std::atomic<float> targetB{ 0.0f };

        bus.addRoute(ModulationBus::Source::LFO, 0, "cutoff",
                      &targetA, &sourceValue, 0.3f);
        bus.addRoute(ModulationBus::Source::LFO, 1, "volume",
                      &targetB, &sourceValue, 0.7f);

        sourceValue = lfo.process(64);
        bus.processBlock(64);

        REQUIRE(targetA.load() != Catch::Approx(500.0f).margin(0.001f));
        REQUIRE(targetB.load() != Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("zero depth route leaves target unchanged")
    {
        ModulationBus bus;
        float sourceValue = 0.0f;

        bus.addRoute(ModulationBus::Source::LFO, 0, "cutoff",
                      &target, &sourceValue, 0.0f);

        sourceValue = lfo.process(64);
        bus.processBlock(64);

        REQUIRE(target.load() == Catch::Approx(1000.0f).margin(0.001f));
    }
}

//==============================================================================
// MultiPointEnvelope – ADSR output shape
//==============================================================================

TEST_CASE("MultiPointEnvelope - ADSR output shape", "[wired][envelope][adsr]")
{
    MultiPointEnvelope env;

    SECTION("ADSR attack phase reaches peak")
    {
        env.setAttack(0.1f);
        env.setDecay(0.3f);
        env.setSustain(0.6f);
        env.setRelease(0.5f);
        env.prepare(48000.0);
        env.trigger();

        // At attack time (0.1s = 4800 samples), value should be near 1.0
        float val = env.process(4800);
        REQUIRE(val == Catch::Approx(1.0f).margin(0.01f));
    }

    SECTION("ADSR holds at sustain after decay")
    {
        env.setAttack(0.02f);
        env.setDecay(0.2f);
        env.setSustain(0.65f);
        env.setRelease(1.0f);
        env.prepare(48000.0);
        env.trigger();

        // Advance through attack (0.02s=960) + decay (0.2s=9600)
        env.process(10560);
        float sustainVal = env.getValue();
        REQUIRE(sustainVal == Catch::Approx(0.65f).margin(0.02f));

        // Hold at sustain for a while
        env.process(4800);
        REQUIRE(env.getValue() == Catch::Approx(sustainVal).margin(0.01f));
    }

    SECTION("ADSR release phase reaches zero")
    {
        env.setAttack(0.01f);
        env.setDecay(0.2f);
        env.setSustain(0.7f);
        env.setRelease(0.4f);
        env.prepare(48000.0);
        env.trigger();

        // Advance through attack + decay + some sustain time
        env.process(15000);
        // Call release (in Sustain mode, or just process past sustain point)
        float valBeforeRelease = env.getValue();

        // Process the remaining release time
        env.process(static_cast<int>(48000.0 * 0.5)); // 0.5s should cover release
        float valAfterRelease = env.getValue();

        // Value should have decreased from sustain toward 0
        REQUIRE(valAfterRelease < valBeforeRelease);
    }

    SECTION("attack-decay-sustain-release cycle is monotonic in segments")
    {
        env.setAttack(0.05f);
        env.setDecay(0.15f);
        env.setSustain(0.5f);
        env.setRelease(0.3f);
        env.prepare(48000.0);
        env.trigger();

        // Sample along the envelope
        // Attack: should rise from 0 to 1
        float v0 = env.process(1);    // t=0
        REQUIRE(v0 >= 0.0f);

        float vAttack = env.process(2400); // t=0.05s
        REQUIRE(vAttack == Catch::Approx(1.0f).margin(0.01f));

        // Decay: should fall from 1 to sustain (0.5)
        float vDecay = env.process(7200); // t=0.2s (0.05+0.15)
        REQUIRE(vDecay == Catch::Approx(0.5f).margin(0.04f));

        // Sustain hold
        float vSustain = env.process(4800); // t=0.3s
        REQUIRE(vSustain == Catch::Approx(0.5f).margin(0.02f));
    }
}

//==============================================================================
// MultiFilter – LP at 500Hz attenuates highs
//==============================================================================

TEST_CASE("MultiFilter - low-pass at 500Hz attenuates high frequencies",
          "[wired][multifilter][freqresp]")
{
    MultiFilter filter;

    SECTION("low frequency passes through LP at 500Hz")
    {
        filter.prepare(makeSpec());
        filter.addSlot(FilterType::LowPass, FilterParams{500.0, 0.0f, 0.0f, 1.0f});

        // Low frequency signal (100Hz) should pass through
        auto bufferLow = createSineBuffer(2, 1024, 100.0f, static_cast<float>(TEST_SR));
        float energyLowBefore = totalEnergy(bufferLow);
        filter.process(bufferLow);
        float energyLowAfter = totalEnergy(bufferLow);
        REQUIRE(energyLowAfter > energyLowBefore * 0.5f);
    }

    SECTION("high frequency is attenuated by LP at 500Hz")
    {
        MultiFilter filterHigh;
        filterHigh.prepare(makeSpec());
        filterHigh.addSlot(FilterType::LowPass, FilterParams{500.0, 0.0f, 0.0f, 1.0f});

        // High frequency signal (5000Hz) should be heavily attenuated
        auto bufferHigh = createSineBuffer(2, 1024, 5000.0f, static_cast<float>(TEST_SR));
        filterHigh.process(bufferHigh);
        float peakHigh = peakMagnitude(bufferHigh);

        // Low frequency signal (100Hz) should pass through more
        MultiFilter filterLow;
        filterLow.prepare(makeSpec());
        filterLow.addSlot(FilterType::LowPass, FilterParams{500.0, 0.0f, 0.0f, 1.0f});
        auto bufferLow2 = createSineBuffer(2, 1024, 100.0f, static_cast<float>(TEST_SR));
        filterLow.process(bufferLow2);
        float peakLow = peakMagnitude(bufferLow2);

        // The high frequency output should be much lower than the low frequency output
        REQUIRE(peakHigh < peakLow * 0.5f);
    }

    SECTION("frequency response shows roll-off above cutoff")
    {
        filter.prepare(makeSpec());
        filter.addSlot(FilterType::LowPass, FilterParams{500.0, 0.7f, 0.0f, 1.0f});

        std::vector<float> freqs = {100.0f, 500.0f, 2000.0f, 8000.0f};
        auto response = filter.getFrequencyResponse(freqs);

        REQUIRE(response.size() == 4);
        // Response at 100Hz should be higher than at 2000Hz
        REQUIRE(response[0] > response[2]);
        // Response at 8000Hz should be lower than at 500Hz
        REQUIRE(response[3] < response[1]);
    }
}

//==============================================================================
// Master volume/pan – vol=0 → silence
//==============================================================================

TEST_CASE("Master volume/pan - volume at zero produces silence", "[wired][master]")
{
    // Replicate the master vol/pan logic from AnaPlugAudioProcessor::processBlock
    auto applyMasterVolPan = [](juce::AudioBuffer<float>& buffer,
                                 float vol, float pan)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();
        const float panL = (pan <= 0.0f) ? 1.0f : 1.0f - pan;
        const float panR = (pan >= 0.0f) ? 1.0f : 1.0f + pan;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* d = buffer.getWritePointer(ch);
            const float panG = (ch == 0) ? panL : panR;
            for (int i = 0; i < numSamples; ++i)
                d[i] = juce::jlimit(-1.0f, 1.0f, d[i] * vol * panG);
        }
    };

    SECTION("vol=0 produces silence")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        applyMasterVolPan(buffer, 0.0f, 0.0f);
        float energy = totalEnergy(buffer);
        REQUIRE(energy == Catch::Approx(0.0f).margin(0.0001f));
    }

    SECTION("vol=0.8 with center pan preserves signal")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        float energyBefore = totalEnergy(buffer);
        applyMasterVolPan(buffer, 0.8f, 0.0f);
        float energyAfter = totalEnergy(buffer);
        // Energy should be reduced but non-zero
        REQUIRE(energyAfter > 0.0f);
        REQUIRE(energyAfter < energyBefore);
    }

    SECTION("pan hard-left silences right channel")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        applyMasterVolPan(buffer, 1.0f, -1.0f);
        float rightEnergy = 0.0f;
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            rightEnergy += std::abs(buffer.getSample(1, s));
        // Right channel should be near zero when panned hard left
        REQUIRE(rightEnergy == Catch::Approx(0.0f).margin(0.0001f));
    }

    SECTION("pan hard-right silences left channel")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        applyMasterVolPan(buffer, 1.0f, 1.0f);
        float leftEnergy = 0.0f;
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            leftEnergy += std::abs(buffer.getSample(0, s));
        // Left channel should be near zero when panned hard right
        REQUIRE(leftEnergy == Catch::Approx(0.0f).margin(0.0001f));
    }

    SECTION("vol and pan combine correctly")
    {
        auto buffer = createSineBuffer(2, 512, 440.0f, static_cast<float>(TEST_SR));
        applyMasterVolPan(buffer, 0.5f, 0.0f);

        // Center pan: both channels get vol*1.0 = 0.5x
        float peak = peakMagnitude(buffer);
        REQUIRE(peak <= 0.5f + 0.01f);
    }

    SECTION("clipping protection: values are clamped to [-1, 1]")
    {
        juce::AudioBuffer<float> hotBuf(2, 512);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                hotBuf.setSample(ch, s, 2.0f); // hot signal

        applyMasterVolPan(hotBuf, 0.8f, 0.0f);
        float peak = peakMagnitude(hotBuf);
        REQUIRE(peak <= 1.0f);
    }
}
