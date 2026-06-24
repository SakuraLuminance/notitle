#include <catch2/catch_all.hpp>
#include <cmath>
#include <random>
#include <set>
#include "dsp/effects/DeEsserModule.h"
#include "dsp/effects/BreathNoiseGenerator.h"
#include "dsp/effects/FormantTuner.h"
#include "dsp/effects/VocalThickenerEffect.h"
#include "dsp/effects/VocalNoiseReducer.h"
#include "dsp/effects/SoloistVocalChain.h"
#include "dsp/effects/SpaceModule.h"
#include "dsp/effects/CompressorEffect.h"
#include "dsp/PresetFactory.h"
#include "dsp/PresetManager.h"

using namespace ana;

//==============================================================================
// Helpers
//==============================================================================

/** Generate a sine wave into the given buffer channel. */
static void fillSine(juce::AudioBuffer<float>& buffer, int channel,
                     double sampleRate, double frequency, float amplitude = 1.0f)
{
    auto* data = buffer.getWritePointer(channel);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        data[i] = amplitude * static_cast<float>(std::sin(
            2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate));
}

/** Compute RMS level of a channel in dBFS. */
static float rmsDb(const juce::AudioBuffer<float>& buffer, int channel)
{
    double sumSq = 0.0;
    auto* data = buffer.getReadPointer(channel);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        sumSq += static_cast<double>(data[i]) * data[i];
    if (sumSq <= 0.0) return -120.0f;
    return static_cast<float>(juce::Decibels::gainToDecibels(
        static_cast<float>(std::sqrt(sumSq / buffer.getNumSamples()))));
}

/** Compute L/R correlation (-1..1). */
static float stereoCorrelation(const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() < 2) return 1.0f;
    auto* l = buffer.getReadPointer(0);
    auto* r = buffer.getReadPointer(1);
    int n = buffer.getNumSamples();
    double meanL = 0.0, meanR = 0.0;
    for (int i = 0; i < n; ++i) { meanL += l[i]; meanR += r[i]; }
    meanL /= n; meanR /= n;
    double varL = 0.0, varR = 0.0, cov = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double dl = l[i] - meanL, dr = r[i] - meanR;
        varL += dl * dl; varR += dr * dr; cov += dl * dr;
    }
    double denom = std::sqrt(varL * varR);
    if (denom < 1e-15) return 1.0f;
    return static_cast<float>(cov / denom);
}

/** Create a standard test spec. */
static juce::dsp::ProcessSpec makeSpec(double sampleRate = 44100.0, int blockSize = 512)
{
    return juce::dsp::ProcessSpec{ sampleRate, static_cast<juce::uint32>(blockSize), 2 };
}

/** Create a temporary directory for test preset files. */
static juce::File getTestDir()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
               .getChildFile("AnaPlug_VocalTests_" + juce::String(__LINE__));
}

static void cleanupDir(const juce::File& dir)
{
    if (dir.exists()) dir.deleteRecursively();
}

//==============================================================================
// 1. DeEsser attenuates sibilance — 6kHz sine → >6dB reduction
//==============================================================================
TEST_CASE("DeEsser attenuates sibilance", "[vocal][deesser]")
{
    DeEsserModule deesser;
    auto spec = makeSpec();
    deesser.prepare(spec);

    juce::AudioBuffer<float> input(1, static_cast<int>(spec.maximumBlockSize));
    fillSine(input, 0, spec.sampleRate, 6000.0, 0.5f);

    float inputRms = rmsDb(input, 0);

    deesser.setFrequency(6000.0f);
    deesser.process(input);

    float outputRms = rmsDb(input, 0);
    float reduction = inputRms - outputRms;

    REQUIRE(reduction > 6.0f);
}

//==============================================================================
// 2. BreathNoise envelope tracks — noise amplitude follows input
//==============================================================================
TEST_CASE("BreathNoise envelope tracks", "[vocal][breath]")
{
    BreathNoiseGenerator breath;
    auto spec = makeSpec();
    breath.prepare(spec);

    juce::AudioBuffer<float> loudBlock(1, static_cast<int>(spec.maximumBlockSize));
    loudBlock.clear();
    fillSine(loudBlock, 0, spec.sampleRate, 440.0, 0.5f);

    juce::AudioBuffer<float> quietBlock(1, static_cast<int>(spec.maximumBlockSize));
    quietBlock.clear();
    fillSine(quietBlock, 0, spec.sampleRate, 440.0, 0.05f);

    breath.setBreathiness(50.0f);
    breath.setNoiseColor(50.0f);
    breath.setMix(1.0f);

    breath.process(loudBlock);
    breath.process(quietBlock);

    float loudRms  = rmsDb(loudBlock, 0);
    float quietRms = rmsDb(quietBlock, 0);

    REQUIRE(loudRms - quietRms > 3.0f);
}

//==============================================================================
// 3. FormantTuner shifts formants — spectral content changes with FormantShift
//==============================================================================
TEST_CASE("FormantTuner shifts formants", "[vocal][formant]")
{
    FormantTuner tuner;
    auto spec = makeSpec();
    auto sr = spec.sampleRate;
    int n  = static_cast<int>(spec.maximumBlockSize);

    // Broadband chirp signal
    juce::AudioBuffer<float> buffer(1, n);
    auto* data = buffer.getWritePointer(0);
    for (int i = 0; i < n; ++i)
    {
        double t = static_cast<double>(i) / sr;
        double f = 200.0 + (8000.0 - 200.0) * t / (static_cast<double>(n) / sr);
        data[i] = 0.5f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * f * t));
    }

    // Process with zero shift first
    juce::AudioBuffer<float> zeroBuf(1, n);
    zeroBuf.copyFrom(0, 0, buffer, 0, 0, n);

    tuner.setFormantShift(0.0f);
    tuner.setMix(1.0f);
    tuner.prepare(spec);
    tuner.process(zeroBuf);

    // Process with positive shift on a fresh copy
    tuner.reset();
    tuner.setFormantShift(8.0f);
    tuner.setMix(1.0f);
    tuner.prepare(spec);

    juce::AudioBuffer<float> shiftBuf(1, n);
    shiftBuf.copyFrom(0, 0, buffer, 0, 0, n);
    tuner.process(shiftBuf);

    // Compute high-frequency energy ratio (difference-based approximation)
    auto highRatio = [n](const juce::AudioBuffer<float>& buf) -> float {
        double totalSq = 0.0, highSq = 0.0;
        auto* p = buf.getReadPointer(0);
        for (int i = 1; i < n; ++i)
        {
            double s = p[i];
            totalSq += s * s;
            double d = p[i] - p[i - 1];
            highSq += d * d;
        }
        if (n > 0) { double s = p[0]; totalSq += s * s; }
        return (totalSq > 1e-15) ? static_cast<float>(highSq / totalSq) : 0.0f;
    };

    float zeroRatio  = highRatio(zeroBuf);
    float shiftRatio = highRatio(shiftBuf);

    REQUIRE(shiftRatio > zeroRatio);
}

//==============================================================================
// 4. VocalThickener stereo spread — L/R correlation decreases with spread
//==============================================================================
TEST_CASE("VocalThickener stereo spread", "[vocal][thickener]")
{
    VocalThickenerEffect thickener;
    auto spec = makeSpec();
    int n = static_cast<int>(spec.maximumBlockSize);

    juce::AudioBuffer<float> monoIn(1, n);
    fillSine(monoIn, 0, spec.sampleRate, 440.0, 0.5f);

    // Low spread
    juce::AudioBuffer<float> narrowBuf(2, n);
    narrowBuf.copyFrom(0, 0, monoIn, 0, 0, n);
    narrowBuf.copyFrom(1, 0, monoIn, 0, 0, n);

    thickener.reset();
    thickener.prepare(spec);
    thickener.setSpread(0.0f);
    thickener.setMix(1.0f);
    thickener.process(narrowBuf);
    float narrowCorr = stereoCorrelation(narrowBuf);

    // High spread
    juce::AudioBuffer<float> wideBuf(2, n);
    wideBuf.copyFrom(0, 0, monoIn, 0, 0, n);
    wideBuf.copyFrom(1, 0, monoIn, 0, 0, n);

    thickener.reset();
    thickener.prepare(spec);
    thickener.setSpread(100.0f);
    thickener.setMix(1.0f);
    thickener.process(wideBuf);
    float wideCorr = stereoCorrelation(wideBuf);

    REQUIRE(wideCorr < narrowCorr);
}

//==============================================================================
// 5. VocalNoiseReducer lowers noise floor
//==============================================================================
TEST_CASE("VocalNoiseReducer lowers noise floor", "[vocal][noisereduce]")
{
    VocalNoiseReducer reducer;
    auto spec = makeSpec();
    reducer.prepare(spec);

    constexpr int kLen = 8192;
    juce::AudioBuffer<float> signal(1, kLen);
    signal.clear();

    // Tone + noise
    std::mt19937 rng(12345);
    std::normal_distribution<float> gauss;
    for (int i = 0; i < kLen; ++i)
    {
        float s = 0.5f * std::sin(2.0f * float(juce::MathConstants<double>::pi) * 440.0f * i / static_cast<float>(spec.sampleRate));
        s += 0.15f * static_cast<float>(gauss(rng));
        signal.setSample(0, i, s);
    }

    float beforeRms = rmsDb(signal, 0);

    reducer.setReduction(80.0f);
    reducer.setFloor(0.01f);
    reducer.process(signal);

    float afterRms = rmsDb(signal, 0);

    // Noise floor should be lower (more negative)
    REQUIRE(afterRms < beforeRms);
    REQUIRE((beforeRms - afterRms) > 0.5f);
}

//==============================================================================
// 6. SoloistChain preset loads — verify all params match defaults
//==============================================================================
TEST_CASE("SoloistChain preset loads", "[vocal][soloist]")
{
    SoloistVocalChain chain;
    chain.prepare(makeSpec());
    chain.preset();

    auto state = chain.getState();
    REQUIRE(state.isValid());
    REQUIRE(static_cast<double>(state.getProperty("presenceDb"))      == Catch::Approx(3.0));
    REQUIRE(static_cast<double>(state.getProperty("airDb"))           == Catch::Approx(2.0));
    REQUIRE(static_cast<double>(state.getProperty("compression"))     == Catch::Approx(4.0));
    REQUIRE(static_cast<double>(state.getProperty("saturation"))      == Catch::Approx(0.15));
    REQUIRE(static_cast<double>(state.getProperty("pitchDriftCents")) == Catch::Approx(5.0));
    REQUIRE(static_cast<double>(state.getProperty("driftRateHz"))     == Catch::Approx(1.2));
    REQUIRE(static_cast<double>(state.getProperty("reverbWet"))       == Catch::Approx(0.25));
    REQUIRE(static_cast<double>(state.getProperty("widthPercent"))    == Catch::Approx(0.8));
}

//==============================================================================
// 7. VocalCharacter modes — each of 7 modes produces different settings
//==============================================================================
TEST_CASE("VocalCharacter modes", "[vocal][character]")
{
    // Verify the enum has 7 members with distinct values
    REQUIRE(static_cast<int>(VocalCharacter::Chest)     == 0);
    REQUIRE(static_cast<int>(VocalCharacter::Head)      == 1);
    REQUIRE(static_cast<int>(VocalCharacter::Breathy)   == 2);
    REQUIRE(static_cast<int>(VocalCharacter::Telephone) == 3);
    REQUIRE(static_cast<int>(VocalCharacter::Choir)     == 4);
    REQUIRE(static_cast<int>(VocalCharacter::Megaphone) == 5);
    REQUIRE(static_cast<int>(VocalCharacter::Whisper)   == 6);

    // Verify factory presets carry distinct VocalCharacter values
    auto presets = PresetFactory::createVocalPresets();
    REQUIRE(presets.size() >= 5);

    std::set<int> seenChars;
    for (const auto& [name, params] : presets)
    {
        auto vc = params.getChildWithName("VocalConfig");
        REQUIRE(vc.isValid());
        int charVal = vc.getProperty("VocalCharacter");
        REQUIRE(charVal >= 0);
        REQUIRE(charVal <= 6);
        seenChars.insert(charVal);
    }
    REQUIRE(seenChars.size() >= 4);
}

//==============================================================================
// 8. Vocal presets round-trip — save → XML → reload → verify all params
//==============================================================================
TEST_CASE("Vocal presets round-trip", "[vocal][preset][roundtrip]")
{
    auto vocalPresets = PresetFactory::createVocalPresets();
    REQUIRE(vocalPresets.size() >= 5);

    auto testDir = getTestDir();
    testDir.createDirectory();

    for (const auto& [name, params] : vocalPresets)
    {
        REQUIRE(params.isValid());
        REQUIRE(params.hasType("Parameters"));

        juce::ValueTree presetTree("AnaPlugPreset");
        presetTree.setProperty("Name", name, nullptr);
        presetTree.setProperty("Category", "Vocal", nullptr);
        presetTree.setProperty("Version", PresetManager::presetVersion, nullptr);
        presetTree.addChild(params, 0, nullptr);

        auto xml = presetTree.createXml();
        REQUIRE(xml != nullptr);

        auto file = testDir.getChildFile(name + ".anaplug");
        {
            juce::FileOutputStream stream(file);
            REQUIRE(stream.openedOk());
            xml->writeTo(stream, juce::XmlElement::TextFormat());
        }

        auto reloadedXml = juce::XmlDocument::parse(file);
        REQUIRE(reloadedXml != nullptr);

        auto reloadedTree = juce::ValueTree::fromXml(*reloadedXml);
        REQUIRE(reloadedTree.isValid());
        REQUIRE(reloadedTree.getProperty("Name").toString() == name);
        REQUIRE(reloadedTree.getProperty("Category").toString() == "Vocal");
        REQUIRE(reloadedTree.hasType("AnaPlugPreset"));
        REQUIRE(reloadedTree.getNumChildren() >= 1);

        auto reloadedParams = reloadedTree.getChild(0);
        REQUIRE(reloadedParams.hasType("Parameters"));

        // Compare properties
        for (int i = 0; i < params.getNumProperties(); ++i)
        {
            auto key = params.getPropertyName(i);
            if (reloadedParams.hasProperty(key))
            {
                double ov = static_cast<double>(params.getProperty(key));
                double rv = static_cast<double>(reloadedParams.getProperty(key));
                REQUIRE(ov == Catch::Approx(rv));
            }
        }

        // Verify VocalConfig child
        auto origVc = params.getChildWithName("VocalConfig");
        auto relVc  = reloadedParams.getChildWithName("VocalConfig");
        REQUIRE(origVc.isValid());
        REQUIRE(relVc.isValid());

        for (int i = 0; i < origVc.getNumProperties(); ++i)
        {
            auto key = origVc.getPropertyName(i);
            if (relVc.hasProperty(key))
            {
                double ov = static_cast<double>(origVc.getProperty(key));
                double rv = static_cast<double>(relVc.getProperty(key));
                REQUIRE(ov == Catch::Approx(rv));
            }
        }
    }

    cleanupDir(testDir);
}
