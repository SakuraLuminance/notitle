#include <catch2/catch_all.hpp>
#include "../src/dsp/PresetManager.h"
#include "../src/dsp/PresetFactory.h"
#include "../src/dsp/ProcessorStore.h"
#include "../src/dsp/EffectsChain.h"
#include "../src/dsp/STFTConfig.h"
#include "../src/dsp/MultiFilter.h"
#include "../src/dsp/MultiPointEnvelope.h"
#include "../src/dsp/LFOSystem.h"
#include "../src/dsp/UnisonEngine.h"
#include "../src/dsp/VoiceManager.h"
#include "../src/dsp/FilterModulation.h"
#include "../src/dsp/GranularSynthesizer.h"
#include "../src/dsp/effects/ConsolidatedDelay.h"
#include "../src/dsp/effects/DriveModule.h"
#include "../src/dsp/effects/DynamicsModule.h"
#include "../src/dsp/effects/EQModule.h"
#include "../src/dsp/effects/ModulationModule.h"
#include "../src/dsp/effects/SpaceModule.h"
#include "../src/dsp/effects/PitchModule.h"

using namespace ana;

//==============================================================================
// Test helpers
//==============================================================================

/** Creates a temporary directory for test preset files. */
static juce::File getTestPresetDir()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("AnaPlug_TestConsolidated_" + juce::String(__LINE__));
    dir.createDirectory();
    return dir;
}

/** Recursively deletes the temporary directory. */
static void cleanupTestDir(const juce::File& dir)
{
    if (dir.exists())
        dir.deleteRecursively();
}

/** Test harness with all state references for PresetManager. */
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
// Consolidated module factory preset list tests
//==============================================================================

TEST_CASE("ConsolidatedDelay - factory preset names are registered",
          "[effects][consolidated][factory]")
{
    auto names = PresetFactory::getModuleFactoryPresets("ConsolidatedDelay");
    REQUIRE_FALSE(names.isEmpty());
    REQUIRE(names.size() == 12);
    REQUIRE(names[0] == "Simple Mono");
    REQUIRE(names[1] == "Slapback Mono");
    REQUIRE(names[10] == "Ducked Delay");
    REQUIRE(names[11] == "Rhythm Echo");
}

TEST_CASE("DriveModule - factory preset names are registered",
          "[effects][consolidated][factory]")
{
    auto names = PresetFactory::getModuleFactoryPresets("DriveModule");
    REQUIRE_FALSE(names.isEmpty());
    REQUIRE(names.size() == 14);
    REQUIRE(names[0] == "Warm Drive");
    REQUIRE(names[6] == "Console Drive");
    REQUIRE(names[13] == "Bell Tone");
}

TEST_CASE("DynamicsModule - factory preset names are registered",
          "[effects][consolidated][factory]")
{
    auto names = PresetFactory::getModuleFactoryPresets("DynamicsModule");
    REQUIRE_FALSE(names.isEmpty());
    REQUIRE(names.size() == 6);
    REQUIRE(names[0] == "Gentle Comp");
    REQUIRE(names[3] == "Brick Wall");
    REQUIRE(names[5] == "Noise Gate");
}

TEST_CASE("EQModule - factory preset names are registered",
          "[effects][consolidated][factory]")
{
    auto names = PresetFactory::getModuleFactoryPresets("EQModule");
    REQUIRE_FALSE(names.isEmpty());
    REQUIRE(names.size() == 8);
    REQUIRE(names[0] == "Scoop");
    REQUIRE(names[7] == "Wide Q Cut");
}

TEST_CASE("ModulationModule - factory preset names are registered",
          "[effects][consolidated][factory]")
{
    auto names = PresetFactory::getModuleFactoryPresets("ModulationModule");
    REQUIRE_FALSE(names.isEmpty());
    REQUIRE(names.size() == 6);
    REQUIRE(names[0] == "Subtle Chorus");
    REQUIRE(names[5] == "Phase Bubble");
}

TEST_CASE("SpaceModule - factory preset names are registered",
          "[effects][consolidated][factory]")
{
    auto names = PresetFactory::getModuleFactoryPresets("SpaceModule");
    REQUIRE_FALSE(names.isEmpty());
    REQUIRE(names.size() == 10);
    REQUIRE(names[0] == "Small Room");
    REQUIRE(names[9] == "Extreme Wide");
}

TEST_CASE("PitchModule - factory preset names are registered",
          "[effects][consolidated][factory]")
{
    auto names = PresetFactory::getModuleFactoryPresets("PitchModule");
    REQUIRE_FALSE(names.isEmpty());
    REQUIRE(names.size() == 8);
    REQUIRE(names[0] == "Soft Tune");
    REQUIRE(names[7] == "Robot Formant");
}

TEST_CASE("Unknown module type returns empty array",
          "[effects][consolidated][factory]")
{
    auto names = PresetFactory::getModuleFactoryPresets("NonExistent");
    REQUIRE(names.isEmpty());
}

//==============================================================================
// Individual factory preset round-trip tests
//==============================================================================

TEST_CASE("ConsolidatedDelay - Slapback Mono preset loads correctly",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("ConsolidatedDelay", "Slapback Mono");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("ConsolidatedDelay"));

    // Set state on a ConsolidatedDelay and verify params
    ConsolidatedDelay delay;
    delay.setState(preset);
    auto state = delay.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 0); // Mono
    REQUIRE(static_cast<double>(state.getProperty("timeMs")) == Catch::Approx(80.0));
    REQUIRE(static_cast<double>(state.getProperty("feedback")) == Catch::Approx(0.15));
    REQUIRE(static_cast<double>(state.getProperty("mix")) == Catch::Approx(0.3));
}

TEST_CASE("ConsolidatedDelay - Bouncing Echo ping-pong preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("ConsolidatedDelay", "Bouncing Echo");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("ConsolidatedDelay"));

    ConsolidatedDelay delay;
    delay.setState(preset);
    auto state = delay.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 2); // PingPong
    REQUIRE(static_cast<double>(state.getProperty("timeMs")) == Catch::Approx(350.0));
    REQUIRE(static_cast<double>(state.getProperty("feedback")) == Catch::Approx(0.65));
    REQUIRE(static_cast<double>(state.getProperty("mix")) == Catch::Approx(0.6));
}

TEST_CASE("DriveModule - Warm Drive soft-clip preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("DriveModule", "Warm Drive");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("DriveModule"));

    DriveModule drive;
    drive.setState(preset);
    auto state = drive.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 0); // Soft
    REQUIRE(static_cast<double>(state.getProperty("drive")) == Catch::Approx(0.35));
    REQUIRE(static_cast<double>(state.getProperty("tone")) == Catch::Approx(0.7));
}

TEST_CASE("DriveModule - Fuzz Face hard-clip preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("DriveModule", "Fuzz Face");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("DriveModule"));

    DriveModule drive;
    drive.setState(preset);
    auto state = drive.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 3); // Hard
    REQUIRE(static_cast<double>(state.getProperty("drive")) == Catch::Approx(1.0));
}

TEST_CASE("DynamicsModule - Vocal Leveler compressor preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("DynamicsModule", "Vocal Leveler");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("DynamicsModule"));

    DynamicsModule dyn;
    dyn.setState(preset);
    auto state = dyn.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 0); // Compressor
    REQUIRE(static_cast<double>(state.getProperty("compRatio")) == Catch::Approx(4.0));
    REQUIRE(static_cast<double>(state.getProperty("compThreshold")) == Catch::Approx(-18.0));
}

TEST_CASE("DynamicsModule - Tight Gate gate preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("DynamicsModule", "Tight Gate");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("DynamicsModule"));

    DynamicsModule dyn;
    dyn.setState(preset);
    auto state = dyn.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 2); // Gate
    REQUIRE(static_cast<double>(state.getProperty("gateThreshold")) == Catch::Approx(-40.0));
}

TEST_CASE("EQModule - Scoop preset loads correctly",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("EQModule", "Scoop");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("EQModule"));

    EQModule eq;
    eq.setState(preset);
    auto state = eq.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 0); // Band3
    REQUIRE(static_cast<double>(state.getProperty("lowGain")) == Catch::Approx(3.0));
    REQUIRE(static_cast<double>(state.getProperty("midGain")) == Catch::Approx(-4.0));
    REQUIRE(static_cast<double>(state.getProperty("highGain")) == Catch::Approx(2.0));
}

TEST_CASE("EQModule - Bright Tilt tilt preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("EQModule", "Bright Tilt");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("EQModule"));

    EQModule eq;
    eq.setState(preset);
    auto state = eq.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 2); // Tilt
    REQUIRE(static_cast<double>(state.getProperty("tiltAmount")) == Catch::Approx(6.0));
}

TEST_CASE("ModulationModule - Wide Ensemble chorus preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("ModulationModule", "Wide Ensemble");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("ModulationModule"));

    ModulationModule mod;
    mod.setState(preset);
    auto state = mod.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 0); // Chorus
    REQUIRE(static_cast<double>(state.getProperty("chorusRate")) == Catch::Approx(1.5));
    REQUIRE(static_cast<double>(state.getProperty("chorusDepth")) == Catch::Approx(0.7));
    REQUIRE(static_cast<double>(state.getProperty("mix")) == Catch::Approx(0.55));
}

TEST_CASE("ModulationModule - Jet Flange flanger preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("ModulationModule", "Jet Flange");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("ModulationModule"));

    ModulationModule mod;
    mod.setState(preset);
    auto state = mod.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 1); // Flanger
    REQUIRE(static_cast<double>(state.getProperty("flangerRate")) == Catch::Approx(1.5));
    REQUIRE(static_cast<double>(state.getProperty("flangerDepth")) == Catch::Approx(0.8));
    REQUIRE(static_cast<double>(state.getProperty("flangerFeedback")) == Catch::Approx(0.6));
}

TEST_CASE("SpaceModule - Concert Hall reverb preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("SpaceModule", "Concert Hall");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("SpaceModule"));

    SpaceModule space;
    space.setState(preset);
    auto state = space.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 1); // Hall
    REQUIRE(static_cast<double>(state.getProperty("reverbSize")) == Catch::Approx(0.8));
    REQUIRE(static_cast<double>(state.getProperty("reverbDamping")) == Catch::Approx(0.3));
    REQUIRE(static_cast<double>(state.getProperty("reverbWidth")) == Catch::Approx(0.8));
}

TEST_CASE("SpaceModule - Shimmer Pad shimmer preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("SpaceModule", "Shimmer Pad");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("SpaceModule"));

    SpaceModule space;
    space.setState(preset);
    auto state = space.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 3); // Shimmer
    REQUIRE(static_cast<double>(state.getProperty("shimmerShift")) == Catch::Approx(12.0));
    REQUIRE(static_cast<double>(state.getProperty("shimmerFeedback")) == Catch::Approx(0.4));
}

TEST_CASE("PitchModule - Hard Tune autotune preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("PitchModule", "Hard Tune");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("PitchModule"));

    PitchModule pitch;
    pitch.setState(preset);
    auto state = pitch.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 0); // AutoTune
    REQUIRE(static_cast<double>(state.getProperty("retuneSpeed")) == Catch::Approx(5.0));
    REQUIRE(static_cast<double>(state.getProperty("amount")) == Catch::Approx(1.0));
}

TEST_CASE("PitchModule - Fifth Harmony harmonize preset",
          "[effects][consolidated][factory][roundtrip]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("PitchModule", "Fifth Harmony");
    REQUIRE(preset.isValid());
    REQUIRE(preset.hasType("PitchModule"));

    PitchModule pitch;
    pitch.setState(preset);
    auto state = pitch.getState();

    REQUIRE(static_cast<int>(state.getProperty("mode")) == 2); // Harmonize
    REQUIRE(static_cast<double>(state.getProperty("interval")) == Catch::Approx(7.0));
    REQUIRE(static_cast<double>(state.getProperty("harmonyMix")) == Catch::Approx(0.6));
}

//==============================================================================
// Unknown preset name returns invalid tree
//==============================================================================

TEST_CASE("Unknown module preset name returns invalid",
          "[effects][consolidated][factory]")
{
    auto preset = PresetFactory::getModuleFactoryPreset("ConsolidatedDelay", "NonExistent");
    REQUIRE_FALSE(preset.isValid());

    preset = PresetFactory::getModuleFactoryPreset("DriveModule", "NonExistent");
    REQUIRE_FALSE(preset.isValid());

    preset = PresetFactory::getModuleFactoryPreset("UnknownModule", "AnyName");
    REQUIRE_FALSE(preset.isValid());
}

//==============================================================================
// All factory presets load without crash and round-trip through setState
//==============================================================================

TEST_CASE("All consolidated module presets round-trip safely",
          "[effects][consolidated][factory][roundtrip][stress]")
{
    struct ModuleTest {
        juce::String type;
        std::function<void*(const juce::ValueTree&)> createAndSet;
    };

    // Test each module type with every registered preset
    auto testModule = [](const juce::String& moduleType, auto& effect, auto setStateFn)
    {
        auto names = PresetFactory::getModuleFactoryPresets(moduleType);
        REQUIRE_FALSE(names.isEmpty());

        for (int i = 0; i < names.size(); ++i)
        {
            juce::String name = names[i];
            auto preset = PresetFactory::getModuleFactoryPreset(moduleType, name);
            REQUIRE(preset.isValid());
            REQUIRE(preset.hasType(moduleType));

            // Apply state — should not crash
            REQUIRE_NOTHROW(setStateFn(preset));

            // Read back state — should be valid
            auto reState = effect.getState();
            REQUIRE(reState.isValid());
            REQUIRE(reState.hasType(moduleType));
        }
    };

    {
        ConsolidatedDelay delay;
        testModule("ConsolidatedDelay", delay,
                   [&](const juce::ValueTree& t) { delay.setState(t); });
    }
    {
        DriveModule drive;
        testModule("DriveModule", drive,
                   [&](const juce::ValueTree& t) { drive.setState(t); });
    }
    {
        DynamicsModule dyn;
        testModule("DynamicsModule", dyn,
                   [&](const juce::ValueTree& t) { dyn.setState(t); });
    }
    {
        EQModule eq;
        testModule("EQModule", eq,
                   [&](const juce::ValueTree& t) { eq.setState(t); });
    }
    {
        ModulationModule mod;
        testModule("ModulationModule", mod,
                   [&](const juce::ValueTree& t) { mod.setState(t); });
    }
    {
        SpaceModule space;
        testModule("SpaceModule", space,
                   [&](const juce::ValueTree& t) { space.setState(t); });
    }
    {
        PitchModule pitch;
        testModule("PitchModule", pitch,
                   [&](const juce::ValueTree& t) { pitch.setState(t); });
    }
}

//==============================================================================
// Factory rack presets — Clean Rack
//==============================================================================

TEST_CASE("Clean Rack preset - structure and effect order",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCleanRackPreset();

    // Root structure
    REQUIRE(rack.isValid());
    REQUIRE(rack.hasType("AnaPlugPreset"));
    REQUIRE(rack.getProperty("Name").toString() == "Clean Rack");
    REQUIRE(rack.getProperty("Category").toString() == "Effects");
    REQUIRE(rack.getProperty("Version").toString() == "2.0");

    // Should have 4 children (ordered effects)
    REQUIRE(rack.getNumChildren() == 4);

    // Order: ConsolidatedDelay → SpaceModule → EQModule → DynamicsModule
    REQUIRE(rack.getChild(0).hasType("ConsolidatedDelay"));
    REQUIRE(rack.getChild(1).hasType("SpaceModule"));
    REQUIRE(rack.getChild(2).hasType("EQModule"));
    REQUIRE(rack.getChild(3).hasType("DynamicsModule"));
}

TEST_CASE("Clean Rack preset - Delay params are correct",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCleanRackPreset();
    auto delayTree = rack.getChild(0);
    REQUIRE(delayTree.hasType("ConsolidatedDelay"));

    // Delay should be Mono mode with 120ms
    REQUIRE(static_cast<int>(delayTree.getProperty("mode")) == 0);
    REQUIRE(static_cast<double>(delayTree.getProperty("timeMs")) == Catch::Approx(120.0));
    REQUIRE(static_cast<double>(delayTree.getProperty("feedback")) == Catch::Approx(0.2));
    REQUIRE(static_cast<double>(delayTree.getProperty("mix")) == Catch::Approx(0.3));
}

TEST_CASE("Clean Rack preset - Reverb params are correct",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCleanRackPreset();
    auto reverbTree = rack.getChild(1);
    REQUIRE(reverbTree.hasType("SpaceModule"));

    // Reverb in Room mode, subtle
    REQUIRE(static_cast<int>(reverbTree.getProperty("mode")) == 0);
    REQUIRE(static_cast<double>(reverbTree.getProperty("reverbSize")) == Catch::Approx(0.3));
    REQUIRE(static_cast<double>(reverbTree.getProperty("mix")) == Catch::Approx(0.25));
}

TEST_CASE("Clean Rack preset - EQ params are correct",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCleanRackPreset();
    auto eqTree = rack.getChild(2);
    REQUIRE(eqTree.hasType("EQModule"));

    // EQ in Band3 mode, flat
    REQUIRE(static_cast<int>(eqTree.getProperty("mode")) == 0);
    REQUIRE(static_cast<double>(eqTree.getProperty("lowGain")) == Catch::Approx(0.0));
    REQUIRE(static_cast<double>(eqTree.getProperty("midGain")) == Catch::Approx(0.0));
    REQUIRE(static_cast<double>(eqTree.getProperty("highGain")) == Catch::Approx(0.0));
}

TEST_CASE("Clean Rack preset - Limiter params are correct",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCleanRackPreset();
    auto limiterTree = rack.getChild(3);
    REQUIRE(limiterTree.hasType("DynamicsModule"));

    // Dynamics in Limiter mode
    REQUIRE(static_cast<int>(limiterTree.getProperty("mode")) == 1);
    REQUIRE(static_cast<double>(limiterTree.getProperty("limThreshold")) == Catch::Approx(-3.0));
    REQUIRE(static_cast<double>(limiterTree.getProperty("limCeiling")) == Catch::Approx(-0.5));
}

//==============================================================================
// Factory rack presets — Creative Rack
//==============================================================================

TEST_CASE("Creative Rack preset - structure and effect order",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCreativeRackPreset();

    REQUIRE(rack.isValid());
    REQUIRE(rack.hasType("AnaPlugPreset"));
    REQUIRE(rack.getProperty("Name").toString() == "Creative Rack");
    REQUIRE(rack.getProperty("Category").toString() == "Effects");
    REQUIRE(rack.getProperty("Version").toString() == "2.0");

    // Should have 4 children (ordered effects)
    REQUIRE(rack.getNumChildren() == 4);

    // Order: DriveModule → ModulationModule → SpaceModule → PitchModule
    REQUIRE(rack.getChild(0).hasType("DriveModule"));
    REQUIRE(rack.getChild(1).hasType("ModulationModule"));
    REQUIRE(rack.getChild(2).hasType("SpaceModule"));
    REQUIRE(rack.getChild(3).hasType("PitchModule"));
}

TEST_CASE("Creative Rack preset - Drive params are correct",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCreativeRackPreset();
    auto driveTree = rack.getChild(0);
    REQUIRE(driveTree.hasType("DriveModule"));

    // Drive in Tube mode with moderate drive
    REQUIRE(static_cast<int>(driveTree.getProperty("mode")) == 1);
    REQUIRE(static_cast<double>(driveTree.getProperty("drive")) == Catch::Approx(0.4));
    REQUIRE(static_cast<double>(driveTree.getProperty("mix")) == Catch::Approx(0.7));
}

TEST_CASE("Creative Rack preset - Modulation params are correct",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCreativeRackPreset();
    auto modTree = rack.getChild(1);
    REQUIRE(modTree.hasType("ModulationModule"));

    // Modulation in Chorus mode
    REQUIRE(static_cast<int>(modTree.getProperty("mode")) == 0);
    REQUIRE(static_cast<double>(modTree.getProperty("chorusRate")) == Catch::Approx(1.0));
    REQUIRE(static_cast<double>(modTree.getProperty("chorusDepth")) == Catch::Approx(0.5));
    REQUIRE(static_cast<double>(modTree.getProperty("mix")) == Catch::Approx(0.4));
}

TEST_CASE("Creative Rack preset - Space params are correct",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCreativeRackPreset();
    auto spaceTree = rack.getChild(2);
    REQUIRE(spaceTree.hasType("SpaceModule"));

    // Space in Plate mode
    REQUIRE(static_cast<int>(spaceTree.getProperty("mode")) == 2);
    REQUIRE(static_cast<double>(spaceTree.getProperty("reverbSize")) == Catch::Approx(0.65));
    REQUIRE(static_cast<double>(spaceTree.getProperty("mix")) == Catch::Approx(0.3));
}

TEST_CASE("Creative Rack preset - Pitch params are correct",
          "[effects][consolidated][rack]")
{
    auto rack = PresetFactory::createCreativeRackPreset();
    auto pitchTree = rack.getChild(3);
    REQUIRE(pitchTree.hasType("PitchModule"));

    // Pitch in PitchShift mode (Fifth Up)
    REQUIRE(static_cast<int>(pitchTree.getProperty("mode")) == 1);
    REQUIRE(static_cast<double>(pitchTree.getProperty("semitones")) == Catch::Approx(7.0));
    REQUIRE(static_cast<double>(pitchTree.getProperty("mix")) == Catch::Approx(0.5));
}

//==============================================================================
// Rack presets round-trip through EffectsChain via PresetManager
//==============================================================================

TEST_CASE("Clean Rack preset round-trips through EffectsChain (EffectBase-compatible only)",
          "[effects][consolidated][rack][roundtrip]")
{
    auto testDir = getTestPresetDir();
    auto presetFile = testDir.getChildFile("CleanRackSubset.anaplug");

    ProcessorStore::registerAll();

    {
        TestHarness harness;

        // Add the EffectBase-compatible effects from the Clean Rack
        harness.effectsChain.addEffect(std::make_unique<ConsolidatedDelay>(), "Delay");
        harness.effectsChain.addEffect(std::make_unique<SpaceModule>(), "Reverb");
        harness.effectsChain.addEffect(std::make_unique<DynamicsModule>(), "Limiter");

        auto rack = PresetFactory::createCleanRackPreset();
        // Children 0, 1, 3 are EffectBase-compatible (Delay, Reverb, Limiter)
        REQUIRE(rack.getNumChildren() >= 3);

        auto& slot0 = harness.effectsChain.getEffect(0);
        slot0.effect->setState(rack.getChild(0));

        auto& slot1 = harness.effectsChain.getEffect(1);
        slot1.effect->setState(rack.getChild(1));

        auto& slot2 = harness.effectsChain.getEffect(2);
        slot2.effect->setState(rack.getChild(3)); // DynamicsModule (index 3)

        REQUIRE(harness.presetManager.savePresetToFile(presetFile));
    }

    {
        TestHarness harness;
        REQUIRE(harness.presetManager.loadPresetFromFile(presetFile));
        REQUIRE(harness.effectsChain.getNumEffects() == 3);

        // The XML file should have version "2.0"
        auto xml = juce::XmlDocument::parse(presetFile);
        REQUIRE(xml != nullptr);
        REQUIRE(xml->getStringAttribute("Version") == "2.0");
    }

    cleanupTestDir(testDir);
}

TEST_CASE("Creative Rack preset round-trips through EffectsChain (EffectBase-compatible only)",
          "[effects][consolidated][rack][roundtrip]")
{
    auto testDir = getTestPresetDir();
    auto presetFile = testDir.getChildFile("CreativeRackSubset.anaplug");

    ProcessorStore::registerAll();

    {
        TestHarness harness;

        harness.effectsChain.addEffect(std::make_unique<DriveModule>(), "Drive");
        harness.effectsChain.addEffect(std::make_unique<ModulationModule>(), "Modulation");
        harness.effectsChain.addEffect(std::make_unique<SpaceModule>(), "Space");

        auto rack = PresetFactory::createCreativeRackPreset();
        // Children 0, 1, 2 are EffectBase-compatible (Drive, Modulation, Space)
        REQUIRE(rack.getNumChildren() >= 3);

        auto& slot0 = harness.effectsChain.getEffect(0);
        slot0.effect->setState(rack.getChild(0));

        auto& slot1 = harness.effectsChain.getEffect(1);
        slot1.effect->setState(rack.getChild(1));

        auto& slot2 = harness.effectsChain.getEffect(2);
        slot2.effect->setState(rack.getChild(2));

        REQUIRE(harness.presetManager.savePresetToFile(presetFile));
    }

    {
        TestHarness harness;
        REQUIRE(harness.presetManager.loadPresetFromFile(presetFile));
        REQUIRE(harness.effectsChain.getNumEffects() == 3);

        auto xml = juce::XmlDocument::parse(presetFile);
        REQUIRE(xml != nullptr);
        REQUIRE(xml->getStringAttribute("Version") == "2.0");
    }

    cleanupTestDir(testDir);
}

//==============================================================================
// Edge cases — malformed module preset ValueTrees
//==============================================================================

TEST_CASE("Empty ValueTree setState does not crash",
          "[effects][consolidated][safety]")
{
    juce::ValueTree empty;

    ConsolidatedDelay delay;
    REQUIRE_NOTHROW(delay.setState(empty));

    DriveModule drive;
    REQUIRE_NOTHROW(drive.setState(empty));

    DynamicsModule dyn;
    REQUIRE_NOTHROW(dyn.setState(empty));

    EQModule eq;
    REQUIRE_NOTHROW(eq.setState(empty));

    ModulationModule mod;
    REQUIRE_NOTHROW(mod.setState(empty));

    SpaceModule space;
    REQUIRE_NOTHROW(space.setState(empty));

    PitchModule pitch;
    REQUIRE_NOTHROW(pitch.setState(empty));
}

TEST_CASE("Wrong root tag setState uses defaults",
          "[effects][consolidated][safety]")
{
    juce::ValueTree wrong("WrongType");
    wrong.setProperty("mode", 999, nullptr);

    // Each module should silently accept with defaults and clamp
    {
        ConsolidatedDelay delay;
        delay.setState(wrong);
        auto s = delay.getState();
        // jlimit(0, 5, 999) → 5 (Ducking)
        REQUIRE(static_cast<int>(s.getProperty("mode")) == 5);
        // Other params should be defaults
        REQUIRE(static_cast<double>(s.getProperty("timeMs")) == Catch::Approx(250.0));
    }
    {
        DriveModule drive;
        drive.setState(wrong);
        auto s = drive.getState();
        // jlimit(0, 6, 999) → 6 (Ring)
        REQUIRE(static_cast<int>(s.getProperty("mode")) == 6);
    }
    {
        DynamicsModule dyn;
        dyn.setState(wrong);
        auto s = dyn.getState();
        // jlimit(0, 2, 999) → 2 (Gate)
        REQUIRE(static_cast<int>(s.getProperty("mode")) == 2);
    }
}
