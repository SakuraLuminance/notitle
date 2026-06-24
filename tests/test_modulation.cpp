#include <catch2/catch_all.hpp>
#include <cmath>
#include <cstring>
#include <array>
#include <atomic>
#include "dsp/ModulationEngine.h"
#include "dsp/LFOSystem.h"
#include "dsp/MultiPointEnvelope.h"
#include "dsp/ModulationBus.h"

using namespace ana;

//==============================================================================
// Shared test constants
//==============================================================================
static constexpr double TEST_SR = 44100.0;

//==============================================================================
// Helper: simulate the processor's flat-array modulation pass for a single slot
//==============================================================================
static float applyModulation(ModulationSlot& slot, const float lfoVals[4], const float envVals[3])
{
    const float baseVal = slot.baseValuePtr->load(std::memory_order_relaxed);
    float modVal = 0.0f;
    const int src = static_cast<int>(slot.mod.source);

    if (src >= static_cast<int>(ModSource::LFO1) && src <= static_cast<int>(ModSource::LFO4))
        modVal = lfoVals[static_cast<size_t>(src - static_cast<int>(ModSource::LFO1))];
    else if (src >= static_cast<int>(ModSource::ENV1) && src <= static_cast<int>(ModSource::ENV3))
        modVal = envVals[static_cast<size_t>(src - static_cast<int>(ModSource::ENV1))];

    slot.modulatedValue = baseVal + modVal * slot.mod.depth;
    return slot.modulatedValue;
}

//==============================================================================
// Helpers for LFO / ENV pool simulation
//==============================================================================
static void runLfoPool(std::array<LFOSystem, 4>& pool, int numSamples, float outVals[4])
{
    for (int i = 0; i < 4; ++i)
        outVals[i] = pool[static_cast<size_t>(i)].process(numSamples);
}

static void runEnvPool(std::array<MultiPointEnvelope, 3>& pool, int numSamples, float outVals[3])
{
    for (int i = 0; i < 3; ++i)
        outVals[i] = pool[static_cast<size_t>(i)].process(numSamples);
}

//==============================================================================
// Samples per cycle at a given frequency
//==============================================================================
static int samplesPerCycle(double hz)
{
    return static_cast<int>(std::round(TEST_SR / hz));
}

//==============================================================================
// Test: LFO modulation applied to parameter
//==============================================================================
TEST_CASE("LFO modulation applied to parameter", "[mod][lfo][routing]")
{
    // Simulate the processor's LFO pool
    std::array<LFOSystem, 4> lfoPool;
    for (auto& lfo : lfoPool)
        lfo.prepare(TEST_SR);

    // Set LFO1 to 2 Hz sine, full depth, bipolar
    lfoPool[0].setRate(2.0f);
    lfoPool[0].setWaveform(WaveformType::Sine);
    lfoPool[0].setDepth(100.0f);
    lfoPool[0].setBipolar(true);

    // Create a modulation slot for filter_cutoff (slot 0)
    std::atomic<float> baseCutoff{ 1000.0f };
    ModulationSlot slot;
    slot.mod.source = ModSource::LFO1;
    slot.mod.depth  = 0.5f;
    slot.baseValuePtr = &baseCutoff;
    slot.paramId = "filter_cutoff";

    // Cache LFO output for one block
    float lfoVals[4] = {};
    runLfoPool(lfoPool, 0, lfoVals);

    SECTION("LFO at phase 0 (sine = 0) applies no modulation")
    {
        // phase 0: sine(0) = 0, depth 100% => lfo value = 0
        float result = applyModulation(slot, lfoVals, {});
        REQUIRE(result == Catch::Approx(1000.0f).margin(0.001f));
    }

    SECTION("LFO at phase 0.25 (sine peak = 1.0) applies +depth")
    {
        // Advance to phase 0.25
        lfoPool[0].reset();
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
        // LFO value at quarter cycle = 1.0 (sine peak, depth 100%)
        // Modulation = 1.0 * 0.5 = 0.5
        float result = applyModulation(slot, lfoVals, {});
        REQUIRE(result == Catch::Approx(1000.5f).margin(0.01f));
    }

    SECTION("LFO at phase 0.75 (sine trough = -1.0) applies -depth")
    {
        lfoPool[0].reset();
        // Advance to phase 0.75
        runLfoPool(lfoPool, samplesPerCycle(2.0f) * 3 / 4, lfoVals);
        // LFO value at three-quarter cycle = -1.0
        // Modulation = -1.0 * 0.5 = -0.5
        float result = applyModulation(slot, lfoVals, {});
        REQUIRE(result == Catch::Approx(999.5f).margin(0.01f));
    }

    SECTION("Modulation oscillates around base value over a full cycle")
    {
        lfoPool[0].reset();
        float prevVal = 1000.0f;
        float minVal = 1000.0f;
        float maxVal = 1000.0f;

        // Sample LFO at quarter-cycle intervals
        for (int step = 0; step < 4; ++step)
        {
            runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
            float result = applyModulation(slot, lfoVals, {});
            minVal = std::min(minVal, result);
            maxVal = std::max(maxVal, result);
        }

        // Over a full cycle, value should go above AND below base
        REQUIRE(maxVal > 1000.0f);
        REQUIRE(minVal < 1000.0f);
        // Oscillation should be symmetrical: ±0.5 around 1000
        REQUIRE(maxVal - 1000.0f == Catch::Approx(0.5f).margin(0.01f));
        REQUIRE(1000.0f - minVal == Catch::Approx(0.5f).margin(0.01f));
    }

    SECTION("Zero depth produces no modulation")
    {
        slot.mod.depth = 0.0f;
        lfoPool[0].reset();
        // Advance to peak
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
        float result = applyModulation(slot, lfoVals, {});
        REQUIRE(result == Catch::Approx(1000.0f).margin(0.001f));
    }

    SECTION("Negative depth inverts modulation")
    {
        slot.mod.depth = -0.5f;
        lfoPool[0].reset();
        // At peak (1.0): modulation = 1.0 * (-0.5) = -0.5
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
        float result = applyModulation(slot, lfoVals, {});
        REQUIRE(result == Catch::Approx(999.5f).margin(0.01f));
    }
}

//==============================================================================
// Test: ENV modulation applied to parameter
//==============================================================================
TEST_CASE("ENV modulation applied to parameter", "[mod][env][routing]")
{
    // Simulate the processor's envelope pool
    std::array<MultiPointEnvelope, 3> envPool;
    for (auto& env : envPool)
        env.prepare(TEST_SR);

    // Configure ENV1: attack=0.01s, decay=0.1s, sustain=0.7, release=0.2s
    envPool[0].setAttack(0.01f);
    envPool[0].setDecay(0.1f);
    envPool[0].setSustain(0.7f);
    envPool[0].setRelease(0.2f);

    // Create a modulation slot for master_vol (slot 13)
    std::atomic<float> baseVol{ 0.8f };
    ModulationSlot slot;
    slot.mod.source = ModSource::ENV1;
    slot.mod.depth  = 0.5f;
    slot.baseValuePtr = &baseVol;
    slot.paramId = "master_vol";

    SECTION("ENV with no trigger stays at zero")
    {
        float envVals[3] = {};
        runEnvPool(envPool, 0, envVals);
        // Untriggered envelope returns 0
        float result = applyModulation(slot, {}, envVals);
        REQUIRE(result == Catch::Approx(0.8f).margin(0.001f));
    }

    SECTION("ENV attack phase ramps to peak")
    {
        envPool[0].trigger();
        const int attackSamples = static_cast<int>(0.01 * TEST_SR); // 441 samples
        float envVals[3] = {};

        // Advance through attack phase
        runEnvPool(envPool, attackSamples, envVals);
        REQUIRE(envVals[0] == Catch::Approx(1.0f).margin(0.002f));

        // At peak: baseVol + 1.0 * 0.5 = 0.8 + 0.5 = 1.3
        float result = applyModulation(slot, {}, envVals);
        REQUIRE(result == Catch::Approx(1.3f).margin(0.01f));
    }

    SECTION("ENV sustain phase holds at sustain level")
    {
        envPool[0].trigger();
        // Advance through attack + decay to reach sustain
        const int sustainStart = static_cast<int>((0.01 + 0.1) * TEST_SR);
        float envVals[3] = {};
        runEnvPool(envPool, sustainStart, envVals);

        // Sustain value should be 0.7
        REQUIRE(envVals[0] == Catch::Approx(0.7f).margin(0.01f));

        // Modulation = 0.7 * 0.5 = 0.35
        float result = applyModulation(slot, {}, envVals);
        REQUIRE(result == Catch::Approx(1.15f).margin(0.02f));
    }

    SECTION("ENV release phase decays to zero after note-off")
    {
        envPool[0].trigger();
        // Advance well into sustain
        runEnvPool(envPool, static_cast<int>(0.5 * TEST_SR), {});

        // Trigger release
        envPool[0].release();

        // Advance through most of release
        const int releaseSamples = static_cast<int>(0.2 * TEST_SR);
        float envVals[3] = {};
        runEnvPool(envPool, releaseSamples, envVals);

        // After full release, envelope should be near 0
        REQUIRE(envVals[0] < 0.01f);
    }

    SECTION("Depth=0 with active ENV produces no modulation")
    {
        slot.mod.depth = 0.0f;
        envPool[0].trigger();
        const int attackSamples = static_cast<int>(0.01 * TEST_SR);
        float envVals[3] = {};
        runEnvPool(envPool, attackSamples, envVals);

        float result = applyModulation(slot, {}, envVals);
        REQUIRE(result == Catch::Approx(0.8f).margin(0.001f));
    }
}

//==============================================================================
// Test: Source switching during playback
//==============================================================================
TEST_CASE("Source switching during playback", "[mod][switch][routing]")
{
    std::array<LFOSystem, 4> lfoPool;
    std::array<MultiPointEnvelope, 3> envPool;
    for (auto& lfo : lfoPool) lfo.prepare(TEST_SR);
    for (auto& env : envPool) env.prepare(TEST_SR);

    // Set up LFO1 (2 Hz sine, bipolar, full depth)
    lfoPool[0].setRate(2.0f);
    lfoPool[0].setWaveform(WaveformType::Sine);
    lfoPool[0].setDepth(100.0f);
    lfoPool[0].setBipolar(true);

    // Set up ENV1 (attack=0.01s)
    envPool[0].setAttack(0.01f);
    envPool[0].setDecay(0.1f);
    envPool[0].setSustain(0.5f);
    envPool[0].setRelease(0.2f);

    // Modulation slot for filter_cutoff
    std::atomic<float> baseVal{ 1000.0f };
    ModulationSlot slot;
    slot.baseValuePtr = &baseVal;
    slot.mod.depth = 0.5f;
    slot.paramId = "filter_cutoff";

    SECTION("Switch from LFO1 to ENV1 changes modulated value")
    {
        slot.mod.source = ModSource::LFO1;
        float lfoVals[4] = {};
        float envVals[3] = {};

        // Advance to LFO peak
        lfoPool[0].reset();
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
        float lfoResult = applyModulation(slot, lfoVals, envVals);
        // LFO at peak = 1.0, depth 0.5 => 1000.0 + 0.5 = 1000.5
        REQUIRE(lfoResult == Catch::Approx(1000.5f).margin(0.01f));

        // Switch to ENV1 and trigger
        slot.mod.source = ModSource::ENV1;
        envPool[0].trigger();
        const int attackSamples = static_cast<int>(0.01 * TEST_SR);
        runEnvPool(envPool, attackSamples, envVals);
        float envResult = applyModulation(slot, lfoVals, envVals);
        // ENV at peak = 1.0, depth 0.5 => 1000.0 + 0.5 = 1000.5
        // (same value numerically, but from different source)
        REQUIRE(envResult == Catch::Approx(1000.5f).margin(0.01f));

        // Now advance LFO to different position while still on ENV1
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
        // LFO would now be at phase 0.5 (value 0), but ENV still active
        float envResult2 = applyModulation(slot, lfoVals, envVals);
        // ENV still holds at peak since we haven't advanced more
        REQUIRE(envResult2 == Catch::Approx(1000.5f).margin(0.01f));
    }

    SECTION("Switch from ENV1 to LFO1 mid-envelope")
    {
        slot.mod.source = ModSource::ENV1;
        envPool[0].trigger();

        // Advance ENV into sustain
        float envVals[3] = {};
        runEnvPool(envPool, static_cast<int>(0.5 * TEST_SR), envVals);
        float lfoVals[4] = {};

        // Switch to LFO1 at non-peak position
        slot.mod.source = ModSource::LFO1;
        lfoPool[0].reset();
        // Advance LFO to phase 0.5 (sine = 0)
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 2, lfoVals);
        float result = applyModulation(slot, lfoVals, envVals);
        // LFO at phase 0.5 = 0.0 => no modulation
        REQUIRE(result == Catch::Approx(1000.0f).margin(0.01f));

        // Advance LFO to quarter cycle (peak = 1.0) - verify clean continuation
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
        float result2 = applyModulation(slot, lfoVals, envVals);
        REQUIRE(result2 == Catch::Approx(1000.5f).margin(0.01f));
    }

    SECTION("Switch source twice during playback (no crash)")
    {
        slot.mod.source = ModSource::LFO1;
        float lfoVals[4] = {};
        float envVals[3] = {};

        lfoPool[0].reset();
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
        REQUIRE_NOTHROW(applyModulation(slot, lfoVals, envVals));

        // Switch to ENV1
        slot.mod.source = ModSource::ENV1;
        envPool[0].trigger();
        runEnvPool(envPool, 100, envVals);
        REQUIRE_NOTHROW(applyModulation(slot, lfoVals, envVals));

        // Switch to ENV2
        slot.mod.source = ModSource::ENV2;
        envPool[1].prepare(TEST_SR);
        envPool[1].trigger();
        runEnvPool(envPool, 100, envVals);
        REQUIRE_NOTHROW(applyModulation(slot, lfoVals, envVals));

        // Switch back to LFO1
        slot.mod.source = ModSource::LFO1;
        lfoPool[0].reset();
        runLfoPool(lfoPool, samplesPerCycle(2.0f) / 4, lfoVals);
        REQUIRE_NOTHROW(applyModulation(slot, lfoVals, envVals));
    }

    SECTION("Switch from OFF to LFO activates modulation cleanly")
    {
        slot.mod.source = ModSource::OFF;
        float lfoVals[4] = {};
        float envVals[3] = {};

        runLfoPool(lfoPool, 100, lfoVals);
        float offResult = applyModulation(slot, lfoVals, envVals);
        REQUIRE(offResult == Catch::Approx(1000.0f).margin(0.001f));

        // Switch to LFO1
        slot.mod.source = ModSource::LFO1;
        float lfoResult = applyModulation(slot, lfoVals, envVals);
        // Should now be modulated (LFO is at some non-zero phase)
        REQUIRE(lfoResult != Catch::Approx(1000.0f).margin(0.001f));
    }
}

//==============================================================================
// Test: Volume ADSR independence
//==============================================================================
TEST_CASE("Volume ADSR independent", "[mod][volume][adsr]")
{
    // The volume ADSR is a separate MultiPointEnvelope that acts as a VCA
    // multiplier. It is NOT part of the modulation bus / modSlots_.
    // This test verifies that LFO modulation on a slot does NOT affect
    // the volume envelope, and vice-versa.

    SECTION("Volume ADSR ramps independently of modulation slots")
    {
        // Create a volume ADSR (separate from the modulation system)
        MultiPointEnvelope volumeAdsr;
        volumeAdsr.prepare(TEST_SR);
        volumeAdsr.setAttack(1.0f);      // slow attack
        volumeAdsr.setDecay(0.3f);
        volumeAdsr.setSustain(0.8f);
        volumeAdsr.setRelease(0.5f);
        volumeAdsr.setLoopMode(LoopMode::Sustain);
        volumeAdsr.setLoopEnd(2);

        // Create modulation slot for filter_cutoff with active LFO
        std::array<LFOSystem, 4> lfoPool;
        for (auto& lfo : lfoPool) lfo.prepare(TEST_SR);

        lfoPool[0].setRate(5.0f);
        lfoPool[0].setWaveform(WaveformType::Sine);
        lfoPool[0].setDepth(100.0f);
        lfoPool[0].setBipolar(true);

        std::atomic<float> baseCutoff{ 1000.0f };
        ModulationSlot modSlot;
        modSlot.mod.source = ModSource::LFO1;
        modSlot.mod.depth = 1.0f; // full depth
        modSlot.baseValuePtr = &baseCutoff;
        modSlot.paramId = "filter_cutoff";

        // Trigger volume ADSR
        volumeAdsr.trigger();

        float lfoVals[4] = {};

        // Early in volume attack (t ~ 0.01s)
        int earlySamples = static_cast<int>(0.01 * TEST_SR);
        float volEarly = volumeAdsr.process(earlySamples);
        runLfoPool(lfoPool, earlySamples, lfoVals);

        // Volume should be low (early in slow 1s attack)
        REQUIRE(volEarly < 0.05f);

        // Modulation slot should have full LFO modulation regardless
        float modResult = applyModulation(modSlot, lfoVals, {});
        // LFO has advanced slightly - should be near some value
        REQUIRE(modResult >= 999.0f);
        REQUIRE(modResult <= 1001.0f);

        // Midway through volume attack (t ~ 0.5s)
        int midSamples = static_cast<int>(0.49 * TEST_SR);
        float volMid = volumeAdsr.process(midSamples);
        runLfoPool(lfoPool, midSamples, lfoVals);

        // Volume should be around halfway through attack
        REQUIRE(volMid > 0.45f);
        REQUIRE(volMid < 0.55f);

        // Modulation slot continues to oscillate independently
        modResult = applyModulation(modSlot, lfoVals, {});
        // LFO should have oscillated several times (5 Hz * 0.5s = 2.5 cycles)
        REQUIRE(std::abs(modResult - 1000.0f) <= 1.0f);
    }

    SECTION("Setting LFO depth on filter slot does not affect volume")
    {
        // The volume ADSR is a completely separate envelope instance.
        // Changing modulation parameters on a mod slot should have
        // zero impact on the volume envelope.

        MultiPointEnvelope volumeAdsr;
        volumeAdsr.prepare(TEST_SR);
        volumeAdsr.setAttack(0.1f);
        volumeAdsr.setSustain(1.0f);
        volumeAdsr.setLoopMode(LoopMode::Sustain);
        volumeAdsr.setLoopEnd(2);

        // Get a baseline
        volumeAdsr.trigger();
        float baseline = volumeAdsr.process(static_cast<int>(0.05 * TEST_SR));

        // Modulation slot - does not touch the volume ADSR
        std::atomic<float> baseCutoff{ 1000.0f };
        ModulationSlot slot;
        slot.mod.source = ModSource::LFO1;
        slot.mod.depth = 0.0f;
        slot.baseValuePtr = &baseCutoff;

        // Change modulation parameters repeatedly
        slot.mod.depth = 0.5f;
        slot.mod.source = ModSource::LFO2;
        slot.mod.depth = -0.3f;
        slot.mod.source = ModSource::ENV1;
        slot.mod.depth = 1.0f;

        // Volume ADSR should be completely unaffected
        float after = volumeAdsr.getValue();
        REQUIRE(after == Catch::Approx(baseline).margin(0.001f));
    }

    SECTION("Volume ADSR can be triggered and released independently")
    {
        MultiPointEnvelope volumeAdsr;
        volumeAdsr.prepare(TEST_SR);
        volumeAdsr.setAttack(0.1f);
        volumeAdsr.setDecay(0.2f);
        volumeAdsr.setSustain(0.6f);
        volumeAdsr.setRelease(0.3f);
        volumeAdsr.setLoopMode(LoopMode::Sustain);
        volumeAdsr.setLoopEnd(2);

        // Trigger independently
        volumeAdsr.trigger();
        REQUIRE(volumeAdsr.isActive());

        // Release independently
        volumeAdsr.release();
        // After release, envelope should eventually decay to 0
        float finalVal = volumeAdsr.process(static_cast<int>(0.5 * TEST_SR));
        REQUIRE(finalVal < 0.01f);
    }
}

//==============================================================================
// Test: OFF source bypass
//==============================================================================
TEST_CASE("OFF source bypass", "[mod][off][routing]")
{
    std::array<LFOSystem, 4> lfoPool;
    std::array<MultiPointEnvelope, 3> envPool;
    for (auto& lfo : lfoPool) lfo.prepare(TEST_SR);
    for (auto& env : envPool) env.prepare(TEST_SR);

    // Activate LFO and ENV sources
    lfoPool[0].setRate(5.0f);
    lfoPool[0].setDepth(100.0f);
    envPool[0].setAttack(0.01f);
    envPool[0].trigger();

    // Multiple slots with different base values
    std::atomic<float> baseCutoff{ 1000.0f };
    std::atomic<float> baseRes{ 0.5f };
    std::atomic<float> baseVol{ 0.8f };

    constexpr int numSlots = 3;
    ModulationSlot slots[numSlots];
    slots[0] = { ModulationConnection(), &baseCutoff, 0.0f, "filter_cutoff" };
    slots[1] = { ModulationConnection(), &baseRes,    0.0f, "filter_res" };
    slots[2] = { ModulationConnection(), &baseVol,    0.0f, "master_vol" };

    float lfoVals[4] = {};
    float envVals[3] = {};
    runLfoPool(lfoPool, 100, lfoVals);
    runEnvPool(envPool, 100, envVals);

    SECTION("All slots at OFF produce base values only")
    {
        for (auto& s : slots)
            s.mod.source = ModSource::OFF;

        for (auto& s : slots)
        {
            float result = applyModulation(s, lfoVals, envVals);
            float expected = s.baseValuePtr->load(std::memory_order_relaxed);
            REQUIRE(result == Catch::Approx(expected).margin(0.0001f));
        }
    }

    SECTION("Switching one slot to LFO while others stay OFF")
    {
        for (auto& s : slots)
            s.mod.source = ModSource::OFF;

        // Activate only slot 0 with LFO1
        slots[0].mod.source = ModSource::LFO1;
        slots[0].mod.depth = 0.5f;

        // Slot 0 should show modulation
        float r0 = applyModulation(slots[0], lfoVals, envVals);
        REQUIRE(r0 != Catch::Approx(1000.0f).margin(0.001f));

        // Other slots stay at base
        float r1 = applyModulation(slots[1], lfoVals, envVals);
        REQUIRE(r1 == Catch::Approx(0.5f).margin(0.0001f));

        float r2 = applyModulation(slots[2], lfoVals, envVals);
        REQUIRE(r2 == Catch::Approx(0.8f).margin(0.0001f));
    }

    SECTION("Rapidly toggle OFF and verify clean base values")
    {
        for (int iter = 0; iter < 10; ++iter)
        {
            for (auto& s : slots)
            {
                s.mod.source = ModSource::LFO1;
                s.mod.depth = 1.0f;
                applyModulation(s, lfoVals, envVals);
            }

            // Switch all back to OFF
            for (auto& s : slots)
            {
                s.mod.source = ModSource::OFF;
                float result = applyModulation(s, lfoVals, envVals);
                float expected = s.baseValuePtr->load(std::memory_order_relaxed);
                REQUIRE(result == Catch::Approx(expected).margin(0.0001f));
            }
        }
    }
}

//==============================================================================
// Test: LFO pool isolation
//==============================================================================
TEST_CASE("LFO pool isolation", "[mod][lfo][isolation]")
{
    // Each LFO in the pool operates independently with its own rate,
    // waveform, phase, and depth settings.

    std::array<LFOSystem, 4> lfoPool;
    for (auto& lfo : lfoPool)
    {
        lfo.prepare(TEST_SR);
        lfo.setWaveform(WaveformType::Sine);
        lfo.setDepth(100.0f);
        lfo.setBipolar(true);
    }

    // LFO1: 2 Hz
    lfoPool[0].setRate(2.0f);
    // LFO2: 8 Hz
    lfoPool[1].setRate(8.0f);

    SECTION("LFO1 and LFO2 produce different values at same sample offset")
    {
        float vals[4] = {};
        const int numSamples = samplesPerCycle(1.0f); // advance 1 second

        runLfoPool(lfoPool, numSamples, vals);

        // After 1 second at 2 Hz: 2 full cycles, back to phase 0 (sine = 0)
        REQUIRE(vals[0] == Catch::Approx(0.0f).margin(0.001f));

        // After 1 second at 8 Hz: 8 full cycles, back to phase 0 (sine = 0)
        REQUIRE(vals[1] == Catch::Approx(0.0f).margin(0.001f));
    }

    SECTION("LFO1 and LFO2 at non-aligned phases produce independent values")
    {
        float vals[4] = {};

        // Advance 1/8 second: LFO1 at phase 0.25 (peak=1.0), LFO2 at phase 1.0 (0)
        const int eighthSec = static_cast<int>(TEST_SR / 8);
        runLfoPool(lfoPool, eighthSec, vals);

        // LFO1: 2 Hz → 0.25 cycles in 1/8s → peak
        REQUIRE(vals[0] == Catch::Approx(1.0f).margin(0.01f));

        // LFO2: 8 Hz → 1 full cycle in 1/8s → back to 0
        REQUIRE(vals[1] == Catch::Approx(0.0f).margin(0.01f));
    }

    SECTION("LFO3 default (1 Hz) is independent of LFO1/2")
    {
        // LFO3 is at default 1 Hz
        float vals[4] = {};
        const int quarterSec = static_cast<int>(TEST_SR / 4);

        runLfoPool(lfoPool, quarterSec, vals);

        // LFO1 at 2 Hz: 1/4s = 0.5 cycles → phase 0.5 → sine(pi) = 0
        REQUIRE(vals[0] == Catch::Approx(0.0f).margin(0.01f));

        // LFO2 at 8 Hz: 1/4s = 2 cycles → phase 0 → 0
        REQUIRE(vals[1] == Catch::Approx(0.0f).margin(0.01f));

        // LFO3 at 1 Hz: 1/4s = 0.25 cycles → phase 0.25 → peak
        REQUIRE(vals[2] == Catch::Approx(1.0f).margin(0.01f));
    }

    SECTION("Changing LFO1 depth does not affect LFO2 output")
    {
        float vals[4] = {};

        // Set LFO1 to 50% depth
        lfoPool[0].setDepth(50.0f);

        const int eighthSec = static_cast<int>(TEST_SR / 8);
        runLfoPool(lfoPool, eighthSec, vals);

        // LFO1 at 50%: peak = 0.5
        REQUIRE(vals[0] == Catch::Approx(0.5f).margin(0.01f));

        // LFO2 at 100%: after 1/8s = 1 full cycle at 8Hz → 0
        REQUIRE(vals[1] == Catch::Approx(0.0f).margin(0.01f));

        // Reset and check LFO2 at a different phase is still at 100%
        lfoPool[1].reset();
        const int sixteenthSec = static_cast<int>(TEST_SR / 16);
        runLfoPool(lfoPool, sixteenthSec, vals);

        // LFO1 at 50%: 2Hz * 1/16s = 0.125 cycles → sin(pi/4) ≈ 0.707 * 0.5 = 0.354
        REQUIRE(vals[0] == Catch::Approx(0.354f).margin(0.01f));

        // LFO2 at 100%: 8Hz * 1/16s = 0.5 cycles → sin(pi) = 0
        REQUIRE(vals[1] == Catch::Approx(0.0f).margin(0.01f));
    }

    SECTION("All 4 LFOs produce independent waveforms when configured differently")
    {
        lfoPool[0].setRate(1.0f);
        lfoPool[0].setWaveform(WaveformType::Sine);
        lfoPool[1].setRate(2.0f);
        lfoPool[1].setWaveform(WaveformType::Triangle);
        lfoPool[2].setRate(3.0f);
        lfoPool[2].setWaveform(WaveformType::Saw);
        lfoPool[3].setRate(4.0f);
        lfoPool[3].setWaveform(WaveformType::Square);

        float vals[4] = {};
        const int numSamples = static_cast<int>(TEST_SR);

        // Run for a while to let them diverge
        for (int block = 0; block < 10; ++block)
            runLfoPool(lfoPool, 100, vals);

        // Each should be producing values - at least some should differ
        // (statistically very unlikely all 4 produce same value with different
        //  rates and waveforms after 1000 samples)
        bool allSame = (vals[0] == vals[1] && vals[1] == vals[2] && vals[2] == vals[3]);
        REQUIRE_FALSE(allSame);
    }
}

//==============================================================================
// Test: ENV pool isolation
//==============================================================================
TEST_CASE("ENV pool isolation", "[mod][env][isolation]")
{
    // Each envelope in the pool operates independently with its own
    // ADSR parameters, trigger/release lifecycle, and output value.

    std::array<MultiPointEnvelope, 3> envPool;
    for (auto& env : envPool)
        env.prepare(TEST_SR);

    SECTION("ENV1 and ENV2 produce different outputs with different attacks")
    {
        // ENV1: fast attack (0.01s)
        envPool[0].setAttack(0.01f);
        envPool[0].setDecay(0.1f);
        envPool[0].setSustain(0.7f);
        envPool[0].setRelease(0.3f);

        // ENV2: slow attack (1.0s)
        envPool[1].setAttack(1.0f);
        envPool[1].setDecay(0.2f);
        envPool[1].setSustain(0.5f);
        envPool[1].setRelease(0.5f);

        // Trigger both simultaneously
        envPool[0].trigger();
        envPool[1].trigger();

        float vals[3] = {};

        // Advance 0.05s (50ms)
        const int early = static_cast<int>(0.05 * TEST_SR);
        runEnvPool(envPool, early, vals);

        // ENV1 should be past attack peak and into decay
        REQUIRE(vals[0] < 1.0f);
        REQUIRE(vals[0] > 0.5f);

        // ENV2 should still be early in its 1s attack
        REQUIRE(vals[1] < 0.06f); // ~0.05/1.0 = 0.05

        // Advance to 0.5s
        const int mid = static_cast<int>(0.45 * TEST_SR);
        runEnvPool(envPool, mid, vals);

        // ENV1 should be well into sustain
        REQUIRE(vals[0] == Catch::Approx(0.7f).margin(0.05f));

        // ENV2 should be at attack peak (~0.5)
        REQUIRE(vals[1] == Catch::Approx(0.5f).margin(0.05f));
    }

    SECTION("ENV1 release does not affect ENV2")
    {
        envPool[0].setAttack(0.01f);
        envPool[0].setDecay(0.1f);
        envPool[0].setSustain(0.8f);
        envPool[0].setRelease(0.2f);

        envPool[1].setAttack(0.01f);
        envPool[1].setDecay(0.1f);
        envPool[1].setSustain(0.8f);
        envPool[1].setRelease(1.0f);

        // Trigger both
        envPool[0].trigger();
        envPool[1].trigger();

        // Advance into sustain
        runEnvPool(envPool, static_cast<int>(0.5 * TEST_SR), {});

        // Release only ENV1
        envPool[0].release();

        // Advance through ENV1's short release
        runEnvPool(envPool, static_cast<int>(0.3 * TEST_SR), {});

        // ENV1 should be finished (near 0)
        REQUIRE_FALSE(envPool[0].isActive());
        REQUIRE(envPool[0].getValue() < 0.01f);

        // ENV2 should still be active (long release not started)
        REQUIRE(envPool[1].isActive());
        REQUIRE(envPool[1].getValue() > 0.5f);
    }

    SECTION("ENV3 independent ADSR parameters")
    {
        // Set all three to known configurations
        envPool[0].setAttack(0.5f);
        envPool[1].setSustain(0.3f);
        envPool[2].setRelease(2.0f);

        // Verify independent parameter storage
        REQUIRE(envPool[0].getAttack() == Catch::Approx(0.5f));
        REQUIRE(envPool[1].getSustain() == Catch::Approx(0.3f));
        REQUIRE(envPool[2].getRelease() == Catch::Approx(2.0f));

        // Default values for unconfigured params
        REQUIRE(envPool[0].getSustain() == Catch::Approx(0.7f)); // default
        REQUIRE(envPool[1].getAttack() == Catch::Approx(0.01f)); // default
    }

    SECTION("ENV3 can be triggered independently after ENV1/2 finish")
    {
        // Short envelope
        envPool[0].setAttack(0.01f);
        envPool[0].setDecay(0.01f);
        envPool[0].setSustain(0.0f);
        envPool[0].setRelease(0.01f);

        envPool[0].trigger();
        runEnvPool(envPool, static_cast<int>(0.1 * TEST_SR), {});

        // ENV1 should be done
        REQUIRE_FALSE(envPool[0].isActive());

        // Trigger ENV2 fresh
        envPool[1].trigger();
        REQUIRE(envPool[1].isActive());

        // ENV3 remains idle
        REQUIRE_FALSE(envPool[2].isActive());
    }
}

//==============================================================================
// Test: Modulation serialization round-trip
//==============================================================================
TEST_CASE("Modulation serialization round-trip", "[mod][serialize]")
{
    // ModulationConnection is a plain-data struct that can be serialized
    // by copying its fields. This test verifies that a ModulationConnection
    // survives a save-clear-restore cycle.

    SECTION("ModulationConnection save and restore via field copy")
    {
        // Set up original modulation connection
        ModulationConnection original;
        original.source = ModSource::LFO2;
        original.depth  = 0.75f;
        original.curve  = 2.0f;

        // Serialize: copy fields to a plain struct
        struct SerializedMod {
            int source;
            float depth;
            float curve;
        };
        SerializedMod saved;
        saved.source = static_cast<int>(original.source);
        saved.depth  = original.depth;
        saved.curve  = original.curve;

        // Clear original
        ModulationConnection restored;
        restored.source = ModSource::OFF;
        restored.depth  = 0.0f;
        restored.curve  = 1.0f;

        // Verify cleared state differs from original
        REQUIRE(restored.source != original.source);
        REQUIRE(restored.depth  != original.depth);
        REQUIRE(restored.curve  != original.curve);

        // Restore from serialized data
        restored.source = static_cast<ModSource>(saved.source);
        restored.depth  = saved.depth;
        restored.curve  = saved.curve;

        // Verify round-trip
        REQUIRE(restored.source == original.source);
        REQUIRE(restored.source == ModSource::LFO2);
        REQUIRE(restored.depth  == Catch::Approx(0.75f));
        REQUIRE(restored.curve  == Catch::Approx(2.0f));
    }

    SECTION("ModulationSlot save and restore via MemoryBlock")
    {
        // Set up a complete modulation slot
        std::atomic<float> baseVal{ 440.0f };
        ModulationSlot original;
        original.mod.source = ModSource::ENV2;
        original.mod.depth  = -0.5f;
        original.mod.curve  = 1.5f;
        original.baseValuePtr = &baseVal;
        original.modulatedValue = 439.5f; // example computed value
        original.paramId = "ring_freq";

        // Serialize using JUCE MemoryBlock
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream stream(block, false);
            stream.writeInt(static_cast<int>(original.mod.source));
            stream.writeFloat(original.mod.depth);
            stream.writeFloat(original.mod.curve);
            stream.writeString(original.paramId);
        }

        // Clear and restore
        ModulationSlot restored;
        restored.mod.source = ModSource::OFF;
        restored.mod.depth  = 0.0f;
        restored.mod.curve  = 1.0f;
        restored.paramId    = {};
        restored.baseValuePtr = nullptr;
        restored.modulatedValue = 0.0f;

        {
            juce::MemoryInputStream stream(block, false);
            restored.mod.source = static_cast<ModSource>(stream.readInt());
            restored.mod.depth  = stream.readFloat();
            restored.mod.curve  = stream.readFloat();
            restored.paramId    = stream.readString();
        }

        // Verify round-trip
        REQUIRE(restored.mod.source == original.mod.source);
        REQUIRE(restored.mod.source == ModSource::ENV2);
        REQUIRE(restored.mod.depth  == Catch::Approx(-0.5f));
        REQUIRE(restored.mod.curve  == Catch::Approx(1.5f));
        REQUIRE(restored.paramId    == "ring_freq");
    }

    SECTION("Multiple modulation routes round-trip")
    {
        // Save a table of modulation connections
        constexpr int numRoutes = 4;
        ModulationConnection routes[numRoutes];
        routes[0] = { ModSource::LFO1,  0.5f, 1.0f };
        routes[1] = { ModSource::LFO2, -0.3f, 2.0f };
        routes[2] = { ModSource::ENV1,  1.0f, 1.0f };
        routes[3] = { ModSource::OFF,   0.0f, 1.0f };

        // Serialize all routes
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream stream(block, false);
            stream.writeInt(numRoutes);
            for (const auto& r : routes)
            {
                stream.writeInt(static_cast<int>(r.source));
                stream.writeFloat(r.depth);
                stream.writeFloat(r.curve);
            }
        }

        // Restore
        ModulationConnection restored[numRoutes];
        for (auto& r : restored)
            r = { ModSource::OFF, 0.0f, 1.0f };

        {
            juce::MemoryInputStream stream(block, false);
            int count = stream.readInt();
            REQUIRE(count == numRoutes);
            for (int i = 0; i < count; ++i)
            {
                restored[i].source = static_cast<ModSource>(stream.readInt());
                restored[i].depth  = stream.readFloat();
                restored[i].curve  = stream.readFloat();
            }
        }

        // Verify all routes match
        for (int i = 0; i < numRoutes; ++i)
        {
            INFO("Route " << i);
            REQUIRE(restored[i].source == routes[i].source);
            REQUIRE(restored[i].depth  == Catch::Approx(routes[i].depth));
            REQUIRE(restored[i].curve  == Catch::Approx(routes[i].curve));
        }
    }

    SECTION("Edge cases: extreme depth and curve values")
    {
        // Test edge cases of the data range
        std::atomic<float> baseVal{ 1.0f };
        ModulationSlot slot;
        slot.mod.source = ModSource::LFO4;
        slot.mod.depth  = -1.0f; // max negative depth
        slot.mod.curve  = 0.1f;  // extreme curve
        slot.baseValuePtr = &baseVal;
        slot.paramId = "spare";

        // Serialize
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream stream(block, false);
            stream.writeInt(static_cast<int>(slot.mod.source));
            stream.writeFloat(slot.mod.depth);
            stream.writeFloat(slot.mod.curve);
            stream.writeString(slot.paramId);
        }

        // Restore
        ModulationSlot restored;
        {
            juce::MemoryInputStream stream(block, false);
            restored.mod.source = static_cast<ModSource>(stream.readInt());
            restored.mod.depth  = stream.readFloat();
            restored.mod.curve  = stream.readFloat();
            restored.paramId    = stream.readString();
        }

        REQUIRE(restored.mod.source == ModSource::LFO4);
        REQUIRE(restored.mod.depth  == Catch::Approx(-1.0f));
        REQUIRE(restored.mod.curve  == Catch::Approx(0.1f));
        REQUIRE(restored.paramId    == "spare");
    }

    SECTION("OFF source with zero depth survives round-trip")
    {
        ModulationConnection original;
        original.source = ModSource::OFF;
        original.depth  = 0.0f;
        original.curve  = 1.0f;

        // Binary copy (simplest serialization for a POD struct)
        ModulationConnection restored;
        std::memcpy(&restored, &original, sizeof(ModulationConnection));

        REQUIRE(restored.source == ModSource::OFF);
        REQUIRE(restored.depth  == Catch::Approx(0.0f));
        REQUIRE(restored.curve  == Catch::Approx(1.0f));
    }
}

//==============================================================================
// Test: ModulationBus integration (the alternative routing system)
//==============================================================================
TEST_CASE("ModulationBus routing", "[mod][bus][routing]")
{
    // The ModulationBus is the alternative routing system that connects
    // source value pointers directly to target atomics. This is separate
    // from the flat-array ModulationSlot system.

    SECTION("ModulationBus apply scaling to routes")
    {
        ModulationBus bus;

        std::atomic<float> target{ 0.0f };
        float sourceVal = 1.0f;

        bus.addRoute(ModulationBus::Source::LFO, 0, "test",
                     &target, &sourceVal, 0.5f);

        REQUIRE(bus.getNumRoutes() == 1);

        bus.processBlock(64);

        // target = sourceVal * depth = 1.0 * 0.5 = 0.5
        float result = target.load(std::memory_order_relaxed);
        REQUIRE(result == Catch::Approx(0.5f));
    }

    SECTION("ModulationBus multiple routes accumulate independently")
    {
        ModulationBus bus;

        std::atomic<float> target1{ 0.0f };
        std::atomic<float> target2{ 0.0f };
        float sourceVal1 = 0.8f;
        float sourceVal2 = 0.3f;

        bus.addRoute(ModulationBus::Source::LFO, 0, "param1",
                     &target1, &sourceVal1, 1.0f);
        bus.addRoute(ModulationBus::Source::Envelope, 0, "param2",
                     &target2, &sourceVal2, 0.5f);

        bus.processBlock(64);

        REQUIRE(target1.load() == Catch::Approx(0.8f));
        REQUIRE(target2.load() == Catch::Approx(0.15f));
    }

    SECTION("ModulationBus clear removes all routes")
    {
        ModulationBus bus;
        std::atomic<float> target{ 0.0f };
        float sourceVal = 1.0f;

        bus.addRoute(ModulationBus::Source::LFO, 0, "test",
                     &target, &sourceVal, 1.0f);
        REQUIRE(bus.getNumRoutes() == 1);

        bus.clear();
        REQUIRE(bus.getNumRoutes() == 0);
    }

    SECTION("ModulationBus null pointers are safely ignored")
    {
        ModulationBus bus;
        std::atomic<float> target{ 0.0f };

        // Null source value pointer
        bus.addRoute(ModulationBus::Source::LFO, 0, "test",
                     &target, nullptr, 1.0f);

        // Should not crash
        REQUIRE_NOTHROW(bus.processBlock(64));
        // Target should remain unchanged
        REQUIRE(target.load() == Catch::Approx(0.0f));
    }
}
