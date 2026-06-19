#include <catch2/catch_all.hpp>
#include "../src/dsp/PresetManager.h"
#include "../src/dsp/PresetFactory.h"
#include "../src/dsp/effects/DistortionEffect.h"
#include "../src/dsp/effects/StereoWidenerEffect.h"
#include "../src/dsp/effects/BitcrusherEffect.h"
#include "../src/dsp/EffectsChain.h"
#include "../src/dsp/STFTConfig.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

using namespace ana;

// ===========================================================================
// Task 14 — Security regression tests V2
//
// V1: Craft DistortionEffect preset with type=999 → verify no crash (clamped)
// V2: Craft StereoWidener preset with mode=999 → verify clamped
// V3: Craft Bitcrusher preset with downsample=0 → verify safe
// V5: Set modSlots baseValuePtr to nullptr → verify skip, no crash
// ===========================================================================

TEST_CASE("V1 - DistortionEffect type=999 clamped to valid range",
          "[security][v2][deserialize]")
{
    // Build a ValueTree with invalid type enum (999)
    juce::ValueTree state("DistortionEffect");
    state.setProperty("type", 999, nullptr);
    state.setProperty("drive", 50.0f, nullptr);
    state.setProperty("range", 50.0f, nullptr);
    state.setProperty("blend", 100.0f, nullptr);
    state.setProperty("volume", 100.0f, nullptr);

    // setState must clamp type=999 to a valid DistortionType (0-4)
    DistortionEffect effect;
    REQUIRE_NOTHROW(effect.setState(state));

    // After setState, type should be clamped to valid range
    auto reState = effect.getState();
    int typeVal = static_cast<int>(reState.getProperty("type"));
    REQUIRE(typeVal >= 0);
    REQUIRE(typeVal <= 4);
    // The enum has 5 values (0=SoftClip .. 4=BitCrush), jlimit(0,4,...) clamps 999→4
    REQUIRE(typeVal == 4);
}

TEST_CASE("V1 - DistortionEffect type=-999 clamped to valid range",
          "[security][v2][deserialize]")
{
    juce::ValueTree state("DistortionEffect");
    state.setProperty("type", -999, nullptr);
    state.setProperty("drive", 50.0f, nullptr);
    state.setProperty("range", 50.0f, nullptr);
    state.setProperty("blend", 100.0f, nullptr);
    state.setProperty("volume", 100.0f, nullptr);

    DistortionEffect effect;
    REQUIRE_NOTHROW(effect.setState(state));

    auto reState = effect.getState();
    int typeVal = static_cast<int>(reState.getProperty("type", -1));
    REQUIRE(typeVal >= 0);
    REQUIRE(typeVal <= 4);
    // jlimit(0, 4, -999) → 0
    REQUIRE(typeVal == 0);
}

TEST_CASE("V2 - StereoWidenerEffect mode=999 clamped to valid range",
          "[security][v2][deserialize]")
{
    // Build ValueTree with invalid mode (999)
    juce::ValueTree state("StereoWidenerEffect");
    state.setProperty("width", 0.5f, nullptr);
    state.setProperty("mode", 999, nullptr);
    state.setProperty("mix", 1.0f, nullptr);
    state.setProperty("bypass", false, nullptr);

    StereoWidenerEffect effect;
    REQUIRE_NOTHROW(effect.setState(state));

    auto reState = effect.getState();
    int modeVal = static_cast<int>(reState.getProperty("mode"));
    // StereoWidenerMode has 3 values (0=Stereo, 1=Wide, 2=Pan), jlimit(0,2,999)→2
    REQUIRE(modeVal >= 0);
    REQUIRE(modeVal <= 2);
    REQUIRE(modeVal == 2);
}

TEST_CASE("V2 - StereoWidenerEffect mode=-999 clamped to valid range",
          "[security][v2][deserialize]")
{
    juce::ValueTree state("StereoWidenerEffect");
    state.setProperty("width", 0.5f, nullptr);
    state.setProperty("mode", -999, nullptr);
    state.setProperty("mix", 1.0f, nullptr);
    state.setProperty("bypass", false, nullptr);

    StereoWidenerEffect effect;
    REQUIRE_NOTHROW(effect.setState(state));

    auto reState = effect.getState();
    int modeVal = static_cast<int>(reState.getProperty("mode"));
    // jlimit(0, 2, -999) → 0
    REQUIRE(modeVal == 0);
}

TEST_CASE("V3 - BitcrusherEffect downsample=0 clamped to safe minimum",
          "[security][v2][deserialize]")
{
    // downsample=0 would cause division by zero in the DSP code
    juce::ValueTree state("BitcrusherEffect");
    state.setProperty("bitDepth", 8.0f, nullptr);
    state.setProperty("downsample", 0.0f, nullptr);
    state.setProperty("mix", 1.0f, nullptr);

    BitcrusherEffect effect;
    REQUIRE_NOTHROW(effect.setState(state));

    // setDownsample clamps to [1, 32], so 0 → 1
    REQUIRE(effect.getDownsample() == Catch::Approx(1.0f));
    REQUIRE(effect.getBitDepth() == Catch::Approx(8.0f));
}

TEST_CASE("V3 - BitcrusherEffect downsample=negative clamped",
          "[security][v2][deserialize]")
{
    juce::ValueTree state("BitcrusherEffect");
    state.setProperty("bitDepth", 8.0f, nullptr);
    state.setProperty("downsample", -5.0f, nullptr);
    state.setProperty("mix", 1.0f, nullptr);

    BitcrusherEffect effect;
    REQUIRE_NOTHROW(effect.setState(state));
    // std::max(1.0f, -5.0f) → 1
    REQUIRE(effect.getDownsample() == Catch::Approx(1.0f));
}

TEST_CASE("V3 - BitcrusherEffect downsample=999 clamped to 32",
          "[security][v2][deserialize]")
{
    juce::ValueTree state("BitcrusherEffect");
    state.setProperty("bitDepth", 8.0f, nullptr);
    state.setProperty("downsample", 999.0f, nullptr);
    state.setProperty("mix", 1.0f, nullptr);

    BitcrusherEffect effect;
    REQUIRE_NOTHROW(effect.setState(state));
    // std::min(32.0f, 999.0f) → 32
    REQUIRE(effect.getDownsample() == Catch::Approx(32.0f));
}

TEST_CASE("V3 - BitcrusherEffect bitDepth=0 clamped to 1",
          "[security][v2][deserialize]")
{
    juce::ValueTree state("BitcrusherEffect");
    state.setProperty("bitDepth", 0.0f, nullptr);
    state.setProperty("downsample", 1.0f, nullptr);
    state.setProperty("mix", 1.0f, nullptr);

    BitcrusherEffect effect;
    REQUIRE_NOTHROW(effect.setState(state));
    REQUIRE(effect.getBitDepth() == Catch::Approx(1.0f));
}

// ===========================================================================
// V5: modSlots baseValuePtr = nullptr — verify safe skip
// ===========================================================================

TEST_CASE("V5 - ModulationSlot baseValuePtr nullptr during serialisation",
          "[security][v2][nullptr]")
{
    // Create modulation slots with one slot having a nullptr baseValuePtr
    std::array<ModulationSlot, 16> slots;

    // Set up a valid slot first
    std::atomic<float> val1{0.5f};
    slots[0].baseValuePtr = &val1;
    slots[0].paramId = "cutoff";
    slots[0].mod.source = LFO1;
    slots[0].mod.depth = 0.3f;
    slots[0].mod.curve = 1.0f;

    // Slot with nullptr baseValuePtr — simulates a disconnected target
    slots[1].baseValuePtr = nullptr;
    slots[1].paramId = "resonance";
    slots[1].mod.source = LFO2;
    slots[1].mod.depth = 0.5f;

    // Slot with both nullptr baseValuePtr and empty paramId — edge case
    slots[2].baseValuePtr = nullptr;
    slots[2].paramId = "";

    // Configure PresetManager with these slots
    PresetManager pm;
    pm.setModulationSlotsRef(&slots);

    // Serialise — should not crash even if baseValuePtr is nullptr
    juce::ValueTree routing;
    REQUIRE_NOTHROW(routing = pm.serialiseModulationRouting());

    // The serialisation should have produced a valid tree
    REQUIRE(routing.isValid());
    REQUIRE(routing.hasType("ModulationRouting"));

    // Should have entries for all 16 slots (including the nullptr ones)
    REQUIRE(routing.getNumChildren() == 16);

    // First slot should be serialised correctly
    auto child0 = routing.getChild(0);
    REQUIRE(child0.getProperty("paramId").toString() == "cutoff");
    REQUIRE(child0.getProperty("source").toString() == "LFO1");
    REQUIRE(static_cast<double>(child0.getProperty("depth")) == Catch::Approx(0.3));

    // Deserialise — should not crash with nullptr baseValuePtr
    REQUIRE_NOTHROW(pm.deserialiseModulationRouting(routing));

    // After deserialise, the nullptr slot's paramId should still be findable
    // and the deserialise should not have crashed when resetting slots
    REQUIRE(slots[1].baseValuePtr == nullptr);
    REQUIRE(slots[2].baseValuePtr == nullptr);
}

TEST_CASE("V5 - ModulationSlot baseValuePtr nullptr on empty bus",
          "[security][v2][nullptr]")
{
    // Test with all slots having nullptr baseValuePtr
    std::array<ModulationSlot, 16> slots;
    for (auto& s : slots)
    {
        s.baseValuePtr = nullptr;
        s.paramId = "";
        s.mod.source = OFF;
        s.mod.depth = 0.0f;
    }

    PresetManager pm;
    pm.setModulationSlotsRef(&slots);

    // Serialisation should not crash with all-nullptr slots
    juce::ValueTree routing;
    REQUIRE_NOTHROW(routing = pm.serialiseModulationRouting());
    REQUIRE(routing.isValid());
    REQUIRE(routing.getNumChildren() == 16);

    // Deserialisation should not crash
    REQUIRE_NOTHROW(pm.deserialiseModulationRouting(routing));
}

TEST_CASE("V5 - ModulationSlot deserialisation with null ref does not crash",
          "[security][v2][nullptr]")
{
    // Call deserialiseModulationRouting with modSlotsRef_=nullptr
    PresetManager pm;

    juce::ValueTree routing("ModulationRouting");
    routing.setProperty("version", 1, nullptr);
    juce::ValueTree slot("Slot");
    slot.setProperty("paramId", "cutoff", nullptr);
    slot.setProperty("source", "LFO1", nullptr);
    slot.setProperty("depth", 0.5f, nullptr);
    routing.addChild(slot, 0, nullptr);

    // modSlotsRef_ is nullptr by default — should return false without crash
    REQUIRE_FALSE(pm.deserialiseModulationRouting(routing));
}
