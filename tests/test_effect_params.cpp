#include <catch2/catch_all.hpp>
#include "dsp/effects/EffectParamRegistry.h"
#include "dsp/EffectsChain.h"
#include <cmath>
#include <memory>

namespace
{

void roundTripType(const juce::String& typeName)
{
    auto effect = ana::EffectParamRegistry::create(typeName);
    REQUIRE(effect != nullptr);
    const int n = effect->getNumParams();
    REQUIRE(n > 0);

    for (int i = 0; i < n; ++i)
    {
        const auto& spec = effect->getParamSpec(i);
        REQUIRE(spec.id != nullptr);
        REQUIRE(spec.label != nullptr);
        REQUIRE(spec.max > spec.min);

        // Probe value: int params must land on an exact step; boolean params
        // (range 0-1, threshold semantics) have no meaningful mid — their
        // round-trip contract is "whatever the setter stored is preserved".
        const float mid = spec.min + (spec.max - spec.min) * 0.5f;
        const float probe = spec.isInt ? std::floor(mid) : mid;
        effect->setParamValue(i, probe);
        const float afterSet = effect->getParamValue(i);

        if (spec.isInt)
            REQUIRE(afterSet == probe);

        // State round-trip: a fresh instance restored from getState() keeps
        // every parameter value (units are converted back consistently).
        const auto tree = effect->getState();
        REQUIRE(tree.isValid());
        auto restored = ana::EffectParamRegistry::create(typeName);
        REQUIRE(restored != nullptr);
        restored->setState(tree);
        REQUIRE(std::abs(restored->getParamValue(i) - afterSet)
                <= (spec.max - spec.min) * 0.02f);
    }
}

} // namespace

TEST_CASE("Delay effect exposes editable params", "[effects][params]")
{
    roundTripType("Delay");
}

TEST_CASE("Reverb effect exposes editable params", "[effects][params]")
{
    roundTripType("Reverb");
}

TEST_CASE("Chorus effect exposes editable params", "[effects][params]")
{
    roundTripType("Chorus");
}

TEST_CASE("Compressor effect exposes editable params", "[effects][params]")
{
    roundTripType("Compressor");
}

TEST_CASE("Distortion effect exposes editable params", "[effects][params]")
{
    roundTripType("Distortion");
}

TEST_CASE("Limiter effect exposes editable params", "[effects][params]")
{
    roundTripType("Limiter");
}

TEST_CASE("Bitcrusher effect exposes editable params", "[effects][params]")
{
    roundTripType("Bitcrusher");
}

TEST_CASE("Saturation effect exposes editable params", "[effects][params]")
{
    roundTripType("Saturation");
}

TEST_CASE("EQ effect exposes editable params", "[effects][params]")
{
    roundTripType("EQ");
}

TEST_CASE("AutoTune effect exposes editable params", "[effects][params]")
{
    roundTripType("AutoTune");
}

TEST_CASE("Flanger effect exposes editable params", "[effects][params]")
{
    roundTripType("Flanger");
}

TEST_CASE("Phaser effect exposes editable params", "[effects][params]")
{
    roundTripType("Phaser");
}

TEST_CASE("RingModulator effect exposes editable params", "[effects][params]")
{
    roundTripType("RingModulator");
}

TEST_CASE("StereoWidener effect exposes editable params", "[effects][params]")
{
    roundTripType("StereoWidener");
}

TEST_CASE("Effect registry covers all rack types", "[effects][params]")
{
    const auto& names = ana::EffectParamRegistry::getTypeNames();
    REQUIRE(names.size() == 14);
    for (const auto& name : names)
        REQUIRE(ana::EffectParamRegistry::create(name) != nullptr);
    REQUIRE(ana::EffectParamRegistry::create("NoSuchEffect") == nullptr);
}
