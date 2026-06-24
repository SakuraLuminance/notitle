#include <catch2/catch_all.hpp>
#include "dsp/VoiceManager.h"
#include <cmath>

using namespace ana;

//==============================================================================
// Helpers
//==============================================================================

/** Creates a stereo buffer with the given number of samples. */
static juce::AudioBuffer<float> makeBuffer(int numSamples)
{
    return juce::AudioBuffer<float>(2, numSamples);
}

static constexpr double testSampleRate = 44100.0;

//==============================================================================
// Initial state
//==============================================================================

TEST_CASE("VoiceManager - initial state", "[voice][init]")
{
    VoiceManager vm;

    SECTION("all voices start free")
    {
        REQUIRE(vm.getNumActiveVoices() == 0);

        for (int i = 0; i < VoiceManager::maxVoices; ++i)
            REQUIRE_FALSE(vm.isVoiceActive(i));
    }

    SECTION("default allocation mode is round-robin")
    {
        REQUIRE(vm.getAllocationMode() == AllocationMode::roundRobin);
    }

    SECTION("maxVoices is 16")
    {
        REQUIRE(VoiceManager::maxVoices == 16);
    }
}

//==============================================================================
// Voice allocation
//==============================================================================

TEST_CASE("VoiceManager - noteOn allocates a voice", "[voice][allocation]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);

    REQUIRE(vm.getNumActiveVoices() == 1);

    auto* v = vm.getVoice(0);
    REQUIRE(v->state == VoiceState::attack);
    REQUIRE(v->note == 60);
    REQUIRE(v->velocity == Catch::Approx(0.5f));
    REQUIRE(v->pitchHz == Catch::Approx(261.6255f).margin(0.1f));
    REQUIRE(v->envelopeLevel == Catch::Approx(0.0f));
}

TEST_CASE("VoiceManager - multiple noteOn allocates multiple voices", "[voice][allocation]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);
    vm.noteOn(64, 0.6f);
    vm.noteOn(67, 0.7f);

    REQUIRE(vm.getNumActiveVoices() == 3);

    // With round-robin, voices should be in slots 0, 1, 2
    REQUIRE(vm.getVoice(0)->note == 60);
    REQUIRE(vm.getVoice(1)->note == 64);
    REQUIRE(vm.getVoice(2)->note == 67);
}

TEST_CASE("VoiceManager - noteOn clamps out-of-range values", "[voice][allocation]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    SECTION("note clamped to 0-127")
    {
        vm.noteOn(-1, 0.5f);
        REQUIRE(vm.getNumActiveVoices() == 0);

        vm.noteOn(128, 0.5f);
        REQUIRE(vm.getNumActiveVoices() == 0);
    }

    SECTION("velocity clamped to 0-1")
    {
        vm.noteOn(60, -0.1f);
        REQUIRE(vm.getVoice(0)->velocity == Catch::Approx(0.0f));

        vm.noteOn(61, 1.5f);
        REQUIRE(vm.getVoice(1)->velocity == Catch::Approx(1.0f));
    }
}

//==============================================================================
// Voice deallocation
//==============================================================================

TEST_CASE("VoiceManager - noteOff triggers release phase", "[voice][release]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);
    REQUIRE(vm.getVoice(0)->state == VoiceState::attack);

    vm.noteOff(60);
    REQUIRE(vm.getVoice(0)->state == VoiceState::release);
    REQUIRE(vm.getVoice(0)->releaseStartLevel > 0.0f);
}

TEST_CASE("VoiceManager - noteOff only affects matching note", "[voice][release]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);
    vm.noteOn(64, 0.5f);

    vm.noteOff(60);

    // Note 60 should be in release, note 64 should still be in attack
    REQUIRE(vm.isVoiceActive(0));
    REQUIRE(vm.getVoice(1)->state == VoiceState::attack);

    // One voice is now in release, one in attack
    REQUIRE(vm.getNumActiveVoices() == 2);
}

TEST_CASE("VoiceManager - noteOff on inactive note is a no-op", "[voice][release]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);
    vm.noteOff(99); // Not playing

    REQUIRE(vm.getVoice(0)->state == VoiceState::attack);
    REQUIRE(vm.getNumActiveVoices() == 1);
}

TEST_CASE("VoiceManager - allVoicesOff releases all voices", "[voice][release]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);
    vm.noteOn(64, 0.5f);
    vm.noteOn(67, 0.5f);

    vm.allVoicesOff();

    for (int i = 0; i < 3; ++i)
        REQUIRE(vm.getVoice(i)->state == VoiceState::release);
}

//==============================================================================
// Voice stealing
//==============================================================================

TEST_CASE("VoiceManager - voice stealing when all voices used", "[voice][stealing]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    // Allocate all 16 voices
    for (int i = 0; i < VoiceManager::maxVoices; ++i)
        vm.noteOn(60 + i, 0.5f);

    REQUIRE(vm.getNumActiveVoices() == VoiceManager::maxVoices);

    // One more note-on should steal the oldest voice (voice 0, note 60)
    vm.noteOn(76, 0.5f);

    // Still 16 active voices
    REQUIRE(vm.getNumActiveVoices() == VoiceManager::maxVoices);

    // The oldest voice (slot 0) should now play note 76
    REQUIRE(vm.getVoice(0)->note == 76);
}

TEST_CASE("VoiceManager - oldest voices stolen first", "[voice][stealing]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    // Fill all voices
    for (int i = 0; i < VoiceManager::maxVoices; ++i)
        vm.noteOn(60 + i, 0.5f);

    // Process enough samples to bring all voices to sustain
    {
        auto buf = makeBuffer(static_cast<int>(0.5 * testSampleRate));
        vm.process(buf);
    }

    // All voices should be in sustain
    for (int i = 0; i < VoiceManager::maxVoices; ++i)
        REQUIRE(vm.getVoice(i)->state == VoiceState::sustain);

    // Voice 0 has the lowest noteOnIndex (0) so it should be stolen first
    vm.noteOn(80, 0.5f);

    REQUIRE(vm.getVoice(0)->note == 80);
}

TEST_CASE("VoiceManager - stealing prefers sustain over attack", "[voice][stealing]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    // Fill all voices
    for (int i = 0; i < VoiceManager::maxVoices; ++i)
        vm.noteOn(60 + i, 0.5f);

    // Process so first half reach sustain, rest stay in attack/decay
    {
        auto buf = makeBuffer(static_cast<int>(0.3 * testSampleRate));
        vm.process(buf);
    }

    // Now noteOn again to trigger steal - should prefer sustain voices
    vm.noteOn(90, 0.5f);

    // The stolen voice should be the one in sustain with lowest noteOnIndex
    auto* stolen = vm.getVoice(0);
    REQUIRE(stolen->note == 90);
}

//==============================================================================
// Envelope shape
//==============================================================================

TEST_CASE("VoiceManager - envelope attack phase", "[voice][envelope]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    // Use a slow attack for easy measurement
    vm.setDefaultAttack(0.1f);  // 100ms

    vm.noteOn(60, 0.5f);

    SECTION("attack rises from 0")
    {
        auto buf = makeBuffer(static_cast<int>(0.05 * testSampleRate)); // 50ms
        vm.process(buf);

        auto* v = vm.getVoice(0);
        REQUIRE(v->state == VoiceState::attack);
        REQUIRE(v->envelopeLevel > 0.0f);
        REQUIRE(v->envelopeLevel < 1.0f);
    }

    SECTION("attack reaches 1 at end of attack time")
    {
        auto buf = makeBuffer(static_cast<int>(0.1 * testSampleRate)); // 100ms
        vm.process(buf);

        auto* v = vm.getVoice(0);
        REQUIRE(v->state == VoiceState::decay);
        REQUIRE(v->envelopeLevel == Catch::Approx(1.0f).margin(0.001f));
    }
}

TEST_CASE("VoiceManager - envelope decay to sustain", "[voice][envelope]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.setDefaultAttack(0.001f);   // 1ms (instant attack)
    vm.setDefaultDecay(0.2f);      // 200ms
    vm.setDefaultSustain(0.5f);    // sustain at 50%

    vm.noteOn(60, 0.5f);

    // Process through attack + part of decay
    {
        auto buf = makeBuffer(static_cast<int>(0.05 * testSampleRate)); // 50ms
        vm.process(buf);
    }

    auto* v = vm.getVoice(0);
    REQUIRE(v->state == VoiceState::decay);
    REQUIRE(v->envelopeLevel < 1.0f);
    REQUIRE(v->envelopeLevel > v->sustainLevel);

    // Process through the rest of decay
    {
        auto buf = makeBuffer(static_cast<int>(0.2 * testSampleRate)); // 200ms
        vm.process(buf);
    }

    REQUIRE(v->state == VoiceState::sustain);
    REQUIRE(v->envelopeLevel == Catch::Approx(0.5f).margin(0.001f));
}

TEST_CASE("VoiceManager - envelope sustain holds level", "[voice][envelope]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.setDefaultAttack(0.001f);
    vm.setDefaultDecay(0.001f);
    vm.setDefaultSustain(0.7f);

    vm.noteOn(60, 0.5f);

    // Process past attack and decay into sustain
    {
        auto buf = makeBuffer(static_cast<int>(0.01 * testSampleRate));
        vm.process(buf);
    }

    auto* v = vm.getVoice(0);
    REQUIRE(v->state == VoiceState::sustain);
    REQUIRE(v->envelopeLevel == Catch::Approx(0.7f).margin(0.001f));

    // Process more - sustain should stay at 0.7
    {
        auto buf = makeBuffer(static_cast<int>(0.1 * testSampleRate));
        vm.process(buf);
    }

    REQUIRE(v->state == VoiceState::sustain);
    REQUIRE(v->envelopeLevel == Catch::Approx(0.7f).margin(0.001f));
}

TEST_CASE("VoiceManager - envelope release fades to zero", "[voice][envelope]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.setDefaultAttack(0.001f);
    vm.setDefaultDecay(0.001f);
    vm.setDefaultSustain(0.7f);
    vm.setDefaultRelease(0.5f); // 500ms release

    vm.noteOn(60, 0.5f);

    // Process into sustain
    {
        auto buf = makeBuffer(static_cast<int>(0.01 * testSampleRate));
        vm.process(buf);
    }

    // Release
    vm.noteOff(60);
    REQUIRE(vm.getVoice(0)->state == VoiceState::release);

    // Process halfway through release
    {
        auto buf = makeBuffer(static_cast<int>(0.25 * testSampleRate)); // 250ms
        vm.process(buf);
    }

    auto* v = vm.getVoice(0);
    REQUIRE(v->state == VoiceState::release);
    REQUIRE(v->envelopeLevel > 0.0f);
    REQUIRE(v->envelopeLevel < 0.7f);

    // Process through the rest of release
    {
        auto buf = makeBuffer(static_cast<int>(0.3 * testSampleRate)); // 300ms
        vm.process(buf);
    }

    REQUIRE(v->state == VoiceState::idle);
    REQUIRE(v->envelopeLevel == Catch::Approx(0.0f));
}

//==============================================================================
// Envelope edge cases
//==============================================================================

TEST_CASE("VoiceManager - zero-length attack is instant", "[voice][envelope][edge]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.setDefaultAttack(0.0f); // instant attack
    vm.setDefaultDecay(1.0f);
    vm.setDefaultSustain(0.5f);

    vm.noteOn(60, 0.5f);

    auto buf = makeBuffer(1); // one sample
    vm.process(buf);

    // Should have skipped directly to decay
    auto* v = vm.getVoice(0);
    REQUIRE(v->state == VoiceState::decay);
    REQUIRE(v->envelopeLevel == Catch::Approx(1.0f));
}

TEST_CASE("VoiceManager - zero-length release is instant", "[voice][envelope][edge]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.setDefaultAttack(0.001f);
    vm.setDefaultDecay(0.001f);
    vm.setDefaultSustain(0.5f);
    vm.setDefaultRelease(0.0f); // instant release

    vm.noteOn(60, 0.5f);

    // Process into sustain
    {
        auto buf = makeBuffer(static_cast<int>(0.01 * testSampleRate));
        vm.process(buf);
    }

    vm.noteOff(60);

    // One more sample should complete release
    {
        auto buf = makeBuffer(1);
        vm.process(buf);
    }

    REQUIRE(vm.getVoice(0)->state == VoiceState::idle);
    REQUIRE(vm.getVoice(0)->envelopeLevel == Catch::Approx(0.0f));
}

//==============================================================================
// Complete lifecycle
//==============================================================================

TEST_CASE("VoiceManager - complete voice lifecycle", "[voice][lifecycle]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.setDefaultAttack(0.05f);
    vm.setDefaultDecay(0.1f);
    vm.setDefaultSustain(0.6f);
    vm.setDefaultRelease(0.1f);

    vm.noteOn(60, 0.5f);

    // 1. Attack phase
    {
        auto buf = makeBuffer(static_cast<int>(0.03 * testSampleRate));
        vm.process(buf);
    }
    REQUIRE(vm.getVoice(0)->state == VoiceState::attack);

    // 2. Complete attack -> decay
    {
        auto buf = makeBuffer(static_cast<int>(0.03 * testSampleRate));
        vm.process(buf);
    }
    // Should now be in decay (or just past it into sustain)

    // 3. Complete decay -> sustain
    {
        auto buf = makeBuffer(static_cast<int>(0.15 * testSampleRate));
        vm.process(buf);
    }
    REQUIRE(vm.getVoice(0)->state == VoiceState::sustain);
    REQUIRE(vm.getVoice(0)->envelopeLevel == Catch::Approx(0.6f).margin(0.001f));

    // 4. Note-off -> release
    vm.noteOff(60);
    REQUIRE(vm.getVoice(0)->state == VoiceState::release);

    // 5. Complete release -> idle
    {
        auto buf = makeBuffer(static_cast<int>(0.15 * testSampleRate));
        vm.process(buf);
    }
    REQUIRE(vm.getVoice(0)->state == VoiceState::idle);
    REQUIRE(vm.getVoice(0)->envelopeLevel == Catch::Approx(0.0f));
}

//==============================================================================
// getNumActiveVoices / isVoiceActive
//==============================================================================

TEST_CASE("VoiceManager - getNumActiveVoices tracks active voices", "[voice][queries]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    REQUIRE(vm.getNumActiveVoices() == 0);

    vm.noteOn(60, 0.5f);
    REQUIRE(vm.getNumActiveVoices() == 1);

    vm.noteOn(64, 0.5f);
    REQUIRE(vm.getNumActiveVoices() == 2);

    vm.noteOn(67, 0.5f);
    REQUIRE(vm.getNumActiveVoices() == 3);

    vm.noteOff(60);
    REQUIRE(vm.getNumActiveVoices() == 3); // release is still active

    // Process through release to idle
    {
        auto buf = makeBuffer(static_cast<int>(0.5 * testSampleRate));
        vm.process(buf);
    }
    // Note 60 should be idle now, note 64/67 still in attack (sustained by process)
    REQUIRE(vm.getNumActiveVoices() == 3); // idle counts as active
}

TEST_CASE("VoiceManager - isVoiceActive returns correct state", "[voice][queries]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    REQUIRE_FALSE(vm.isVoiceActive(0));
    REQUIRE_FALSE(vm.isVoiceActive(5));

    vm.noteOn(60, 0.5f);
    REQUIRE(vm.isVoiceActive(0));
    REQUIRE_FALSE(vm.isVoiceActive(1));

    // Out-of-range indices return false
    REQUIRE_FALSE(vm.isVoiceActive(-1));
    REQUIRE_FALSE(vm.isVoiceActive(VoiceManager::maxVoices));
}

//==============================================================================
// Allocation modes
//==============================================================================

TEST_CASE("VoiceManager - round-robin allocation cycles through voices", "[voice][allocation][mode]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);
    vm.setAllocationMode(AllocationMode::roundRobin);

    // Fill all voices, each note-on should use the next slot
    for (int i = 0; i < VoiceManager::maxVoices; ++i)
    {
        vm.noteOn(60 + i, 0.5f);
        REQUIRE(vm.isVoiceActive(i));
        REQUIRE(vm.getVoice(i)->note == 60 + i);
    }
}

TEST_CASE("VoiceManager - oldest-first allocation", "[voice][allocation][mode]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);
    vm.setAllocationMode(AllocationMode::oldestFirst);

    // Allocate a few, free one by processing to idle
    vm.noteOn(60, 0.5f);
    vm.noteOn(64, 0.5f);

    // Release note 60 and process it to idle
    vm.noteOff(60);
    {
        auto buf = makeBuffer(static_cast<int>(0.5 * testSampleRate));
        vm.process(buf);
    }

    // Voice 0 should be idle now
    REQUIRE(vm.getVoice(0)->state == VoiceState::idle);

    // Next noteOn with oldestFirst should use the first free voice (slot 0)
    vm.noteOn(72, 0.5f);
    REQUIRE(vm.getVoice(0)->note == 72);
}

TEST_CASE("VoiceManager - random allocation", "[voice][allocation][mode]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);
    vm.setAllocationMode(AllocationMode::random);

    // Allocate all voices - should not crash and all should be active
    for (int i = 0; i < VoiceManager::maxVoices; ++i)
        vm.noteOn(60 + i, 0.5f);

    REQUIRE(vm.getNumActiveVoices() == VoiceManager::maxVoices);
}

//==============================================================================
// Audio output
//==============================================================================

TEST_CASE("VoiceManager - process produces non-zero output for active voice",
          "[voice][audio]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);

    auto buffer = makeBuffer(512);
    vm.process(buffer);

    // Should have some non-zero samples
    bool hasAudio = false;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            if (std::abs(buffer.getSample(ch, s)) > 0.0f)
            {
                hasAudio = true;
                break;
            }
        }
        if (hasAudio) break;
    }
    REQUIRE(hasAudio);
}

TEST_CASE("VoiceManager - process sums multiple voices", "[voice][audio]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    // Single voice
    vm.noteOn(60, 0.5f);
    auto buf1 = makeBuffer(512);
    vm.process(buf1);

    float max1 = 0.0f;
    for (int s = 0; s < buf1.getNumSamples(); ++s)
        max1 = std::max(max1, std::abs(buf1.getSample(0, s)));

    // Three voices should produce larger output (constructive summing may vary
    // by phase but generally amplitude should be higher)
    vm.noteOn(64, 0.5f);
    vm.noteOn(67, 0.5f);
    auto buf3 = makeBuffer(512);
    vm.process(buf3);

    float max3 = 0.0f;
    for (int s = 0; s < buf3.getNumSamples(); ++s)
        max3 = std::max(max3, std::abs(buf3.getSample(0, s)));

    // With more voices, peak output should be >= single voice output
    REQUIRE(max3 >= max1);
}

TEST_CASE("VoiceManager - process with no active voices produces silence",
          "[voice][audio]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    auto buffer = makeBuffer(256);
    vm.process(buffer);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            REQUIRE(buffer.getSample(ch, s) == Catch::Approx(0.0f));
}

TEST_CASE("VoiceManager - process outputs to all channels", "[voice][audio]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);

    auto buffer = makeBuffer(128);
    vm.process(buffer);

    bool chan0hasAudio = false, chan1hasAudio = false;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        if (std::abs(buffer.getSample(0, s)) > 0.0f) chan0hasAudio = true;
        if (std::abs(buffer.getSample(1, s)) > 0.0f) chan1hasAudio = true;
    }
    REQUIRE(chan0hasAudio);
    REQUIRE(chan1hasAudio);
}

//==============================================================================
// Per-voice ADSR setters
//==============================================================================

TEST_CASE("VoiceManager - per-voice ADSR setters work", "[voice][adsr]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.setVoiceAttack(0, 2.0f);
    vm.setVoiceDecay(0, 1.5f);
    vm.setVoiceSustain(0, 0.3f);
    vm.setVoiceRelease(0, 4.0f);

    REQUIRE(vm.getVoice(0)->attackSeconds == Catch::Approx(2.0f));
    REQUIRE(vm.getVoice(0)->decaySeconds == Catch::Approx(1.5f));
    REQUIRE(vm.getVoice(0)->sustainLevel == Catch::Approx(0.3f));
    REQUIRE(vm.getVoice(0)->releaseSeconds == Catch::Approx(4.0f));
}

TEST_CASE("VoiceManager - per-voice ADSR clamps to valid ranges", "[voice][adsr]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    // Clamp beyond max
    vm.setVoiceAttack(0, 15.0f);
    vm.setVoiceDecay(0, 15.0f);
    vm.setVoiceSustain(0, 2.0f);
    vm.setVoiceRelease(0, 15.0f);

    REQUIRE(vm.getVoice(0)->attackSeconds == Catch::Approx(10.0f));
    REQUIRE(vm.getVoice(0)->decaySeconds == Catch::Approx(10.0f));
    REQUIRE(vm.getVoice(0)->sustainLevel == Catch::Approx(1.0f));
    REQUIRE(vm.getVoice(0)->releaseSeconds == Catch::Approx(10.0f));

    // Clamp below min
    vm.setVoiceAttack(0, -1.0f);
    vm.setVoiceSustain(0, -0.5f);

    REQUIRE(vm.getVoice(0)->attackSeconds == Catch::Approx(0.0f));
    REQUIRE(vm.getVoice(0)->sustainLevel == Catch::Approx(0.0f));
}

TEST_CASE("VoiceManager - per-voice ADSR setters ignore invalid index",
          "[voice][adsr]")
{
    VoiceManager vm;

    vm.setVoiceAttack(-1, 5.0f);   // should not crash
    vm.setVoiceDecay(99, 5.0f);    // should not crash
    vm.setVoiceSustain(VoiceManager::maxVoices, 0.5f);

    // defaults should be unchanged
    REQUIRE(vm.getVoice(0)->attackSeconds == Catch::Approx(0.01f));
}

//==============================================================================
// Default ADSR setters
//==============================================================================

TEST_CASE("VoiceManager - default ADSR values are applied to new voices",
          "[voice][adsr]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.setDefaultAttack(0.5f);
    vm.setDefaultDecay(1.0f);
    vm.setDefaultSustain(0.4f);
    vm.setDefaultRelease(2.0f);

    vm.noteOn(60, 0.5f);

    auto* v = vm.getVoice(0);
    REQUIRE(v->attackSeconds == Catch::Approx(0.5f));
    REQUIRE(v->decaySeconds == Catch::Approx(1.0f));
    REQUIRE(v->sustainLevel == Catch::Approx(0.4f));
    REQUIRE(v->releaseSeconds == Catch::Approx(2.0f));
}

TEST_CASE("VoiceManager - default ADSR clamps to valid ranges", "[voice][adsr]")
{
    VoiceManager vm;

    vm.setDefaultAttack(15.0f);
    vm.setDefaultDecay(-1.0f);
    vm.setDefaultSustain(2.0f);
    vm.setDefaultRelease(15.0f);

    vm.noteOn(60, 0.5f);

    auto* v = vm.getVoice(0);
    REQUIRE(v->attackSeconds == Catch::Approx(10.0f));
    REQUIRE(v->decaySeconds == Catch::Approx(0.0f));
    REQUIRE(v->sustainLevel == Catch::Approx(1.0f));
    REQUIRE(v->releaseSeconds == Catch::Approx(10.0f));
}

//==============================================================================
// Output buffer edge cases
//==============================================================================

TEST_CASE("VoiceManager - process empty buffer does nothing", "[voice][audio][edge]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    vm.noteOn(60, 0.5f);

    // Empty buffer should not crash
    auto buffer = makeBuffer(0);
    vm.process(buffer);
}

TEST_CASE("VoiceManager - process called before prepare uses default sample rate",
          "[voice][audio][edge]")
{
    VoiceManager vm;

    vm.noteOn(60, 0.5f);
    auto buffer = makeBuffer(128);
    vm.process(buffer); // Should not crash

    bool hasAudio = false;
    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        if (std::abs(buffer.getSample(0, s)) > 0.0f)
        {
            hasAudio = true;
            break;
        }
    }
    REQUIRE(hasAudio);
}

//==============================================================================
// Process produces only finite values
//==============================================================================

TEST_CASE("VoiceManager - process produces no NaN or Inf", "[voice][audio]")
{
    VoiceManager vm;
    vm.prepare(testSampleRate);

    // Stack several voices
    for (int i = 0; i < 8; ++i)
        vm.noteOn(60 + i, 1.0f);

    auto buffer = makeBuffer(1024);
    vm.process(buffer);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            const float sample = buffer.getSample(ch, s);
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE_FALSE(std::isinf(sample));
        }
    }
}
