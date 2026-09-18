#include <catch2/catch_all.hpp>
#include "dsp/MidiLearn.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

using namespace ana;

// ===========================================================================
// addMapping + getMappedValue — range mapping correctness
// ===========================================================================

TEST_CASE("MidiLearn - addMapping and getMappedValue range scaling", "[midi][mapping]")
{
    MidiLearn ml;
    std::atomic<float> volTarget{0.0f};

    // Map CC 7 to volume (0.0 – 1.0)
    ml.addMapping(7, "volume", &volTarget, 0.0f, 1.0f);

    SECTION("CC at minimum value (0) → min of range")
    {
        auto msg = juce::MidiMessage::controllerEvent(1, 7, 0);
        ml.processMidi(msg);
        REQUIRE(volTarget.load() == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("CC at centre value (64) → midpoint of range")
    {
        auto msg = juce::MidiMessage::controllerEvent(1, 7, 64);
        ml.processMidi(msg);
        // 64/127 ≈ 0.5039, scaled to [0,1] = 0.5039
        REQUIRE(volTarget.load() == Catch::Approx(64.0f / 127.0f).margin(0.001f));
    }

    SECTION("CC at maximum value (127) → max of range")
    {
        auto msg = juce::MidiMessage::controllerEvent(1, 7, 127);
        ml.processMidi(msg);
        REQUIRE(volTarget.load() == Catch::Approx(1.0f).margin(0.001f));
    }
}

TEST_CASE("MidiLearn - addMapping with custom min/max range", "[midi][mapping]")
{
    MidiLearn ml;
    std::atomic<float> filterTarget{0.0f};

    // Map CC 1 (mod wheel) to filter cutoff 200 – 20000 Hz
    ml.addMapping(1, "filterCutoff", &filterTarget, 200.0f, 20000.0f);

    auto msg = juce::MidiMessage::controllerEvent(1, 1, 0);
    ml.processMidi(msg);
    REQUIRE(filterTarget.load() == Catch::Approx(200.0f).margin(0.001f));

    auto msg2 = juce::MidiMessage::controllerEvent(1, 1, 127);
    ml.processMidi(msg2);
    REQUIRE(filterTarget.load() == Catch::Approx(20000.0f).margin(0.001f));

    // Half-way: 200 + 0.5039 * (20000 - 200) ≈ 10178
    auto msg3 = juce::MidiMessage::controllerEvent(1, 1, 64);
    ml.processMidi(msg3);
    float expected = 200.0f + (64.0f / 127.0f) * (20000.0f - 200.0f);
    REQUIRE(filterTarget.load() == Catch::Approx(expected).margin(0.5f));
}

TEST_CASE("MidiLearn - unmapped CC has no mapping", "[midi][mapping]")
{
    MidiLearn ml;
    bool foundCC99 = false;
    for (const auto& m : ml.getMappings())
        if (m.ccNumber == 99) foundCC99 = true;
    REQUIRE_FALSE(foundCC99);
}

// ===========================================================================
// Learn mode
// ===========================================================================

TEST_CASE("MidiLearn - startLearn captures next CC", "[midi][learn]")
{
    MidiLearn ml;
    std::atomic<float> reverbTarget{0.0f};

    ml.startLearn("reverbMix", &reverbTarget);
    REQUIRE(ml.isLearning());

    // Send a CC message while in learn mode
    auto msg = juce::MidiMessage::controllerEvent(1, 15, 100);
    ml.processMidi(msg);

    // Should have stopped learning
    REQUIRE_FALSE(ml.isLearning());

    // A mapping should have been created for CC 15
    const auto& mappings = ml.getMappings();
    REQUIRE(mappings.size() == 1);
    REQUIRE(mappings[0].ccNumber == 15);
    REQUIRE(mappings[0].parameterId == "reverbMix");

    // Target value should reflect the CC value
    float expected = 100.0f / 127.0f;
    REQUIRE(reverbTarget.load() == Catch::Approx(expected).margin(0.001f));
}

TEST_CASE("MidiLearn - startLearn multiple times", "[midi][learn]")
{
    MidiLearn ml;
    std::atomic<float> targetA{0.0f};
    std::atomic<float> targetB{0.0f};

    ml.startLearn("paramA", &targetA);
    auto msgA = juce::MidiMessage::controllerEvent(1, 10, 64);
    ml.processMidi(msgA);
    REQUIRE(ml.getMappings().size() == 1);
    REQUIRE(ml.getMappings()[0].ccNumber == 10);

    ml.startLearn("paramB", &targetB);
    auto msgB = juce::MidiMessage::controllerEvent(1, 20, 100);
    ml.processMidi(msgB);
    REQUIRE(ml.getMappings().size() == 2);
    REQUIRE(ml.getMappings()[1].ccNumber == 20);
}

TEST_CASE("MidiLearn - getLastLearnedParam", "[midi][learn]")
{
    MidiLearn ml;
    std::atomic<float> delayTarget{0.0f};

    ml.startLearn("delayFeedback", &delayTarget);
    auto msg = juce::MidiMessage::controllerEvent(1, 25, 80);
    ml.processMidi(msg);

    REQUIRE(ml.getMappings().back().parameterId == "delayFeedback");
}

// ===========================================================================
// removeMapping
// ===========================================================================

TEST_CASE("MidiLearn - removeMapping removes specific CC", "[midi][remove]")
{
    MidiLearn ml;
    std::atomic<float> targetA{0.0f};
    std::atomic<float> targetB{0.0f};
    std::atomic<float> targetC{0.0f};
    ml.addMapping(1, "paramA", &targetA, 0.0f, 1.0f);
    ml.addMapping(2, "paramB", &targetB, 0.0f, 1.0f);
    ml.addMapping(3, "paramC", &targetC, 0.0f, 1.0f);
    REQUIRE(ml.getMappings().size() == 3);

    ml.removeMapping(2);
    REQUIRE(ml.getMappings().size() == 2);

    // The remaining mappings should be CC 1 and CC 3
    bool foundCC1 = false, foundCC3 = false;
    for (const auto& m : ml.getMappings())
    {
        if (m.ccNumber == 1) foundCC1 = true;
        if (m.ccNumber == 3) foundCC3 = true;
        REQUIRE(m.ccNumber != 2); // CC 2 should be gone
    }
    REQUIRE(foundCC1);
    REQUIRE(foundCC3);
}

TEST_CASE("MidiLearn - removeMapping non-existent CC is safe", "[midi][remove]")
{
    MidiLearn ml;
    std::atomic<float> targetA{0.0f};
    ml.addMapping(1, "paramA", &targetA, 0.0f, 1.0f);
    REQUIRE_NOTHROW(ml.removeMapping(99));
    REQUIRE(ml.getMappings().size() == 1);
}

TEST_CASE("MidiLearn - removeAllMappings removes all", "[midi][remove]")
{
    MidiLearn ml;
    std::atomic<float> targetA{0.0f};
    std::atomic<float> targetB{0.0f};
    std::atomic<float> targetC{0.0f};
    ml.addMapping(1, "a", &targetA, 0.0f, 1.0f);
    ml.addMapping(2, "b", &targetB, 0.0f, 1.0f);
    ml.addMapping(3, "c", &targetC, 0.0f, 1.0f);
    REQUIRE(ml.getMappings().size() == 3);

    ml.removeAllMappings();
    REQUIRE(ml.getMappings().empty());
}

// ===========================================================================
// ValueTree serialisation round-trip
// ===========================================================================

TEST_CASE("MidiLearn - saveProcessorState and loadProcessorState round-trip", "[midi][state]")
{
    MidiLearn original;
    std::atomic<float> volTarget{0.0f};
    std::atomic<float> filterTarget{0.0f};
    std::atomic<float> resTarget{0.0f};
    original.addMapping(1,  "volume",    &volTarget,    0.0f,   1.0f);
    original.addMapping(7,  "filter",    &filterTarget, 20.0f,  20000.0f);
    original.addMapping(74, "resonance", &resTarget,    0.1f,   0.95f);

    // Save
    auto state = original.saveProcessorState();
    REQUIRE(state.hasType("MidiLearn"));

    // Count child Mapping elements
    int mappingCount = 0;
    for (const auto& child : state)
        if (child.hasType("Mapping"))
            ++mappingCount;
    REQUIRE(mappingCount == 3);

    // Load into a fresh instance
    MidiLearn loaded;
    loaded.loadProcessorState(state);

    // Verify mappings
    const auto& restored = loaded.getMappings();
    REQUIRE(restored.size() == 3);

    // Check each mapping
    auto findCC = [&](int cc) -> const MidiMapping* {
        for (const auto& m : restored)
            if (m.ccNumber == cc) return &m;
        return nullptr;
    };

    {
        auto* m = findCC(1);
        REQUIRE(m != nullptr);
        REQUIRE(m->parameterId == "volume");
        REQUIRE(m->minValue == Catch::Approx(0.0f));
        REQUIRE(m->maxValue == Catch::Approx(1.0f));
    }
    {
        auto* m = findCC(7);
        REQUIRE(m != nullptr);
        REQUIRE(m->parameterId == "filter");
        REQUIRE(m->minValue == Catch::Approx(20.0f));
        REQUIRE(m->maxValue == Catch::Approx(20000.0f));
    }
    {
        auto* m = findCC(74);
        REQUIRE(m != nullptr);
        REQUIRE(m->parameterId == "resonance");
        REQUIRE(m->minValue == Catch::Approx(0.1f));
        REQUIRE(m->maxValue == Catch::Approx(0.95f));
    }
}

TEST_CASE("MidiLearn - loadProcessorState empty document is safe", "[midi][state]")
{
    MidiLearn ml;
    juce::ValueTree empty("MidiLearn");
    REQUIRE_NOTHROW(ml.loadProcessorState(empty));
    REQUIRE(ml.getMappings().empty());
}

TEST_CASE("MidiLearn - loadProcessorState wrong tag name is ignored", "[midi][state]")
{
    MidiLearn ml;
    std::atomic<float> targetX{0.0f};
    ml.addMapping(1, "x", &targetX, 0.0f, 1.0f);

    juce::ValueTree wrong("NotMidiLearn");
    ml.loadProcessorState(wrong);
    // Mappings should remain unchanged
    REQUIRE(ml.getMappings().size() == 1);
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_CASE("MidiLearn - non-controller messages are ignored", "[midi][edge]")
{
    MidiLearn ml;
    std::atomic<float> volTarget{0.0f};
    ml.addMapping(1, "vol", &volTarget, 0.0f, 1.0f);

    // Note-on should not affect mapped values
    auto noteOn = juce::MidiMessage::noteOn(1, 60, 0.8f);
    ml.processMidi(noteOn);

    // Mapped value for CC 1 should still be 0
    REQUIRE(volTarget.load() == Catch::Approx(0.0f));
}

TEST_CASE("MidiLearn - isLearning initial state", "[midi][edge]")
{
    MidiLearn ml;
    REQUIRE_FALSE(ml.isLearning());
    REQUIRE(ml.getMappings().empty());
}

TEST_CASE("MidiLearn - startLearn/stopLearn toggle", "[midi][edge]")
{
    MidiLearn ml;
    std::atomic<float> testTarget{0.0f};
    ml.startLearn("test", &testTarget);
    REQUIRE(ml.isLearning());
    ml.stopLearn();
    REQUIRE_FALSE(ml.isLearning());
}


// ===========================================================================
// Callback targets (rack effect parameters)
//
// Effect parameters live behind EffectBase::get/setParamValue rather than an
// atomic, so a mapping may carry a setter/getter pair instead.  The atomic
// path stays authoritative whenever both kinds are connected.
// ===========================================================================

TEST_CASE("MidiLearn - callback target receives scaled values", "[midi][callback]")
{
    MidiLearn ml;
    float received = -1.0f;
    ml.addMapping(20, "fx0_p1",
                  [&received](float v) { received = v; },
                  [&received]() { return received; },
                  100.0f, 2000.0f);

    REQUIRE(ml.getMappings().size() == 1);
    REQUIRE(ml.getMappings()[0].targetParam == nullptr);
    REQUIRE(static_cast<bool>(ml.getMappings()[0].targetSetter));

    SECTION("CC 0 -> minimum of the range")
    {
        ml.processMidi(juce::MidiMessage::controllerEvent(1, 20, 0));
        REQUIRE(received == Catch::Approx(100.0f).margin(0.001f));
    }

    SECTION("CC 127 -> maximum of the range")
    {
        ml.processMidi(juce::MidiMessage::controllerEvent(1, 20, 127));
        REQUIRE(received == Catch::Approx(2000.0f).margin(0.001f));
    }

    SECTION("Unmapped CC leaves the callback untouched")
    {
        ml.processMidi(juce::MidiMessage::controllerEvent(1, 21, 64));
        REQUIRE(received == Catch::Approx(-1.0f).margin(0.001f));
    }
}

TEST_CASE("MidiLearn - startLearn learns a callback target", "[midi][callback][learn]")
{
    MidiLearn ml;
    float value = 0.0f;

    ml.startLearn("fx2_p0",
                  [&value](float v) { value = v; },
                  [&value]() { return value; },
                  0.0f, 10.0f);
    REQUIRE(ml.isLearning());

    ml.processMidi(juce::MidiMessage::controllerEvent(1, 11, 127));
    REQUIRE_FALSE(ml.isLearning());

    REQUIRE(ml.getMappings().size() == 1);
    REQUIRE(ml.getMappings()[0].ccNumber == 11);
    REQUIRE(ml.getMappings()[0].parameterId == "fx2_p0");
    // The learned value is applied immediately...
    REQUIRE(value == Catch::Approx(10.0f).margin(0.001f));

    // ...and later CCs keep driving the callback.
    ml.processMidi(juce::MidiMessage::controllerEvent(1, 11, 0));
    REQUIRE(value == Catch::Approx(0.0f).margin(0.001f));
}

TEST_CASE("MidiLearn - reconnecting to an atomic clears the callback target",
          "[midi][callback]")
{
    MidiLearn ml;
    float callbackValue = -1.0f;
    std::atomic<float> atomicTarget{0.0f};

    ml.addMapping(9, "fx0_p0",
                  [&callbackValue](float v) { callbackValue = v; },
                  [&callbackValue]() { return callbackValue; },
                  0.0f, 1.0f);

    ml.reconnectTarget("fx0_p0", &atomicTarget);
    REQUIRE(ml.getMappings()[0].targetParam == &atomicTarget);
    REQUIRE_FALSE(static_cast<bool>(ml.getMappings()[0].targetSetter));

    ml.processMidi(juce::MidiMessage::controllerEvent(1, 9, 127));
    REQUIRE(atomicTarget.load() == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(callbackValue == Catch::Approx(-1.0f).margin(0.001f));
}

TEST_CASE("MidiLearn - reconnecting a stale atomic to callbacks",
          "[midi][callback][state]")
{
    MidiLearn ml;
    std::atomic<float> staleTarget{0.0f};
    ml.addMapping(7, "fx1_p2", &staleTarget, 0.0f, 1.0f);

    float viaCallback = -1.0f;
    ml.reconnectTarget("fx1_p2",
                       [&viaCallback](float v) { viaCallback = v; },
                       [&viaCallback]() { return viaCallback; });

    REQUIRE(ml.getMappings()[0].targetParam == nullptr);
    REQUIRE(static_cast<bool>(ml.getMappings()[0].targetSetter));

    ml.processMidi(juce::MidiMessage::controllerEvent(1, 7, 127));
    REQUIRE(viaCallback == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(staleTarget.load() == Catch::Approx(0.0f).margin(0.001f));
}

TEST_CASE("MidiLearn - empty callback target is a harmless no-op",
          "[midi][callback][nullptr]")
{
    MidiLearn ml;
    ml.addMapping(12, "fx9_p9", std::function<void(float)>{},
                  std::function<float()>{}, 0.0f, 1.0f);

    REQUIRE_NOTHROW(ml.processMidi(juce::MidiMessage::controllerEvent(1, 12, 64)));

    float out = 0.0f;
    REQUIRE_FALSE(ml.getMappingValue("fx9_p9", out));
}

TEST_CASE("MidiLearn - removeMapping stops a callback target", "[midi][callback]")
{
    MidiLearn ml;
    float value = 0.0f;
    ml.addMapping(42, "fx0_p3",
                  [&value](float v) { value = v; },
                  [&value]() { return value; },
                  0.0f, 1.0f);

    ml.processMidi(juce::MidiMessage::controllerEvent(1, 42, 127));
    REQUIRE(value == Catch::Approx(1.0f).margin(0.001f));

    ml.removeMapping(42);
    REQUIRE(ml.getMappings().empty());

    ml.processMidi(juce::MidiMessage::controllerEvent(1, 42, 0));
    REQUIRE(value == Catch::Approx(1.0f).margin(0.001f));
}

TEST_CASE("MidiLearn - getMappingValue reads atomic and callback targets",
          "[midi][callback]")
{
    MidiLearn ml;
    std::atomic<float> atomicValue{0.25f};
    ml.addMapping(1, "atomic_param", &atomicValue, 0.0f, 1.0f);

    float callbackValue = 0.75f;
    ml.addMapping(2, "cb_param",
                  [&callbackValue](float v) { callbackValue = v; },
                  [&callbackValue]() { return callbackValue; },
                  0.0f, 1.0f);

    float out = -1.0f;
    REQUIRE(ml.getMappingValue("atomic_param", out));
    REQUIRE(out == Catch::Approx(0.25f));

    REQUIRE(ml.getMappingValue("cb_param", out));
    REQUIRE(out == Catch::Approx(0.75f));

    REQUIRE_FALSE(ml.getMappingValue("no_such_param", out));
}

TEST_CASE("MidiLearn - callback targets survive a state round-trip disconnected",
          "[midi][callback][state]")
{
    MidiLearn ml;
    float callbackValue = 0.5f;
    ml.addMapping(30, "fx3_p1",
                  [&callbackValue](float v) { callbackValue = v; },
                  [&callbackValue]() { return callbackValue; },
                  0.0f, 2.0f);

    const auto state = ml.saveProcessorState();

    MidiLearn loaded;
    loaded.loadProcessorState(state);
    REQUIRE(loaded.getMappings().size() == 1);
    REQUIRE(loaded.getMappings()[0].parameterId == "fx3_p1");
    REQUIRE(loaded.getMappings()[0].minValue == Catch::Approx(0.0f));
    REQUIRE(loaded.getMappings()[0].maxValue == Catch::Approx(2.0f));

    // Callbacks are runtime-only: a loaded mapping has no target until the
    // editor reconnects it (and must not crash in the meantime).
    REQUIRE(loaded.getMappings()[0].targetParam == nullptr);
    REQUIRE_FALSE(static_cast<bool>(loaded.getMappings()[0].targetSetter));
    REQUIRE_FALSE(static_cast<bool>(loaded.getMappings()[0].targetGetter));

    REQUIRE_NOTHROW(loaded.processMidi(juce::MidiMessage::controllerEvent(1, 30, 100)));
}

