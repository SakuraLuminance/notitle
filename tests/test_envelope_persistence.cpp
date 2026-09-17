#include <catch2/catch_all.hpp>
#define ANA_INCLUDE_TEST_ACCESSORS
#include "dsp/PresetManager.h"
#include <array>

using namespace ana;

//==============================================================================
/** Test accessor exposing the private envelope serialisation helpers. */
class PresetManagerTestAccess
{
public:
    static juce::ValueTree serialiseENVConfig(PresetManager& pm) { return pm.serialiseENVConfig(); }
    static bool deserialiseENVConfig(PresetManager& pm, const juce::ValueTree& tree) { return pm.deserialiseENVConfig(tree); }
    static juce::ValueTree serialiseVolumeADSR(PresetManager& pm) { return pm.serialiseVolumeADSR(); }
    static bool deserialiseVolumeADSR(PresetManager& pm, const juce::ValueTree& tree) { return pm.deserialiseVolumeADSR(tree); }
};

TEST_CASE("Envelope preset persistence round-trips shape, loop and sync", "[envelope][preset]")
{
    std::array<MultiPointEnvelope, 3> pool;
    MultiPointEnvelope volume;
    for (auto& e : pool)
        e.prepare(48000.0);
    volume.prepare(48000.0);

    PresetManager pm;
    pm.setEnvPoolRef(&pool);
    pm.setVolumeAdsrRef(&volume);

    auto& env = pool[1];
    env.addBreakpoint(0.0f, 0.0f);
    env.addBreakpoint(0.15f, 0.9f, CurveType::Exponential);
    env.addBreakpoint(0.4f, 0.4f, CurveType::SCurve);
    env.addBreakpoint(0.75f, 0.2f);
    env.addBreakpoint(1.2f, 0.0f);
    env.setLoopMode(LoopMode::PingPong);
    env.setLoopStart(1);
    env.setLoopEnd(3);
    env.setSyncMode(true);
    env.setTempo(90.0);
    env.setBeatDivision(0.5);

    volume.addBreakpoint(0.0f, 0.0f);
    volume.addBreakpoint(0.05f, 1.0f);
    volume.addBreakpoint(0.3f, 0.6f);
    volume.addBreakpoint(0.9f, 0.0f);
    volume.setLoopMode(LoopMode::Sustain);
    volume.setLoopEnd(2);

    const auto envTree = PresetManagerTestAccess::serialiseENVConfig(pm);
    const auto volTree = PresetManagerTestAccess::serialiseVolumeADSR(pm);

    std::array<MultiPointEnvelope, 3> pool2;
    MultiPointEnvelope volume2;
    for (auto& e : pool2)
        e.prepare(48000.0);
    volume2.prepare(48000.0);

    pm.setEnvPoolRef(&pool2);
    pm.setVolumeAdsrRef(&volume2);

    REQUIRE(PresetManagerTestAccess::deserialiseENVConfig(pm, envTree));
    REQUIRE(PresetManagerTestAccess::deserialiseVolumeADSR(pm, volTree));

    auto& loaded = pool2[1];
    REQUIRE(loaded.getNumBreakpoints() == 5);
    REQUIRE(loaded.getBreakpoint(1).time  == Catch::Approx(0.15f).margin(0.0001f));
    REQUIRE(loaded.getBreakpoint(1).value == Catch::Approx(0.9f).margin(0.0001f));
    REQUIRE(loaded.getBreakpoint(1).curve == CurveType::Exponential);
    REQUIRE(loaded.getBreakpoint(2).curve == CurveType::SCurve);
    REQUIRE(loaded.getBreakpoint(3).curve == CurveType::Linear);
    REQUIRE(loaded.getLoopMode() == LoopMode::PingPong);
    REQUIRE(loaded.getLoopStart() == 1);
    REQUIRE(loaded.getLoopEnd() == 3);
    REQUIRE(loaded.getSyncMode());
    REQUIRE(loaded.getTempo() == Catch::Approx(90.0));
    REQUIRE(loaded.getBeatDivision() == Catch::Approx(0.5));

    REQUIRE(volume2.getNumBreakpoints() == 4);
    REQUIRE(volume2.getLoopMode() == LoopMode::Sustain);
    REQUIRE(volume2.getLoopEnd() == 2);
}

TEST_CASE("Legacy ADSR-only preset nodes still load", "[envelope][preset]")
{
    std::array<MultiPointEnvelope, 3> pool;
    MultiPointEnvelope volume;
    for (auto& e : pool)
        e.prepare(48000.0);
    volume.prepare(48000.0);

    PresetManager pm;
    pm.setEnvPoolRef(&pool);
    pm.setVolumeAdsrRef(&volume);

    // Hand-written legacy node: ADSR scalars only, no breakpoint children.
    juce::ValueTree legacy("ENVConfig");
    juce::ValueTree env("ENV");
    env.setProperty("index", 0, nullptr);
    env.setProperty("attack", 0.2f, nullptr);
    env.setProperty("decay", 0.3f, nullptr);
    env.setProperty("sustain", 0.5f, nullptr);
    env.setProperty("release", 1.5f, nullptr);
    legacy.addChild(env, -1, nullptr);

    // Give env 2 a custom shape; the legacy node must not clobber it.
    pool[2].addBreakpoint(0.0f, 0.1f);
    pool[2].addBreakpoint(0.5f, 0.9f);
    pool[2].addBreakpoint(1.0f, 0.0f);

    REQUIRE(PresetManagerTestAccess::deserialiseENVConfig(pm, legacy));

    REQUIRE(pool[0].getNumBreakpoints() == 4);              // rebuilt standard ADSR
    REQUIRE(pool[0].getAttack()  == Catch::Approx(0.2f));
    REQUIRE(pool[0].getDecay()   == Catch::Approx(0.3f));
    REQUIRE(pool[0].getSustain() == Catch::Approx(0.5f));
    REQUIRE(pool[0].getRelease() == Catch::Approx(1.5f));

    REQUIRE(pool[2].getNumBreakpoints() == 3);              // untouched
    REQUIRE(pool[2].getBreakpoint(1).value == Catch::Approx(0.9f));
}
