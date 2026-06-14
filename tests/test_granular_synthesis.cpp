#include <catch2/catch_all.hpp>
#include "../src/dsp/GranularSynthesizer.h"
#include <cmath>

//==============================================================================
// Helper: accumulate absolute energy in a buffer region
static float bufferEnergy(const juce::AudioBuffer<float>& buf, int channel = 0)
{
    float energy = 0.0f;
    const float* data = buf.getReadPointer(channel);
    for (int i = 0; i < buf.getNumSamples(); ++i)
        energy += std::abs(data[i]);
    return energy;
}

//==============================================================================
// Helper: estimate dominant frequency via zero-crossing rate
static double estimateFrequency(const float* data, int numSamples, double sampleRate)
{
    if (numSamples < 10)
        return 0.0;

    // Find the central region with maximum energy to avoid window edges
    int start = 0;
    float maxEnergy = 0.0f;
    const int windowSize = std::min(numSamples / 2, 1024);

    for (int offset = 0; offset <= numSamples - windowSize; offset += windowSize / 4)
    {
        float energy = 0.0f;
        for (int i = 0; i < windowSize; ++i)
            energy += std::abs(data[offset + i]);

        if (energy > maxEnergy)
        {
            maxEnergy = energy;
            start = offset;
        }
    }

    // Count zero-crossings in the highest-energy window
    int crossings = 0;
    for (int i = start + 1; i < start + windowSize; ++i)
    {
        if (data[i - 1] <= 0.0f && data[i] > 0.0f)
            ++crossings;
    }

    if (crossings == 0)
        return 0.0;

    return static_cast<double>(crossings) * sampleRate / static_cast<double>(windowSize);
}

//==============================================================================
TEST_CASE("GranularSynthesizer - grain generation density", "[granular]")
{
    ana::GranularSynthesizer synth;

    // 1 second of DC at 48 kHz
    std::vector<float> buffer(48000, 1.0f);
    synth.setSourceBuffer(buffer, 48000.0);

    synth.setGrainSize(10.0f);          // 10 ms grains
    synth.setDensity(100.0f);           // 100 grains/sec
    synth.setPosition(0.5f);
    synth.setPitch(0.0f);
    synth.setAmplitude(0.5f);

    // Process exactly 1 second of audio
    // At density=100 and 48 kHz: grainsPerSample = 100/48000 = 0.0020833
    // Over 48000 samples: 48000 * 0.0020833 = 100 grains expected
    juce::AudioBuffer<float> output(2, 48000);
    synth.process(output);

    // The total grains spawned should be close to 100
    int spawned = synth.getTotalGrainsSpawned();
    REQUIRE(spawned >= 80);
    REQUIRE(spawned <= 120);
}

TEST_CASE("GranularSynthesizer - grain generation at low density", "[granular]")
{
    ana::GranularSynthesizer synth;

    std::vector<float> buffer(48000, 1.0f);
    synth.setSourceBuffer(buffer, 48000.0);

    synth.setGrainSize(5.0f);
    synth.setDensity(10.0f);            // 10 grains/sec
    synth.setPosition(0.5f);
    synth.setPitch(0.0f);
    synth.setAmplitude(0.5f);

    // Process 2 seconds (96000 samples)
    juce::AudioBuffer<float> output(2, 96000);
    synth.process(output);

    // With density=10 at 48 kHz over 96000 samples:
    // grainsPerSample = 10/48000 = 2.083e-4
    // expected: 96000 * 2.083e-4 = 20 grains
    int spawned = synth.getTotalGrainsSpawned();
    REQUIRE(spawned >= 15);
    REQUIRE(spawned <= 25);
}

//==============================================================================
TEST_CASE("GranularSynthesizer - grain duration", "[granular]")
{
    ana::GranularSynthesizer synth;

    // DC source so the grain envelope is directly visible in the output
    std::vector<float> buffer(48000, 1.0f);
    synth.setSourceBuffer(buffer, 48000.0);

    const float expectedMs = 20.0f;
    const int expectedSamples = static_cast<int>(expectedMs * 48.0f); // 960 @ 48 kHz

    synth.setGrainSize(expectedMs);
    synth.setDensity(1000.0f);          // high density ensures grains are spawned
    synth.setPosition(0.5f);
    synth.setPitch(0.0f);
    synth.setAmplitude(0.5f);
    synth.setWindowType(ana::GrainWindowType::Triangle);

    // Process enough samples to capture the grain envelope
    juce::AudioBuffer<float> output(1, expectedSamples + 128);
    synth.process(output);

    const float* data = output.getReadPointer(0);

    // Triangle window: first sample = 0, rises to max at middle, falls to 0 at end.
    // Find the non-zero span using a small threshold.
    int firstNZ = -1;
    int lastNZ  = -1;

    for (int i = 0; i < output.getNumSamples(); ++i)
    {
        if (std::abs(data[i]) > 1.0e-6f)
        {
            if (firstNZ < 0) firstNZ = i;
            lastNZ = i;
        }
    }

    REQUIRE(firstNZ >= 0);   // must have output
    REQUIRE(lastNZ  >= 0);

    const int actualSamples = lastNZ - firstNZ + 1;

    // Allow small tolerance for window edge rounding
    REQUIRE(std::abs(actualSamples - expectedSamples) <= 5);

    // Verify Triangle shape: max at centre, rising first half
    const int mid = (firstNZ + lastNZ) / 2;
    const int qtr = (firstNZ + mid) / 2;

    REQUIRE(data[mid] > data[qtr]);       // rising to centre
    REQUIRE(data[qtr] > data[firstNZ]);   // first sample near zero
}

//==============================================================================
TEST_CASE("GranularSynthesizer - position control", "[granular]")
{
    ana::GranularSynthesizer synth;

    // Create a buffer with energy only in the first quarter
    std::vector<float> buffer(48000, 0.0f);
    for (int i = 0; i < 12000; ++i)
        buffer[i] = std::sin(2.0 * M_PI * 440.0 * i / 48000.0);
    // Positions 0.25 .. 1.0 are silent

    synth.setSourceBuffer(buffer, 48000.0);
    synth.setGrainSize(30.0f);
    synth.setDensity(200.0f);
    synth.setPitch(0.0f);
    synth.setAmplitude(1.0f);
    synth.setWindowType(ana::GrainWindowType::Hann);

    //--------------------------------------------------------------------------
    // Position 0.125 -> grains land in the energy region
    //--------------------------------------------------------------------------
    synth.setPosition(0.125f);
    synth.reset();

    juce::AudioBuffer<float> outputNear(1, 24000); // 0.5 sec
    synth.process(outputNear);
    float energyNear = bufferEnergy(outputNear);

    REQUIRE(energyNear > 100.0f); // significant output

    //--------------------------------------------------------------------------
    // Position 0.75 -> grains land in the silent region
    //--------------------------------------------------------------------------
    synth.setPosition(0.75f);
    synth.reset();

    juce::AudioBuffer<float> outputFar(1, 24000);
    synth.process(outputFar);
    float energyFar = bufferEnergy(outputFar);

    // Should be orders of magnitude quieter (only window tails from edge-reads)
    REQUIRE(energyFar < energyNear * 0.01f);
}

//==============================================================================
TEST_CASE("GranularSynthesizer - pitch shifting", "[granular]")
{
    ana::GranularSynthesizer synth;

    // 1 second of 440 Hz sine at 48 kHz
    const double sr = 48000.0;
    std::vector<float> buffer(static_cast<size_t>(sr), 0.0f);
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[i] = std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / sr);

    synth.setSourceBuffer(buffer, sr);
    synth.setGrainSize(80.0f);       // long enough to capture multiple cycles
    synth.setDensity(300.0f);
    synth.setPosition(0.5f);
    synth.setAmplitude(1.0f);
    synth.setWindowType(ana::GrainWindowType::Hann);

    const int testLen = static_cast<int>(sr * 0.5); // 0.5 sec

    //--------------------------------------------------------------------------
    // Unshifted (0 semitones) -> ~440 Hz
    //--------------------------------------------------------------------------
    synth.setPitch(0.0f);
    synth.reset();

    juce::AudioBuffer<float> outOrig(1, testLen);
    synth.process(outOrig);
    double f0 = estimateFrequency(outOrig.getReadPointer(0), testLen, sr);
    REQUIRE(f0 >= 300.0);
    REQUIRE(f0 <= 550.0);

    //--------------------------------------------------------------------------
    // +12 semitones (octave up) -> ~880 Hz
    //--------------------------------------------------------------------------
    synth.setPitch(12.0f);
    synth.reset();

    juce::AudioBuffer<float> outUp(1, testLen);
    synth.process(outUp);
    double fUp = estimateFrequency(outUp.getReadPointer(0), testLen, sr);
    REQUIRE(fUp >= 700.0);
    REQUIRE(fUp <= 1050.0);

    // Verify the ratio is approximately 2:1
    REQUIRE(std::abs(fUp / f0 - 2.0) < 0.5);

    //--------------------------------------------------------------------------
    // -12 semitones (octave down) -> ~220 Hz
    //--------------------------------------------------------------------------
    synth.setPitch(-12.0f);
    synth.reset();

    juce::AudioBuffer<float> outDown(1, testLen);
    synth.process(outDown);
    double fDown = estimateFrequency(outDown.getReadPointer(0), testLen, sr);
    REQUIRE(fDown >= 150.0);
    REQUIRE(fDown <= 300.0);

    // Verify ratio of original to shifted-down is approximately 2:1
    REQUIRE(std::abs(f0 / fDown - 2.0) < 0.6);
}

//==============================================================================
TEST_CASE("GranularSynthesizer - maximum grains cap", "[granular]")
{
    ana::GranularSynthesizer synth;

    std::vector<float> buffer(48000, 1.0f);
    synth.setSourceBuffer(buffer, 48000.0);

    // Maximum density and long grain size to saturate the pool
    synth.setGrainSize(100.0f);         // 100 ms grains
    synth.setDensity(1000.0f);          // 1000 grains/sec
    synth.setPosition(0.5f);
    synth.setPitch(0.0f);
    synth.setAmplitude(1.0f);

    // Process a 1-second block
    juce::AudioBuffer<float> output(2, 48000);
    synth.process(output);

    // Active grains should not exceed 256
    int active = synth.getActiveGrainCount();
    REQUIRE(active <= 256);

    // Total spawned should be high (density is maxed)
    int total = synth.getTotalGrainsSpawned();
    REQUIRE(total > 500);
}

TEST_CASE("GranularSynthesizer - stereo pan", "[granular]")
{
    ana::GranularSynthesizer synth;

    std::vector<float> buffer(48000, 1.0f); // DC
    synth.setSourceBuffer(buffer, 48000.0);

    synth.setGrainSize(50.0f);
    synth.setDensity(50.0f);
    synth.setPosition(0.5f);
    synth.setPitch(0.0f);
    synth.setAmplitude(1.0f);

    //--------------------------------------------------------------------------
    // Hard-left pan
    //--------------------------------------------------------------------------
    synth.setPan(-1.0f);
    synth.reset();

    juce::AudioBuffer<float> outLeft(2, 24000);
    synth.process(outLeft);
    float leftEnergyL = bufferEnergy(outLeft, 0);
    float leftEnergyR = bufferEnergy(outLeft, 1);

    REQUIRE(leftEnergyL > leftEnergyR * 10.0f); // left significantly louder

    //--------------------------------------------------------------------------
    // Hard-right pan
    //--------------------------------------------------------------------------
    synth.setPan(1.0f);
    synth.reset();

    juce::AudioBuffer<float> outRight(2, 24000);
    synth.process(outRight);
    float rightEnergyL = bufferEnergy(outRight, 0);
    float rightEnergyR = bufferEnergy(outRight, 1);

    REQUIRE(rightEnergyR > rightEnergyL * 10.0f);

    //--------------------------------------------------------------------------
    // Centre pan (equal energy)
    //--------------------------------------------------------------------------
    synth.setPan(0.0f);
    synth.reset();

    juce::AudioBuffer<float> outCentre(2, 24000);
    synth.process(outCentre);
    float centreEnergyL = bufferEnergy(outCentre, 0);
    float centreEnergyR = bufferEnergy(outCentre, 1);

    REQUIRE(std::abs(centreEnergyL - centreEnergyR) < centreEnergyL * 0.1f);
}

TEST_CASE("GranularSynthesizer - reset clears state", "[granular]")
{
    ana::GranularSynthesizer synth;

    std::vector<float> buffer(48000, 1.0f);
    synth.setSourceBuffer(buffer, 48000.0);
    synth.setDensity(200.0f);
    synth.setGrainSize(20.0f);
    synth.setPosition(0.5f);
    synth.setAmplitude(0.5f);

    juce::AudioBuffer<float> output(2, 48000);
    synth.process(output);

    REQUIRE(synth.getTotalGrainsSpawned() > 0);

    synth.reset();

    REQUIRE(synth.getActiveGrainCount() == 0);
    REQUIRE(synth.getTotalGrainsSpawned() == 0);
}

TEST_CASE("GranularSynthesizer - empty source produces silence", "[granular]")
{
    ana::GranularSynthesizer synth;

    std::vector<float> emptyBuffer;
    synth.setSourceBuffer(emptyBuffer, 44100.0);

    synth.setDensity(100.0f);
    synth.setGrainSize(30.0f);

    juce::AudioBuffer<float> output(2, 1024);
    synth.process(output);

    float energy = bufferEnergy(output, 0) + bufferEnergy(output, 1);
    REQUIRE(energy == 0.0f);
}
