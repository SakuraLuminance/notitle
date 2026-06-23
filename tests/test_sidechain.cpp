#include <catch2/catch_all.hpp>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "dsp/effects/DynamicsModule.h"

using namespace ana;

//==============================================================================
// Sidechain test helpers
//==============================================================================

/** Fills a buffer with a constant amplitude sine tone. */
static void fillSine(juce::AudioBuffer<float>& buffer, float amplitude, double sampleRate, float freq = 440.0f)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    for (int s = 0; s < numSamples; ++s)
    {
        const float val = amplitude * std::sin(2.0 * M_PI * freq * s / sampleRate);
        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample(ch, s, val);
    }
}

/** Fills the main buffer with a quiet signal, creates a loud interleaved sidechain buffer. */
static std::vector<float> createLoudSidechain(int numSamples, int sidechainChannels, double sampleRate)
{
    std::vector<float> sc(static_cast<size_t>(numSamples) * static_cast<size_t>(sidechainChannels));
    for (int s = 0; s < numSamples; ++s)
    {
        // Loud 200 Hz sine (near 0 dBFS)
        const float val = 0.9f * std::sin(2.0 * M_PI * 200.0 * s / sampleRate);
        for (int ch = 0; ch < sidechainChannels; ++ch)
            sc[static_cast<size_t>(s) * sidechainChannels + ch] = val;
    }
    return sc;
}

//==============================================================================
// Test cases
//==============================================================================

TEST_CASE("DynamicsModule - sidechain compressor applies gain reduction from sidechain signal",
          "[effects][sidechain][compressor]")
{
    DynamicsModule dyn;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;

    dyn.prepare(spec);
    dyn.setMode(DynamicsMode::Compressor);

    // Set compressor to react to signals above -30 dBFS with 4:1 ratio
    dyn.setCompressorThreshold(-30.0f);
    dyn.setCompressorRatio(4.0f);
    dyn.setCompressorAttack(1.0f);   // fast attack
    dyn.setCompressorRelease(50.0f);

    // Enable sidechain
    dyn.setSidechainMode(SidechainMode::External);

    // Create main buffer with a quiet signal (-40 dBFS ≈ 0.01 amplitude)
    juce::AudioBuffer<float> mainBuffer(2, 512);
    mainBuffer.clear();
    // Very quiet signal that would NOT trigger compression alone
    fillSine(mainBuffer, 0.01f, spec.sampleRate, 440.0f);

    // Create loud sidechain signal (-1 dBFS ≈ 0.9 amplitude)
    auto sidechainData = createLoudSidechain(512, 1, spec.sampleRate);

    // Save a copy of the dry signal for comparison
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(mainBuffer, true);

    // Process with sidechain
    juce::MidiBuffer midi;
    dyn.process(mainBuffer, midi, sidechainData.data(), 1);

    // The loud sidechain SHOULD cause compression on the quiet main signal
    // Compute RMS of processed vs dry signal
    double dryRms = 0.0;
    double wetRms = 0.0;
    const int numSamples = mainBuffer.getNumSamples();
    const int numCh = mainBuffer.getNumChannels();

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* dry = dryBuffer.getReadPointer(ch);
        const auto* wet = mainBuffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            dryRms += static_cast<double>(dry[s]) * dry[s];
            wetRms += static_cast<double>(wet[s]) * wet[s];
        }
    }
    dryRms = std::sqrt(dryRms / (numSamples * numCh));
    wetRms = std::sqrt(wetRms / (numSamples * numCh));

    // The wet signal should be quieter than dry due to sidechain compression
    REQUIRE(wetRms < dryRms * 0.9); // at least 10% quieter
}

TEST_CASE("DynamicsModule - sidechain off preserves normal compression behavior",
          "[effects][sidechain][compressor]")
{
    DynamicsModule dyn;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;

    dyn.prepare(spec);
    dyn.setMode(DynamicsMode::Compressor);
    dyn.setCompressorThreshold(-30.0f);
    dyn.setCompressorRatio(4.0f);
    dyn.setCompressorAttack(1.0f);
    dyn.setCompressorRelease(50.0f);

    // Sidechain is Off (default)
    dyn.setSidechainMode(SidechainMode::Off);

    // Create main buffer with a moderately loud signal that would trigger compression
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    fillSine(buffer, 0.5f, spec.sampleRate, 440.0f); // -6 dBFS

    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer, true);

    // Even if we pass sidechain data, mode is Off so it should be ignored
    auto sidechainData = createLoudSidechain(512, 1, spec.sampleRate);
    juce::MidiBuffer midi;
    dyn.process(buffer, midi, sidechainData.data(), 1);

    // Verify that the main signal IS compressed (normal compressor behavior)
    double dryRms = 0.0;
    double wetRms = 0.0;
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* dry = dryBuffer.getReadPointer(ch);
        const auto* wet = buffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            dryRms += static_cast<double>(dry[s]) * dry[s];
            wetRms += static_cast<double>(wet[s]) * wet[s];
        }
    }
    dryRms = std::sqrt(dryRms / (numSamples * 2));
    wetRms = std::sqrt(wetRms / (numSamples * 2));

    // The -6 dBFS signal should be compressed even without sidechain
    REQUIRE(wetRms < dryRms);
}

TEST_CASE("DynamicsModule - sidechain gate opens/closes from sidechain signal",
          "[effects][sidechain][gate]")
{
    DynamicsModule dyn;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 256;
    spec.numChannels = 2;

    dyn.prepare(spec);
    dyn.setMode(DynamicsMode::Gate);
    dyn.setGateThreshold(-40.0f);  // threshold at -40 dBFS
    dyn.setGateHold(0.0f);         // no hold for immediate test
    dyn.setGateRelease(1.0f);      // fast release

    dyn.setSidechainMode(SidechainMode::External);

    // Main signal: very quiet (would keep gate closed on its own)
    juce::AudioBuffer<float> mainBuffer(2, 256);
    mainBuffer.clear();
    fillSine(mainBuffer, 0.001f, spec.sampleRate, 440.0f); // -60 dBFS

    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(mainBuffer, true);

    // Sidechain: first half loud, second half silent
    std::vector<float> sidechainData(256);
    for (int s = 0; s < 256; ++s)
    {
        // First 128 samples: loud signal
        // Last 128 samples: silence (gate should close)
        if (s < 128)
            sidechainData[static_cast<size_t>(s)] = 0.5f; // -6 dBFS
        else
            sidechainData[static_cast<size_t>(s)] = 0.0f;
    }

    juce::MidiBuffer midi;
    dyn.process(mainBuffer, midi, sidechainData.data(), 1);

    // First half of output should be gated OPEN (signal passes)
    // Last half should be gated CLOSED (signal attenuated)
    float firstHalfRms = 0.0f;
    float secondHalfRms = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* data = mainBuffer.getReadPointer(ch);
        for (int s = 0; s < 128; ++s)
            firstHalfRms += data[s] * data[s];
        for (int s = 128; s < 256; ++s)
            secondHalfRms += data[s] * data[s];
    }
    firstHalfRms = std::sqrt(firstHalfRms / (128.0f * 2));
    secondHalfRms = std::sqrt(secondHalfRms / (128.0f * 2));

    // First half should be significantly louder than second half
    REQUIRE(firstHalfRms > secondHalfRms * 5.0f);
}

TEST_CASE("DynamicsModule - backward compatibility: original process() still works",
          "[effects][sidechain][compat]")
{
    DynamicsModule dyn;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 128;
    spec.numChannels = 1;

    dyn.prepare(spec);
    dyn.setMode(DynamicsMode::Compressor);
    dyn.setCompressorThreshold(-24.0f);
    dyn.setCompressorRatio(4.0f);
    dyn.setCompressorAttack(10.0f);
    dyn.setCompressorRelease(100.0f);

    // Create a signal that needs compression
    juce::AudioBuffer<float> buffer(1, 128);
    buffer.clear();
    fillSine(buffer, 0.5f, spec.sampleRate, 440.0f);

    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer, true);

    // Call the original process() — no sidechain arguments
    dyn.process(buffer);

    // Signal should be compressed (reduced amplitude)
    float dryLevel = 0.0f;
    float wetLevel = 0.0f;
    for (int s = 0; s < 128; ++s)
    {
        dryLevel += std::abs(dryBuffer.getSample(0, s));
        wetLevel += std::abs(buffer.getSample(0, s));
    }

    // The 6 dBFS sine should trigger reduction
    REQUIRE(wetLevel < dryLevel * 0.95f);
}

TEST_CASE("DynamicsModule - bypass with sidechain still bypasses",
          "[effects][sidechain][bypass]")
{
    DynamicsModule dyn;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 128;
    spec.numChannels = 2;

    dyn.prepare(spec);
    dyn.setBypass(true);
    dyn.setSidechainMode(SidechainMode::External);

    juce::AudioBuffer<float> buffer(2, 128);
    buffer.clear();
    fillSine(buffer, 0.5f, spec.sampleRate, 440.0f);

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf(buffer, true);

    auto sidechainData = createLoudSidechain(128, 1, spec.sampleRate);
    juce::MidiBuffer midi;
    dyn.process(buffer, midi, sidechainData.data(), 1);

    // When bypassed, output should equal input regardless of sidechain
    for (int s = 0; s < 128; ++s)
    {
        REQUIRE(buffer.getSample(0, s) == reference.getSample(0, s));
        REQUIRE(buffer.getSample(1, s) == reference.getSample(1, s));
    }
}
