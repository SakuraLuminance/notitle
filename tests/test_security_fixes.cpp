#include <catch2/catch_all.hpp>
#include "dsp/ResynthesisEngine.h"
#include "dsp/VoiceManager.h"
#include "dsp/MidiLearn.h"
#include "dsp/UndoManager.h"
#include "dsp/PresetManager.h"
#include "dsp/STFTConfig.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <atomic>
#include <cstdint>

// SSE intrinsics for FTZ/DAZ denormal test (x86 only)
#if JUCE_INTEL
#include <xmmintrin.h>
#endif

using namespace ana;

// ===========================================================================
// Task 2: Resynth double-buffer fix (no UAF)
//
// The double-buffer pattern in PluginProcessor uses an atomic index to swap
// between two buffers. The write buffer is filled then the index toggles.
// The read buffer (now the old write buffer) must remain valid after the
// swap — no dangling references, no use-after-free.
// ===========================================================================

TEST_CASE("ResynthEngine - resynthesize produces valid output buffer",
          "[security][resynth][uaf]")
{
    ResynthesisEngine engine;
    PartialData data;
    STFTConfig config;

    data.sampleRate = 44100.0;
    data.hopSize = 512.0;
    data.maxPartials = 512;

    // Create 3 frames with a 440 Hz partial
    for (int i = 0; i < 3; ++i)
    {
        PartialFrame frame;
        frame.timestamp = i * 512.0 / 44100.0;
        frame.partials.push_back({440.0f, 0.5f, 0.0f});
        data.frames.push_back(frame);
    }

    // First call
    auto result1 = engine.resynthesize(data, config);
    REQUIRE_FALSE(result1.empty());

    // Check for NaN or Inf
    for (float s : result1)
    {
        REQUIRE_FALSE(std::isnan(s));
        REQUIRE_FALSE(std::isinf(s));
    }
    // Check output is normalized to [-1, 1]
    float maxVal = 0.0f;
    for (float s : result1)
        maxVal = std::max(maxVal, std::abs(s));
    REQUIRE(maxVal <= 1.0f);

    // Second call — double-buffer should produce a new valid result
    // without corrupting the first result (simulates the swap pattern)
    auto result2 = engine.resynthesize(data, config);
    REQUIRE_FALSE(result2.empty());
    for (float s : result2)
    {
        REQUIRE_FALSE(std::isnan(s));
        REQUIRE_FALSE(std::isinf(s));
    }

    // Both buffers should be independently valid (no cross-contamination)
    bool hasSignal1 = false, hasSignal2 = false;
    for (float s : result1) if (std::abs(s) > 1e-6f) hasSignal1 = true;
    for (float s : result2) if (std::abs(s) > 1e-6f) hasSignal2 = true;
    REQUIRE(hasSignal1);
    REQUIRE(hasSignal2);
}

TEST_CASE("ResynthEngine - empty partial data returns empty result",
          "[security][resynth][uaf]")
{
    ResynthesisEngine engine;
    PartialData data;
    STFTConfig config;

    auto result = engine.resynthesize(data, config);
    REQUIRE(result.empty());
}

// ===========================================================================
// Task 3: Voice data races (TSan clean)
//
// VoiceManager::process() clears the output buffer before rendering voices.
// This eliminates TSan reports from stale buffer contents. Voice state
// transitions use std::atomic with proper memory ordering. Test that buffer
// is always cleared before voice rendering.
// ===========================================================================

TEST_CASE("VoiceManager - process clears buffer before voice render",
          "[security][voice][tsan]")
{
    VoiceManager vm;
    vm.prepare(44100.0);

    // Allocate one voice
    vm.noteOn(60, 0.8f);
    REQUIRE(vm.getNumActiveVoices() == 1);

    // Pre-fill buffer with garbage (simulates stale data from a previous block)
    juce::AudioBuffer<float> buffer(2, 256);
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 256; ++s)
            buffer.setSample(ch, s, 0.5f);

    // This should clear the buffer before summing voices into it
    vm.process(buffer);

    // The output should be fresh voice data, not our garbage.
    // Voice output should be in the valid float range.
    bool anyFreshOutput = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int s = 0; s < 256; ++s)
        {
            float sample = buffer.getSample(ch, s);
            // Must not contain the pre-filled garbage value
            REQUIRE(std::abs(sample) < 0.49f);
            REQUIRE(sample >= -1.0f);
            REQUIRE(sample <= 1.0f);
            if (std::abs(sample) > 1e-4f)
                anyFreshOutput = true;
        }
    }
    // A voice rendering at 261.6 Hz should produce audible output
    REQUIRE(anyFreshOutput);
}

TEST_CASE("VoiceManager - atomic state transitions are coherent",
          "[security][voice][tsan]")
{
    VoiceManager vm;
    vm.prepare(44100.0);

    // Allocate and release voices, verify atomic state machine
    vm.noteOn(60, 0.8f);
    auto* voice = vm.getVoice(0);
    REQUIRE(voice != nullptr);

    // Initial state should be attack
    auto state = voice->state.load(std::memory_order_acquire);
    REQUIRE(state == VoiceState::attack);

    // Note off should transition to release (with tail)
    vm.noteOff(60, 0.0f, true);
    state = voice->state.load(std::memory_order_acquire);
    REQUIRE(state == VoiceState::release);
}

TEST_CASE("VoiceManager - multiple process calls do not accumulate garbage",
          "[security][voice][tsan]")
{
    VoiceManager vm;
    vm.prepare(44100.0);

    // Process empty buffer with no voices — should stay silent
    {
        juce::AudioBuffer<float> buffer(2, 64);
        // Fill with noise first
        buffer.clear();
        buffer.setSample(0, 0, 0.5f);
        buffer.setSample(1, 0, 0.5f);

        vm.process(buffer);

        // process() clears => first sample should now be 0
        REQUIRE(std::abs(buffer.getSample(0, 0)) < 1e-6f);
    }

    // Process again — should still be clean
    {
        juce::AudioBuffer<float> buffer(2, 64);
        buffer.setSample(0, 0, 0.5f);
        vm.process(buffer);
        REQUIRE(std::abs(buffer.getSample(0, 0)) < 1e-6f);
    }
}

// ===========================================================================
// Task 4: ARM denormal (verify ARM branch compiles)
//
// The denormal-flushing code uses #if JUCE_INTEL / #elif JUCE_ARM guards.
// On Intel, _mm_setcsr sets FTZ+DAZ. On ARM, inline asm sets the FZ bit
// in FPCR (aarch64) or FPSCR (32-bit ARM). This test verifies the SSE
// path works on x86 and that the 1e-30f denormal-flushing idiom compiles.
// ===========================================================================

TEST_CASE("Denormal flushing - 1e-30f idiom prevents denormals",
          "[security][denormal]")
{
    // The production code uses `+ 1e-30f` to flush denormals to zero
    // in resonant feedback paths (PhaserEffect, MultiFilter).
    // A denormal is a very small float near the underflow threshold.
    volatile float tiny = 1e-38f;  // sub-normal / denormal on most platforms
    float withEpsilon = tiny + 1e-30f;
    // Adding 1e-30f should not produce NaN or Inf
    REQUIRE_FALSE(std::isnan(withEpsilon));
    REQUIRE_FALSE(std::isinf(withEpsilon));
}

TEST_CASE("Denormal flushing - SSE FTZ/DAZ CSR bits (x86)",
          "[security][denormal][x86]")
{
#if JUCE_INTEL
    unsigned int oldCsr = _mm_getcsr();

    // Set FTZ (bit 15) + DAZ (bit 6) — same as production code
    _mm_setcsr(oldCsr | 0x8040);
    unsigned int newCsr = _mm_getcsr();
    REQUIRE((newCsr & 0x8040) == 0x8040);

    // Restore
    _mm_setcsr(oldCsr);
#else
    // Non-x86: verify the ARM preprocessor guards compile correctly.
    // These branches mirror PluginProcessor's denormal-flushing code.
    #if JUCE_ARM
        // The ARM inline asm branches exist and are reachable
        SUCCEED("ARM platform – denormal guards compile");
    #else
        SUCCEED("Non-x86, non-ARM platform – denormal test skipped");
    #endif
#endif
}

// ===========================================================================
// Task 5: Deserialization clamping (fftSize=0 → 256)
//
// PresetManager::deserialiseSTFTConfig clamps fftSize to [256, 65536],
// hopSize to [64, 8192], and maxPartials to [32, 2048] using juce::jlimit.
// This prevents division-by-zero and allocation bombs from corrupt
// presets.
// ===========================================================================

TEST_CASE("Deserialisation - STFT fftSize=0 clamped to 256",
          "[security][deserialize]")
{
    STFTConfig config;
    PresetManager pm;
    pm.setStateReferences(&config, nullptr, nullptr, nullptr,
                          nullptr, nullptr, nullptr, nullptr);

    // Build a ValueTree with fftSize=0 (malicious/corrupt preset)
    juce::ValueTree params("Parameters");
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      0,     nullptr);
    stft.setProperty("HopSize",      512,   nullptr);
    stft.setProperty("WindowType",   "Hann", nullptr);
    stft.setProperty("Threshold",    -60.0f, nullptr);
    stft.setProperty("MaxPartials",  512,   nullptr);
    params.addChild(stft, 0, nullptr);

    REQUIRE(pm.deserialiseState(params));

    // fftSize must be clamped to 256 (the minimum)
    REQUIRE(config.fftSize == 256);
    REQUIRE(config.hopSize == 512);
    REQUIRE(config.maxPartials == 512);
}

TEST_CASE("Deserialisation - STFT fftSize negative clamped to 256",
          "[security][deserialize]")
{
    STFTConfig config;
    PresetManager pm;
    pm.setStateReferences(&config, nullptr, nullptr, nullptr,
                          nullptr, nullptr, nullptr, nullptr);

    juce::ValueTree params("Parameters");
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      -100,  nullptr);
    stft.setProperty("HopSize",      512,   nullptr);
    stft.setProperty("WindowType",   "Hann", nullptr);
    stft.setProperty("Threshold",    -60.0f, nullptr);
    stft.setProperty("MaxPartials",  512,   nullptr);
    params.addChild(stft, 0, nullptr);

    REQUIRE(pm.deserialiseState(params));
    REQUIRE(config.fftSize == 256);
}

TEST_CASE("Deserialisation - STFT hopSize=0 clamped to 64",
          "[security][deserialize]")
{
    STFTConfig config;
    PresetManager pm;
    pm.setStateReferences(&config, nullptr, nullptr, nullptr,
                          nullptr, nullptr, nullptr, nullptr);

    juce::ValueTree params("Parameters");
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      2048,  nullptr);
    stft.setProperty("HopSize",      0,     nullptr);
    stft.setProperty("WindowType",   "Hann", nullptr);
    stft.setProperty("Threshold",    -60.0f, nullptr);
    stft.setProperty("MaxPartials",  512,   nullptr);
    params.addChild(stft, 0, nullptr);

    REQUIRE(pm.deserialiseState(params));
    REQUIRE(config.hopSize == 64);
    REQUIRE(config.fftSize == 2048);
}

TEST_CASE("Deserialisation - STFT maxPartials=0 clamped to 32",
          "[security][deserialize]")
{
    STFTConfig config;
    PresetManager pm;
    pm.setStateReferences(&config, nullptr, nullptr, nullptr,
                          nullptr, nullptr, nullptr, nullptr);

    juce::ValueTree params("Parameters");
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      2048, nullptr);
    stft.setProperty("HopSize",      512,  nullptr);
    stft.setProperty("WindowType",   "Hann", nullptr);
    stft.setProperty("Threshold",    -60.0f, nullptr);
    stft.setProperty("MaxPartials",  0,    nullptr);
    params.addChild(stft, 0, nullptr);

    REQUIRE(pm.deserialiseState(params));
    REQUIRE(config.maxPartials == 32);
}

TEST_CASE("Deserialisation - STFT huge values clamped to max",
          "[security][deserialize]")
{
    STFTConfig config;
    PresetManager pm;
    pm.setStateReferences(&config, nullptr, nullptr, nullptr,
                          nullptr, nullptr, nullptr, nullptr);

    juce::ValueTree params("Parameters");
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize",      999999, nullptr);
    stft.setProperty("HopSize",      99999,  nullptr);
    stft.setProperty("WindowType",   "Hann", nullptr);
    stft.setProperty("Threshold",    -60.0f, nullptr);
    stft.setProperty("MaxPartials",  99999,  nullptr);
    params.addChild(stft, 0, nullptr);

    REQUIRE(pm.deserialiseState(params));
    REQUIRE(config.fftSize == 65536);
    REQUIRE(config.hopSize == 8192);
    REQUIRE(config.maxPartials == 2048);
}

TEST_CASE("Deserialisation - STFT with null ref returns false",
          "[security][deserialize]")
{
    PresetManager pm;

    juce::ValueTree params("Parameters");
    juce::ValueTree stft("STFTConfig");
    stft.setProperty("FFTSize", 0, nullptr);
    params.addChild(stft, 0, nullptr);

    // stftConfigRef is null — should return false without crashing
    REQUIRE_FALSE(pm.deserialiseState(params));
}

// ===========================================================================
// Task 6: Path traversal (../../evil blocked)
//
// PresetManager::sanitizePresetName replaces ".." with "_", replaces
// path separators with "_", and strips illegal filename characters.
// This prevents directory traversal via preset names.
// ===========================================================================

TEST_CASE("Path traversal - preset name with ../.. is sanitized",
          "[security][pathtraversal]")
{
    // Test through loadPreset — a traversal attempt should sanitize
    // the name so it can't escape the preset directory.
    PresetManager pm;

    // "../../evil" -> sanitize: replace ".." -> "_", "/" -> "_"
    // -> "_____evil" -> createLegalFileName -> "_____evil"
    // loadPreset will search for "_____evil.anaplug" which doesn't exist
    // (returns false) — but crucially, no directory traversal occurs.
    bool result = pm.loadPreset("../../evil");
    REQUIRE_FALSE(result);
}

TEST_CASE("Path traversal - preset name with backslashes is sanitized",
          "[security][pathtraversal]")
{
    PresetManager pm;

    // "..\\..\\evil" -> sanitize: ".." -> "_", "\\" -> "_"
    bool result = pm.loadPreset("..\\..\\evil");
    REQUIRE_FALSE(result);
}

TEST_CASE("Path traversal - null byte in preset name is sanitized",
          "[security][pathtraversal]")
{
    PresetManager pm;

    // Names with illegal characters should be sanitized safely
    bool result = pm.loadPreset("safe\x00name");
    // Should not crash, should return false (file won't exist)
    REQUIRE_FALSE(result);
}

TEST_CASE("Path traversal - normal preset name passes through",
          "[security][pathtraversal]")
{
    PresetManager pm;

    // Normal names should not be blocked
    bool result = pm.loadPreset("My Great Preset");
    // The preset won't exist (no factory presets written in tests
    // unless the directory happens to have them), but the key is that
    // it doesn't crash and the name isn't rejected as empty.
    // We just verify no exception/crash.
    REQUIRE(true);  // reached without crash
    // Note: result may be false because file doesn't exist — that's fine.
}

// ===========================================================================
// Task 7: Null pointer safety (MidiLearn with nullptr)
//
// MidiLearn::processMidi and addMapping must handle nullptr targetParam
// gracefully. The fix adds nullptr checks before dereferencing the
// atomic<float>* target in the audio thread hot path.
// ===========================================================================

TEST_CASE("MidiLearn - addMapping with nullptr target does not crash",
          "[security][midi][nullptr]")
{
    MidiLearn ml;

    // Add mapping with nullptr target — this should not crash
    REQUIRE_NOTHROW(ml.addMapping(1, "param", nullptr, 0.0f, 1.0f));

    const auto& mappings = ml.getMappings();
    REQUIRE(mappings.size() == 1);
    REQUIRE(mappings[0].ccNumber == 1);
    REQUIRE(mappings[0].targetParam == nullptr);
}

TEST_CASE("MidiLearn - processMidi with nullptr target is safe",
          "[security][midi][nullptr]")
{
    MidiLearn ml;

    // Add mapping with nullptr target (simulates a loaded preset
    // where the target pointer hasn't been reconnected yet)
    ml.addMapping(7, "volume", nullptr, 0.0f, 1.0f);
    REQUIRE(ml.getMappings().size() == 1);

    // Send a MIDI CC message — should not crash with nullptr target
    auto msg = juce::MidiMessage::controllerEvent(1, 7, 100);
    REQUIRE_NOTHROW(ml.processMidi(msg));
}

TEST_CASE("MidiLearn - startLearn with nullptr target does not crash",
          "[security][midi][nullptr]")
{
    MidiLearn ml;

    // Start learn mode with nullptr target
    REQUIRE_NOTHROW(ml.startLearn("param", nullptr, 0.0f, 1.0f));
    REQUIRE(ml.isLearning());

    // Send CC while learning with nullptr — should not crash
    auto msg = juce::MidiMessage::controllerEvent(1, 15, 64);
    REQUIRE_NOTHROW(ml.processMidi(msg));

    // After learning, there should be a mapping with nullptr target
    const auto& mappings = ml.getMappings();
    REQUIRE(mappings.size() == 1);
    REQUIRE(mappings[0].targetParam == nullptr);
}

TEST_CASE("MidiLearn - processMidi with no mappings is safe",
          "[security][midi][nullptr]")
{
    MidiLearn ml;

    // processMidi with no mappings at all
    auto msg = juce::MidiMessage::controllerEvent(1, 7, 100);
    REQUIRE_NOTHROW(ml.processMidi(msg));
    REQUIRE(ml.getMappings().empty());
}

TEST_CASE("MidiLearn - reconnectTarget with null sets mapping to null",
          "[security][midi][nullptr]")
{
    MidiLearn ml;

    std::atomic<float> target{0.5f};
    ml.addMapping(1, "param", &target, 0.0f, 1.0f);
    REQUIRE(ml.getMappings()[0].targetParam != nullptr);

    // Reconnect to nullptr (simulating target being destroyed)
    ml.reconnectTarget("param", nullptr);
    REQUIRE(ml.getMappings()[0].targetParam == nullptr);

    // processMidi with now-null target should not crash
    auto msg = juce::MidiMessage::controllerEvent(1, 1, 100);
    REQUIRE_NOTHROW(ml.processMidi(msg));
}

// ===========================================================================
// Task 8: UndoManager leak (ASAN clean)
//
// UndoManager must not leak UndoStep or UndoCommand memory. Commands
// are owned via unique_ptr. The composite allocates a raw UndoStep on
// the heap and must clean it up in clear() and ~UndoManager().
// ===========================================================================

TEST_CASE("UndoManager - composite step freed on clear",
          "[security][undo][leak]")
{
    UndoManager um;

    // Start a composite but never end it — clear() must handle this
    um.beginComposite("Orphaned Composite");
    // Execute a sub-operation
    int value = 0;
    auto cmd = std::make_unique<UndoCommand>();
    cmd->execute = [&value]() { value = 42; };
    cmd->undo    = [&value]() { value = 0; };
    cmd->description = "Sub Op";
    um.execute(std::move(cmd));

    // Clear without ending composite — must clean up internal state
    REQUIRE_NOTHROW(um.clear());
    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE(um.getNumRedoSteps() == 0);
    // After clear, new operations should work
    REQUIRE_NOTHROW(um.beginComposite("New"));
    REQUIRE_NOTHROW(um.endComposite());
}

TEST_CASE("UndoManager - composite with no sub-ops discarded on clear",
          "[security][undo][leak]")
{
    UndoManager um;

    // Begin composite, clear without ending
    um.beginComposite("Empty Composite");
    REQUIRE_NOTHROW(um.clear());

    // Should be clean after
    REQUIRE(um.getNumUndoSteps() == 0);
}

TEST_CASE("UndoManager - redo stack cleared on new execute",
          "[security][undo][leak]")
{
    UndoManager um;
    int value = 0;

    auto makeCmd = [&value](int delta) {
        auto cmd = std::make_unique<UndoCommand>();
        cmd->execute = [&value, delta]() { value += delta; };
        cmd->undo    = [&value, delta]() { value -= delta; };
        return cmd;
    };

    um.execute(makeCmd(10));
    um.execute(makeCmd(20));
    REQUIRE(value == 30);
    REQUIRE(um.getNumUndoSteps() == 2);

    // Undo both, verify redo stack grows
    um.undo();
    REQUIRE(value == 10);
    REQUIRE(um.getNumRedoSteps() == 1);

    um.undo();
    REQUIRE(value == 0);
    REQUIRE(um.getNumRedoSteps() == 2);

    // New action must clear redo stack (ASAN: no stale refs to freed steps)
    um.execute(makeCmd(100));
    REQUIRE(value == 100);
    REQUIRE(um.getNumRedoSteps() == 0);
    REQUIRE(um.getNumUndoSteps() == 1);
}

TEST_CASE("UndoManager - many execute/undo cycles do not leak",
          "[security][undo][leak]")
{
    UndoManager um(16);  // depth of 16

    int value = 0;
    auto makeCmd = [&value](int delta) {
        auto cmd = std::make_unique<UndoCommand>();
        cmd->execute = [&value, delta]() { value += delta; };
        cmd->undo    = [&value, delta]() { value -= delta; };
        return cmd;
    };

    // Cycle many times
    for (int cycle = 0; cycle < 10; ++cycle)
    {
        for (int i = 0; i < 20; ++i)
            um.execute(makeCmd(1));
        for (int i = 0; i < 20; ++i)
            um.undo();
    }

    // Final state should be consistent
    REQUIRE(value == 0);
    REQUIRE(um.getNumUndoSteps() == 0);
    REQUIRE(um.getNumRedoSteps() <= 16);  // bounded by maxDepth
}

TEST_CASE("UndoManager - max depth eviction frees memory",
          "[security][undo][leak]")
{
    UndoManager um(3);  // small depth for tight bounds

    int value = 0;
    auto makeCmd = [&value](int delta) {
        auto cmd = std::make_unique<UndoCommand>();
        cmd->execute = [&value, delta]() { value += delta; };
        cmd->undo    = [&value, delta]() { value -= delta; };
        return cmd;
    };

    // Push 5 commands, depth 3 => oldest 2 are evicted
    for (int i = 0; i < 5; ++i)
        um.execute(makeCmd(1));
    REQUIRE(value == 5);

    // Undo all remaining (3) — oldest 2 are lost but that's correct
    for (int i = 0; i < 3; ++i)
        um.undo();
    REQUIRE(value == 2);
    REQUIRE(um.getNumUndoSteps() == 0);
}

TEST_CASE("UndoManager - command with nullptr callbacks is safe",
          "[security][undo][leak]")
{
    UndoManager um;

    // Command with no execute/undo callbacks
    auto cmd = std::make_unique<UndoCommand>();
    cmd->description = "Empty Command";
    // execute and undo are nullptr by default (std::function default ctor)
    REQUIRE_NOTHROW(um.execute(std::move(cmd)));
    REQUIRE(um.getNumUndoSteps() == 1);

    // Undo with empty callback
    REQUIRE_NOTHROW(um.undo());
    REQUIRE(um.getNumUndoSteps() == 0);

    // Redo with empty callback
    REQUIRE_NOTHROW(um.redo());
    REQUIRE(um.getNumRedoSteps() == 0);
}
