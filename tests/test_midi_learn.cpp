#include <catch2/catch_all.hpp>
#include "dsp/MidiLearn.h"
#include <juce_core/juce_core.h>

using namespace ana;

// ===========================================================================
// addMapping + getMappedValue — range mapping correctness
// ===========================================================================

TEST_CASE("MidiLearn - addMapping and getMappedValue range scaling", "[midi][mapping]")
{
    MidiLearn ml;

    // Map CC 7 to volume (0.0 – 1.0)
    ml.addMapping(7, "volume", 0.0f, 1.0f);

    SECTION("CC at minimum value (0) → min of range")
    {
        auto msg = juce::MidiMessage::controllerEvent(1, 7, 0);
        ml.processMidi(msg);
        REQUIRE(ml.getMappedValue(7) == Approx(0.0f).margin(0.001f));
    }

    SECTION("CC at centre value (64) → midpoint of range")
    {
        auto msg = juce::MidiMessage::controllerEvent(1, 7, 64);
        ml.processMidi(msg);
        // 64/127 ≈ 0.5039, scaled to [0,1] = 0.5039
        REQUIRE(ml.getMappedValue(7) == Approx(64.0f / 127.0f).margin(0.001f));
    }

    SECTION("CC at maximum value (127) → max of range")
    {
        auto msg = juce::MidiMessage::controllerEvent(1, 7, 127);
        ml.processMidi(msg);
        REQUIRE(ml.getMappedValue(7) == Approx(1.0f).margin(0.001f));
    }
}

TEST_CASE("MidiLearn - addMapping with custom min/max range", "[midi][mapping]")
{
    MidiLearn ml;

    // Map CC 1 (mod wheel) to filter cutoff 200 – 20000 Hz
    ml.addMapping(1, "filterCutoff", 200.0f, 20000.0f);

    auto msg = juce::MidiMessage::controllerEvent(1, 1, 0);
    ml.processMidi(msg);
    REQUIRE(ml.getMappedValue(1) == Approx(200.0f).margin(0.001f));

    auto msg2 = juce::MidiMessage::controllerEvent(1, 1, 127);
    ml.processMidi(msg2);
    REQUIRE(ml.getMappedValue(1) == Approx(20000.0f).margin(0.001f));

    // Half-way: 200 + 0.5039 * (20000 - 200) ≈ 10178
    auto msg3 = juce::MidiMessage::controllerEvent(1, 1, 64);
    ml.processMidi(msg3);
    float expected = 200.0f + (64.0f / 127.0f) * (20000.0f - 200.0f);
    REQUIRE(ml.getMappedValue(1) == Approx(expected).margin(0.5f));
}

TEST_CASE("MidiLearn - unmapped CC returns 0.0", "[midi][mapping]")
{
    MidiLearn ml;
    REQUIRE(ml.getMappedValue(99) == Approx(0.0f));
}

// ===========================================================================
// Learn mode
// ===========================================================================

TEST_CASE("MidiLearn - startLearn captures next CC", "[midi][learn]")
{
    MidiLearn ml;

    ml.startLearn("reverbMix");
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

    // getMappedValue should reflect the CC value
    float expected = 100.0f / 127.0f;
    REQUIRE(ml.getMappedValue(15) == Approx(expected).margin(0.001f));
}

TEST_CASE("MidiLearn - startLearn multiple times", "[midi][learn]")
{
    MidiLearn ml;

    ml.startLearn("paramA");
    auto msgA = juce::MidiMessage::controllerEvent(1, 10, 64);
    ml.processMidi(msgA);
    REQUIRE(ml.getMappings().size() == 1);
    REQUIRE(ml.getMappings()[0].ccNumber == 10);

    ml.startLearn("paramB");
    auto msgB = juce::MidiMessage::controllerEvent(1, 20, 100);
    ml.processMidi(msgB);
    REQUIRE(ml.getMappings().size() == 2);
    REQUIRE(ml.getMappings()[1].ccNumber == 20);
}

TEST_CASE("MidiLearn - getLastLearnedParam", "[midi][learn]")
{
    MidiLearn ml;

    ml.startLearn("delayFeedback");
    auto msg = juce::MidiMessage::controllerEvent(1, 25, 80);
    ml.processMidi(msg);

    REQUIRE(ml.getLastLearnedParam() == "delayFeedback");
}

// ===========================================================================
// removeMapping
// ===========================================================================

TEST_CASE("MidiLearn - removeMapping removes specific CC", "[midi][remove]")
{
    MidiLearn ml;
    ml.addMapping(1, "paramA", 0.0f, 1.0f);
    ml.addMapping(2, "paramB", 0.0f, 1.0f);
    ml.addMapping(3, "paramC", 0.0f, 1.0f);
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
    ml.addMapping(1, "paramA", 0.0f, 1.0f);
    REQUIRE_NOTHROW(ml.removeMapping(99));
    REQUIRE(ml.getMappings().size() == 1);
}

TEST_CASE("MidiLearn - clearAllMappings removes all", "[midi][remove]")
{
    MidiLearn ml;
    ml.addMapping(1, "a", 0.0f, 1.0f);
    ml.addMapping(2, "b", 0.0f, 1.0f);
    ml.addMapping(3, "c", 0.0f, 1.0f);
    REQUIRE(ml.getMappings().size() == 3);

    ml.clearAllMappings();
    REQUIRE(ml.getMappings().empty());
}

// ===========================================================================
// XML serialisation round-trip
// ===========================================================================

TEST_CASE("MidiLearn - createXml and loadFromXml round-trip", "[midi][xml]")
{
    MidiLearn original;
    original.addMapping(1,  "volume",    0.0f,   1.0f);
    original.addMapping(7,  "filter",    20.0f,  20000.0f);
    original.addMapping(74, "resonance", 0.1f,   0.95f);

    // Save
    auto* xml = original.createXml();
    REQUIRE(xml != nullptr);
    REQUIRE(xml->hasTagName("MidiLearn"));

    // Count child Mapping elements
    int mappingCount = 0;
    for (auto* child : xml->getChildIterator())
        if (child->hasTagName("Mapping"))
            ++mappingCount;
    REQUIRE(mappingCount == 3);

    // Load into a fresh instance
    MidiLearn loaded;
    loaded.loadFromXml(*xml);

    delete xml;

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
        REQUIRE(m->minValue == Approx(0.0f));
        REQUIRE(m->maxValue == Approx(1.0f));
    }
    {
        auto* m = findCC(7);
        REQUIRE(m != nullptr);
        REQUIRE(m->parameterId == "filter");
        REQUIRE(m->minValue == Approx(20.0f));
        REQUIRE(m->maxValue == Approx(20000.0f));
    }
    {
        auto* m = findCC(74);
        REQUIRE(m != nullptr);
        REQUIRE(m->parameterId == "resonance");
        REQUIRE(m->minValue == Approx(0.1f));
        REQUIRE(m->maxValue == Approx(0.95f));
    }
}

TEST_CASE("MidiLearn - loadFromXml empty document is safe", "[midi][xml]")
{
    MidiLearn ml;
    juce::XmlElement empty("MidiLearn");
    REQUIRE_NOTHROW(ml.loadFromXml(empty));
    REQUIRE(ml.getMappings().empty());
}

TEST_CASE("MidiLearn - loadFromXml wrong tag name is ignored", "[midi][xml]")
{
    MidiLearn ml;
    ml.addMapping(1, "x", 0.0f, 1.0f);

    juce::XmlElement wrong("NotMidiLearn");
    ml.loadFromXml(wrong);
    // Mappings should remain unchanged
    REQUIRE(ml.getMappings().size() == 1);
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_CASE("MidiLearn - non-controller messages are ignored", "[midi][edge]")
{
    MidiLearn ml;
    ml.addMapping(1, "vol", 0.0f, 1.0f);

    // Note-on should not affect mapped values
    auto noteOn = juce::MidiMessage::noteOn(1, 60, 0.8f);
    ml.processMidi(noteOn);

    // Mapped value for CC 1 should still be 0
    REQUIRE(ml.getMappedValue(1) == Approx(0.0f));
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
    ml.startLearn("test");
    REQUIRE(ml.isLearning());
    ml.stopLearn();
    REQUIRE_FALSE(ml.isLearning());
}
