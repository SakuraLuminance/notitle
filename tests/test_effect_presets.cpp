#include <catch2/catch_all.hpp>
#include "dsp/PresetManager.h"
#include "dsp/PresetFactory.h"
#include "dsp/EffectsChain.h"
#include "dsp/STFTConfig.h"
#include "dsp/MultiFilter.h"
#include "dsp/MultiPointEnvelope.h"
#include "dsp/LFOSystem.h"
#include "dsp/UnisonEngine.h"
#include "dsp/VoiceManager.h"
#include "dsp/GranularSynthesizer.h"
#include "dsp/FilterModulation.h"
#include "dsp/effects/DelayEffect.h"
#include "dsp/effects/ChorusEffect.h"
#include "dsp/effects/FlangerEffect.h"
#include "dsp/effects/PhaserEffect.h"
#include "dsp/effects/DistortionEffect.h"
#include "dsp/effects/ReverbEffect.h"
#include "dsp/effects/EQEffect.h"
#include "dsp/effects/CompressorEffect.h"
#include "dsp/effects/LimiterEffect.h"
#include "dsp/effects/BitcrusherEffect.h"
#include "dsp/effects/SaturationEffect.h"
#include "dsp/effects/StereoWidenerEffect.h"
#include "dsp/effects/RingModulatorEffect.h"
#include "dsp/effects/AutoTuneEffect.h"

using namespace ana;

//==============================================================================
// Test helpers
//==============================================================================

/** Creates a temporary directory for test preset files. */
static juce::File getTestPresetDir()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("AnaPlug_TestEffectPresets_" + juce::String(__LINE__));
    dir.createDirectory();
    return dir;
}

/** Recursively deletes the temporary directory. */
static void cleanupTestDir(const juce::File& dir)
{
    if (dir.exists())
        dir.deleteRecursively();
}

/** Creates a minimal PresetManager with all state references default-constructed. */
struct TestHarness
{
    STFTConfig              stftConfig;
    MultiFilter             multiFilter;
    MultiPointEnvelope      envelope;
    LFOSystem               lfo;
    GranularSynthesizer     granular;
    UnisonEngine            unison;
    VoiceManager            voiceManager;
    FilterModulationSystem  filterMod;
    EffectsChain            effectsChain;
    PresetManager           presetManager;

    TestHarness()
    {
        presetManager.setStateReferences(&stftConfig, &multiFilter, &envelope,
                                          &lfo, &granular, &unison,
                                          &voiceManager, &filterMod);
        presetManager.setEffectsChain(&effectsChain);
    }
};

//==============================================================================
// Effect state serialization for individual effects
//==============================================================================

TEST_CASE("Effect state serialization - DelayEffect", "[effects][preset][serialize]")
{
    DelayEffect effect;
    effect.setDelayTime(150.0f);
    effect.setFeedback(0.45f);
    effect.setMix(60.0f);

    auto state = effect.getState();
    REQUIRE(state.hasType("DelayEffect"));
    REQUIRE(state.getProperty("delayMs") == 150.0f);

    DelayEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("delayMs") == 150.0f);
    REQUIRE(reState.getProperty("feedback") == Catch::Approx(0.45f));
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.6f));
}

TEST_CASE("Effect state serialization - ChorusEffect", "[effects][preset][serialize]")
{
    ChorusEffect effect;
    effect.setRate(2.5f);
    effect.setDepth(75.0f);
    effect.setCentreDelay(15.0f);
    effect.setFeedback(40.0f);
    effect.setMix(50.0f);

    auto state = effect.getState();
    REQUIRE(state.hasType("ChorusEffect"));

    ChorusEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("rate") == Catch::Approx(2.5f));
    REQUIRE(reState.getProperty("depth") == Catch::Approx(0.75f));
    REQUIRE(reState.getProperty("centreDelay") == Catch::Approx(15.0f));
    REQUIRE(reState.getProperty("feedback") == Catch::Approx(0.4f));
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.5f));
}

TEST_CASE("Effect state serialization - FlangerEffect", "[effects][preset][serialize]")
{
    FlangerEffect effect;
    effect.setRate(1.2f);
    effect.setDepth(0.8f);
    effect.setDelay(5.0f);
    effect.setFeedback(0.6f);
    effect.setMix(0.7f);
    effect.setBypass(true);
    effect.setGain(0.9f);

    auto state = effect.getState();
    REQUIRE(state.hasType("FlangerEffect"));

    FlangerEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("rate") == Catch::Approx(1.2f));
    REQUIRE(reState.getProperty("depth") == Catch::Approx(0.8f));
    REQUIRE(reState.getProperty("delay") == Catch::Approx(5.0f));
    REQUIRE(reState.getProperty("feedback") == Catch::Approx(0.6f));
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.7f));
    REQUIRE(reState.getProperty("bypass") == true);
    REQUIRE(reState.getProperty("gain") == Catch::Approx(0.9f));
}

TEST_CASE("Effect state serialization - PhaserEffect", "[effects][preset][serialize]")
{
    PhaserEffect effect;
    effect.setRate(2.0f);
    effect.setDepth(0.9f);
    effect.setFeedback(0.5f);
    effect.setStages(8);
    effect.setMix(0.6f);
    effect.setBypass(false);
    effect.setGain(1.0f);
    effect.setStereoPhaseOffset(45.0f);

    auto state = effect.getState();
    REQUIRE(state.hasType("PhaserEffect"));

    PhaserEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("rate") == Catch::Approx(2.0f));
    REQUIRE(reState.getProperty("depth") == Catch::Approx(0.9f));
    REQUIRE(reState.getProperty("feedback") == Catch::Approx(0.5f));
    REQUIRE(reState.getProperty("stages") == 8);
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.6f));
    REQUIRE(reState.getProperty("bypass") == false);
    REQUIRE(reState.getProperty("gain") == Catch::Approx(1.0f));
    REQUIRE(reState.getProperty("stereoPhaseOffset") == Catch::Approx(45.0f));
}

TEST_CASE("Effect state serialization - DistortionEffect", "[effects][preset][serialize]")
{
    DistortionEffect effect;
    effect.setType(DistortionType::Tube);
    effect.setDrive(70.0f);
    effect.setRange(30.0f);
    effect.setBlend(80.0f);
    effect.setVolume(90.0f);

    auto state = effect.getState();
    REQUIRE(state.hasType("DistortionEffect"));

    DistortionEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(static_cast<int>(reState.getProperty("type")) == static_cast<int>(DistortionType::Tube));
    REQUIRE(reState.getProperty("drive") == Catch::Approx(70.0f));
    REQUIRE(reState.getProperty("range") == Catch::Approx(30.0f));
    REQUIRE(reState.getProperty("blend") == Catch::Approx(80.0f));
    REQUIRE(reState.getProperty("volume") == Catch::Approx(90.0f));
}

TEST_CASE("Effect state serialization - ReverbEffect", "[effects][preset][serialize]")
{
    ReverbEffect effect;
    effect.setRoomSize(0.8f);
    effect.setDamping(0.4f);
    effect.setWetLevel(0.5f);
    effect.setDryLevel(0.5f);
    effect.setWidth(0.9f);

    auto state = effect.getState();
    REQUIRE(state.hasType("ReverbEffect"));

    ReverbEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("roomSize") == Catch::Approx(0.8f));
    REQUIRE(reState.getProperty("damping") == Catch::Approx(0.4f));
    REQUIRE(reState.getProperty("wetLevel") == Catch::Approx(0.5f));
    REQUIRE(reState.getProperty("dryLevel") == Catch::Approx(0.5f));
    REQUIRE(reState.getProperty("width") == Catch::Approx(0.9f));
}

TEST_CASE("Effect state serialization - EQEffect", "[effects][preset][serialize]")
{
    EQEffect effect;
    effect.setLowBand(200.0f, -3.0f, 0.5f);
    effect.setMidBand(1000.0f, 2.0f, 1.0f);
    effect.setHighBand(8000.0f, 1.5f, 0.707f);
    effect.setLowType(EQBandType::LowShelf);
    effect.setHighType(EQBandType::HighShelf);

    auto state = effect.getState();
    REQUIRE(state.hasType("EQEffect"));
    REQUIRE(state.getNumChildren() == 3);

    EQEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    // Check band 0 (low)
    auto band0 = reState.getChild(0);
    REQUIRE(static_cast<double>(band0.getProperty("frequency")) == Catch::Approx(200.0));
    REQUIRE(static_cast<double>(band0.getProperty("gain")) == Catch::Approx(-3.0));
    REQUIRE(static_cast<double>(band0.getProperty("q")) == Catch::Approx(0.5));
    REQUIRE(static_cast<int>(band0.getProperty("type")) == static_cast<int>(EQBandType::LowShelf));

    // Check band 1 (mid)
    auto band1 = reState.getChild(1);
    REQUIRE(static_cast<double>(band1.getProperty("frequency")) == Catch::Approx(1000.0));
    REQUIRE(static_cast<double>(band1.getProperty("gain")) == Catch::Approx(2.0));
    REQUIRE(static_cast<double>(band1.getProperty("q")) == Catch::Approx(1.0));

    // Check band 2 (high)
    auto band2 = reState.getChild(2);
    REQUIRE(static_cast<double>(band2.getProperty("frequency")) == Catch::Approx(8000.0));
    REQUIRE(static_cast<double>(band2.getProperty("gain")) == Catch::Approx(1.5));
    REQUIRE(static_cast<int>(band2.getProperty("type")) == static_cast<int>(EQBandType::HighShelf));
}

TEST_CASE("Effect state serialization - CompressorEffect", "[effects][preset][serialize]")
{
    CompressorEffect effect;
    effect.setThreshold(-30.0f);
    effect.setRatio(8.0f);
    effect.setAttack(5.0f);
    effect.setRelease(50.0f);
    effect.setKnee(3.0f);
    effect.setMakeupGain(6.0f);
    effect.setMix(0.8f);
    effect.setBypass(false);
    effect.setGain(1.0f);
    effect.setRMSMode(false);
    effect.setAutoMakeup(true);

    auto state = effect.getState();
    REQUIRE(state.hasType("CompressorEffect"));

    CompressorEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("threshold") == Catch::Approx(-30.0f));
    REQUIRE(reState.getProperty("ratio") == Catch::Approx(8.0f));
    REQUIRE(reState.getProperty("attack") == Catch::Approx(5.0f));
    REQUIRE(reState.getProperty("release") == Catch::Approx(50.0f));
    REQUIRE(reState.getProperty("knee") == Catch::Approx(3.0f));
    REQUIRE(reState.getProperty("makeupGain") == Catch::Approx(6.0f));
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.8f));
    REQUIRE(reState.getProperty("bypass") == false);
    REQUIRE(reState.getProperty("gain") == Catch::Approx(1.0f));
    REQUIRE(reState.getProperty("rmsMode") == false);
    REQUIRE(reState.getProperty("autoMakeup") == true);
}

TEST_CASE("Effect state serialization - LimiterEffect", "[effects][preset][serialize]")
{
    LimiterEffect effect;
    effect.setThreshold(-12.0f);
    effect.setAttack(0.5f);
    effect.setRelease(10.0f);
    effect.setLookahead(1.0f);
    effect.setMix(0.9f);
    effect.setBypass(true);
    effect.setGain(0.8f);
    effect.setOversampling(2);

    auto state = effect.getState();
    REQUIRE(state.hasType("LimiterEffect"));

    LimiterEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("threshold") == Catch::Approx(-12.0f));
    REQUIRE(reState.getProperty("attack") == Catch::Approx(0.5f));
    REQUIRE(reState.getProperty("release") == Catch::Approx(10.0f));
    REQUIRE(reState.getProperty("lookahead") == Catch::Approx(1.0f));
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.9f));
    REQUIRE(reState.getProperty("bypass") == true);
    REQUIRE(reState.getProperty("gain") == Catch::Approx(0.8f));
    REQUIRE(reState.getProperty("oversampling") == 2);
}

TEST_CASE("Effect state serialization - BitcrusherEffect", "[effects][preset][serialize]")
{
    BitcrusherEffect effect;
    effect.setBitDepth(12.0f);
    effect.setDownsample(4.0f);
    effect.setMix(0.7f);

    auto state = effect.getState();
    REQUIRE(state.hasType("BitcrusherEffect"));

    BitcrusherEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("bitDepth") == Catch::Approx(12.0f));
    REQUIRE(reState.getProperty("downsample") == Catch::Approx(4.0f));
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.7f));
}

TEST_CASE("Effect state serialization - SaturationEffect", "[effects][preset][serialize]")
{
    SaturationEffect effect;
    effect.setDrive(60.0f);
    effect.setTone(5000.0f);
    effect.setMode(SaturationMode::Tube);
    effect.setMix(80.0f);
    effect.setBypass(false);
    effect.setGain(1.2f);

    auto state = effect.getState();
    REQUIRE(state.hasType("SaturationEffect"));

    SaturationEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(static_cast<int>(reState.getProperty("mode")) == static_cast<int>(SaturationMode::Tube));
    REQUIRE(static_cast<double>(reState.getProperty("drive")) == Catch::Approx(60.0));
    REQUIRE(static_cast<double>(reState.getProperty("tone")) == Catch::Approx(5000.0));
    REQUIRE(static_cast<double>(reState.getProperty("mix")) == Catch::Approx(80.0));
    REQUIRE(reState.getProperty("bypass") == false);
    REQUIRE(static_cast<double>(reState.getProperty("gain")) == Catch::Approx(1.2));
}

TEST_CASE("Effect state serialization - StereoWidenerEffect", "[effects][preset][serialize]")
{
    StereoWidenerEffect effect;
    effect.setWidth(0.75f);
    effect.setMode(StereoWidenerMode::Wide);
    effect.setMix(0.6f);
    effect.setBypass(false);

    auto state = effect.getState();
    REQUIRE(state.hasType("StereoWidenerEffect"));

    StereoWidenerEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("width") == Catch::Approx(0.75f));
    REQUIRE(static_cast<int>(reState.getProperty("mode")) == static_cast<int>(StereoWidenerMode::Wide));
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.6f));
    REQUIRE(reState.getProperty("bypass") == false);
}

TEST_CASE("Effect state serialization - RingModulatorEffect", "[effects][preset][serialize]")
{
    RingModulatorEffect effect;
    effect.setFrequency(440.0f);
    effect.setWaveform(1);
    effect.setMix(0.4f);
    effect.setBypass(true);
    effect.setGain(0.7f);

    auto state = effect.getState();
    REQUIRE(state.hasType("RingModulatorEffect"));

    RingModulatorEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("frequency") == Catch::Approx(440.0f));
    REQUIRE(reState.getProperty("waveform") == 1);
    REQUIRE(reState.getProperty("mix") == Catch::Approx(0.4f));
    REQUIRE(reState.getProperty("bypass") == true);
    REQUIRE(reState.getProperty("gain") == Catch::Approx(0.7f));
}

TEST_CASE("Effect state serialization - AutoTuneEffect", "[effects][preset][serialize]")
{
    AutoTuneEffect effect;
    effect.setRetuneSpeed(25.0f);
    effect.setAmount(0.8f);
    effect.setEnabled(true);

    auto state = effect.getState();
    REQUIRE(state.hasType("AutoTuneEffect"));

    AutoTuneEffect loaded;
    loaded.setState(state);
    auto reState = loaded.getState();

    REQUIRE(reState.getProperty("retuneSpeed") == Catch::Approx(25.0f));
    REQUIRE(reState.getProperty("amount") == Catch::Approx(0.8f));
    REQUIRE(reState.getProperty("enabled") == true);
}

//==============================================================================
// Preset round-trip: set params -> save -> load -> verify ALL params match
//==============================================================================

TEST_CASE("Preset round-trip", "[effects][preset][roundtrip]")
{
    auto testDir = getTestPresetDir();
    auto presetFile = testDir.getChildFile("RoundTripTest.anaplug");

    {
        TestHarness harness;

        // Add two effects (EffectBase subclasses) with distinct parameters
        auto flanger = std::make_unique<FlangerEffect>();
        flanger->setRate(1.5f);
        flanger->setDepth(0.7f);
        flanger->setDelay(4.0f);
        flanger->setFeedback(0.5f);
        flanger->setMix(0.6f);
        flanger->setBypass(false);
        flanger->setGain(1.0f);

        auto crusher = std::make_unique<BitcrusherEffect>();
        crusher->setBitDepth(6.0f);
        crusher->setDownsample(3.0f);
        crusher->setMix(0.8f);

        harness.effectsChain.addEffect(std::move(flanger), "Flanger");
        harness.effectsChain.addEffect(std::move(crusher), "Bitcrusher");

        // Save to temp file
        REQUIRE(harness.presetManager.savePresetToFile(presetFile));
    }

    {
        TestHarness harness;

        // Load from temp file — the chain is empty, so deserialisation
        // must reconstruct both effects via ProcessorStore.
        REQUIRE(harness.presetManager.loadPresetFromFile(presetFile));

        // Chain should now have 2 effects reconstructed from XML
        REQUIRE(harness.effectsChain.getNumEffects() == 2);

        // Verify FlangerEffect parameters
        auto& slot0 = harness.effectsChain.getEffect(0);
        auto flangerState = slot0.effect->getState();
        REQUIRE(flangerState.getProperty("rate") == Catch::Approx(1.5f));
        REQUIRE(flangerState.getProperty("depth") == Catch::Approx(0.7f));
        REQUIRE(flangerState.getProperty("delay") == Catch::Approx(4.0f));
        REQUIRE(flangerState.getProperty("feedback") == Catch::Approx(0.5f));
        REQUIRE(flangerState.getProperty("mix") == Catch::Approx(0.6f));
        REQUIRE(flangerState.getProperty("bypass") == false);
        REQUIRE(flangerState.getProperty("gain") == Catch::Approx(1.0f));

        // Verify BitcrusherEffect parameters
        auto& slot1 = harness.effectsChain.getEffect(1);
        auto crusherState = slot1.effect->getState();
        REQUIRE(crusherState.getProperty("bitDepth") == Catch::Approx(6.0f));
        REQUIRE(crusherState.getProperty("downsample") == Catch::Approx(3.0f));
        REQUIRE(crusherState.getProperty("mix") == Catch::Approx(0.8f));
    }

    cleanupTestDir(testDir);
}

//==============================================================================
// Factory presets load — "Slapback" delay preset = 80ms
//==============================================================================

TEST_CASE("Factory presets load", "[effects][preset][factory]")
{
    auto slapback = PresetFactory::getFactoryPreset("Delay", "Slapback");
    REQUIRE(slapback.isValid());
    REQUIRE(slapback.hasType("DelayEffect"));
    REQUIRE(static_cast<double>(slapback.getProperty("delayMs")) == Catch::Approx(80.0));

    // Loading the factory preset ValueTree into a DelayEffect should set 80ms
    DelayEffect effect;
    effect.setState(slapback);

    auto state = effect.getState();
    REQUIRE(state.getProperty("delayMs") == 80.0f);
    REQUIRE(static_cast<double>(state.getProperty("feedback")) == Catch::Approx(0.2));
    REQUIRE(static_cast<double>(state.getProperty("mix")) == Catch::Approx(0.3));
}

//==============================================================================
// Format version bump — verify saved preset has version="1.1"
//==============================================================================

TEST_CASE("Format version bump", "[effects][preset][version]")
{
    auto testDir = getTestPresetDir();
    auto presetFile = testDir.getChildFile("VersionTest.anaplug");

    {
        TestHarness harness;
        harness.effectsChain.addEffect(std::make_unique<FlangerEffect>(), "Flanger");

        REQUIRE(harness.presetManager.savePresetToFile(presetFile));
    }

    // Read the raw XML and check the version attribute
    auto xml = juce::XmlDocument::parse(presetFile);
    REQUIRE(xml != nullptr);
    REQUIRE(xml->hasTagName("AnaPlugPreset"));
    REQUIRE(xml->getStringAttribute("Version") == "1.2");

    // Also verify that the parsed ValueTree has Version="1.2"
    auto tree = juce::ValueTree::fromXml(*xml);
    REQUIRE(tree.getProperty("Version").toString() == "1.2");

    cleanupTestDir(testDir);
}

//==============================================================================
// Old preset backward compat — preset without <Effects> → no crash
//==============================================================================

TEST_CASE("Old preset backward compat", "[effects][preset][backward]")
{
    auto testDir = getTestPresetDir();

    // Create an old-format preset that has NO <Effects> section
    {
        juce::ValueTree root("AnaPlugPreset");
        root.setProperty("Name", "OldStyle", nullptr);
        root.setProperty("Category", "Bass", nullptr);
        root.setProperty("Version", "1.0", nullptr);

        juce::ValueTree params("Parameters");
        // Only an STFTConfig section, no Effects
        juce::ValueTree stft("STFTConfig");
        stft.setProperty("FFTSize", 1024, nullptr);
        params.addChild(stft, 0, nullptr);
        root.addChild(params, 0, nullptr);

        auto xml = root.createXml();
        auto file = testDir.getChildFile("OldStyle.anaplug");
        juce::FileOutputStream stream(file);
        REQUIRE(stream.openedOk());
        xml->writeTo(stream, juce::XmlElement::TextFormat());
    }

    {
        TestHarness harness;

        harness.effectsChain.addEffect(std::make_unique<FlangerEffect>(), "Flanger");

        // Loading the old-style preset (no <Effects> section) should NOT crash
        auto file = testDir.getChildFile("OldStyle.anaplug");
        REQUIRE_NOTHROW(harness.presetManager.loadPresetFromFile(file));
    }

    cleanupTestDir(testDir);
}

//==============================================================================
// Missing Effects section — load old preset → effects retain current state
//==============================================================================

TEST_CASE("Missing Effects section", "[effects][preset][missing]")
{
    auto testDir = getTestPresetDir();

    // Create a preset without <Effects> child
    {
        juce::ValueTree root("AnaPlugPreset");
        root.setProperty("Name", "NoEffects", nullptr);
        root.setProperty("Category", "Pad", nullptr);
        root.setProperty("Version", "1.0", nullptr);

        juce::ValueTree params("Parameters");
        // Only LFO section, no Effects
        juce::ValueTree lfoTree("LFO");
        lfoTree.setProperty("RateHz", 5.0f, nullptr);
        params.addChild(lfoTree, 0, nullptr);
        root.addChild(params, 0, nullptr);

        auto xml = root.createXml();
        auto file = testDir.getChildFile("NoEffects.anaplug");
        juce::FileOutputStream stream(file);
        REQUIRE(stream.openedOk());
        xml->writeTo(stream, juce::XmlElement::TextFormat());
    }

    {
        TestHarness harness;

        // Add a Flanger with a specific state
        auto flanger = std::make_unique<FlangerEffect>();
        flanger->setRate(3.0f);
        flanger->setDepth(0.9f);
        flanger->setFeedback(0.7f);
        harness.effectsChain.addEffect(std::move(flanger), "Flanger");

        // Load the preset (which has no <Effects> section)
        auto file = testDir.getChildFile("NoEffects.anaplug");
        REQUIRE_NOTHROW(harness.presetManager.loadPresetFromFile(file));

        // The flanger should still have its original state since there's no <Effects> section
        auto& slot0 = harness.effectsChain.getEffect(0);
        auto state = slot0.effect->getState();
        REQUIRE(state.getProperty("rate") == Catch::Approx(3.0f));
        REQUIRE(state.getProperty("depth") == Catch::Approx(0.9f));
        REQUIRE(state.getProperty("feedback") == Catch::Approx(0.7f));
    }

    cleanupTestDir(testDir);
}

//==============================================================================
// Multiple slot serialization — 3 effects with distinct params
//==============================================================================

TEST_CASE("Multiple slot serialization", "[effects][preset][multislot]")
{
    auto testDir = getTestPresetDir();
    auto presetFile = testDir.getChildFile("MultiSlot.anaplug");

    {
        TestHarness harness;

        // Effect 1: Flanger with distinct params
        auto flanger = std::make_unique<FlangerEffect>();
        flanger->setRate(0.8f);
        flanger->setDepth(0.6f);
        flanger->setDelay(2.5f);
        flanger->setFeedback(0.4f);
        flanger->setMix(0.5f);
        flanger->setBypass(false);
        flanger->setGain(1.0f);

        // Effect 2: Compressor with distinct params
        auto comp = std::make_unique<CompressorEffect>();
        comp->setThreshold(-18.0f);
        comp->setRatio(6.0f);
        comp->setAttack(3.0f);
        comp->setRelease(80.0f);
        comp->setKnee(6.0f);
        comp->setMakeupGain(3.0f);
        comp->setMix(0.75f);
        comp->setRMSMode(true);

        // Effect 3: Bitcrusher with distinct params
        auto crusher = std::make_unique<BitcrusherEffect>();
        crusher->setBitDepth(4.0f);
        crusher->setDownsample(8.0f);
        crusher->setMix(0.5f);

        harness.effectsChain.addEffect(std::move(flanger), "Flanger");
        harness.effectsChain.addEffect(std::move(comp), "Compressor");
        harness.effectsChain.addEffect(std::move(crusher), "Bitcrusher");

        REQUIRE(harness.presetManager.savePresetToFile(presetFile));
    }

    {
        TestHarness harness;

        // Load from temp file — deserialisation will reconstruct all 3
        // effects via ProcessorStore from the serialised XML.
        REQUIRE(harness.presetManager.loadPresetFromFile(presetFile));

        // Chain should be fully reconstructed
        REQUIRE(harness.effectsChain.getNumEffects() == 3);

        // Verify Flanger params
        {
            auto& slot = harness.effectsChain.getEffect(0);
            auto state = slot.effect->getState();
            REQUIRE(state.getProperty("rate") == Catch::Approx(0.8f));
            REQUIRE(state.getProperty("depth") == Catch::Approx(0.6f));
            REQUIRE(state.getProperty("delay") == Catch::Approx(2.5f));
            REQUIRE(state.getProperty("feedback") == Catch::Approx(0.4f));
            REQUIRE(state.getProperty("mix") == Catch::Approx(0.5f));
            REQUIRE(state.getProperty("bypass") == false);
            REQUIRE(state.getProperty("gain") == Catch::Approx(1.0f));
        }

        // Verify Compressor params
        {
            auto& slot = harness.effectsChain.getEffect(1);
            auto state = slot.effect->getState();
            REQUIRE(state.getProperty("threshold") == Catch::Approx(-18.0f));
            REQUIRE(state.getProperty("ratio") == Catch::Approx(6.0f));
            REQUIRE(state.getProperty("attack") == Catch::Approx(3.0f));
            REQUIRE(state.getProperty("release") == Catch::Approx(80.0f));
            REQUIRE(state.getProperty("knee") == Catch::Approx(6.0f));
            REQUIRE(state.getProperty("makeupGain") == Catch::Approx(3.0f));
            REQUIRE(state.getProperty("mix") == Catch::Approx(0.75f));
            REQUIRE(state.getProperty("rmsMode") == true);
        }

        // Verify Bitcrusher params
        {
            auto& slot = harness.effectsChain.getEffect(2);
            auto state = slot.effect->getState();
            REQUIRE(state.getProperty("bitDepth") == Catch::Approx(4.0f));
            REQUIRE(state.getProperty("downsample") == Catch::Approx(8.0f));
            REQUIRE(state.getProperty("mix") == Catch::Approx(0.5f));
        }
    }

    cleanupTestDir(testDir);
}
