#include <catch2/catch_all.hpp>
#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "dsp/GranularSynthesizer.h"

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
    // One grain per grain-duration so grains do NOT overlap: the span of
    // non-zero output then measures exactly one window.  (At high density
    // the sample-accurate scheduler spreads overlapping grains and the
    // non-zero span covers the whole buffer.)
    synth.setDensity(48000.0f / expectedSamples);
    synth.setPosition(0.5f);
    synth.setPitch(0.0f);
    synth.setAmplitude(0.5f);
    synth.setWindowType(ana::GrainWindowType::Triangle);

    // Process exactly two grain-durations: the first grain spawns when the
    // density accumulator first crosses 1.0 (≈ sample 960) and ends just
    // before the buffer end; the second spawn falls outside the buffer.
    juce::AudioBuffer<float> output(1, 2 * expectedSamples);
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


//==============================================================================
// Window-table cache reservation (P7): the processor reserves the cache in
// prepareToPlay so a grain-size change can no longer allocate on the audio
// thread.  Reserving must not alter the rendered result.
//==============================================================================

TEST_CASE("GranularSynthesizer - reserveWindowCache grows the cache capacity", "[granular]")
{
    ana::GranularSynthesizer synth;
    const int reserved = 4800;   // 100 ms at 48 kHz

    REQUIRE(synth.getWindowCacheCapacity() < reserved);
    synth.reserveWindowCache(reserved);
    REQUIRE(synth.getWindowCacheCapacity() >= reserved);

    // A subsequent render (which resize()s the cache to the grain duration)
    // must not shrink the reserved capacity.
    std::vector<float> source(48000, 0.0f);
    for (size_t i = 0; i < source.size(); ++i)
        source[i] = 0.25f * std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / 48000.0);

    synth.setSourceBuffer(source, 48000.0);
    synth.setDensity(80.0f);

    juce::AudioBuffer<float> output(2, 512);
    for (const float ms : { 5.0f, 100.0f, 2.0f, 60.0f })
    {
        synth.setGrainSize(ms);
        synth.process(output);
        REQUIRE(synth.getWindowCacheCapacity() >= reserved);
    }

    // reserveWindowCache() with a nonsense size is ignored
    synth.reserveWindowCache(0);
    REQUIRE(synth.getWindowCacheCapacity() >= reserved);
}

TEST_CASE("GranularSynthesizer - reserving the window cache does not change output", "[granular]")
{
    const double sampleRate = 44100.0;
    std::vector<float> source(44100);
    for (size_t i = 0; i < source.size(); ++i)
        source[i] = 0.4f * std::sin(2.0 * M_PI * 330.0 * static_cast<double>(i) / sampleRate);

    ana::GranularSynthesizer reserved;
    reserved.reserveWindowCache(static_cast<int>(sampleRate * 0.1) + 2);

    ana::GranularSynthesizer plain;

    for (auto* synth : { &reserved, &plain })
    {
        synth->setSourceBuffer(source, sampleRate);
        synth->setDensity(40.0f);
        synth->setGrainSize(35.0f);
        synth->setPosition(0.25f);
        synth->setPitch(0.0f);
        synth->setAmplitude(0.5f);
        synth->setPan(0.0f);
        synth->setWindowType(ana::GrainWindowType::Hann);
        synth->setPositionModulation(ana::PositionModulation::Off);
    }

    juce::AudioBuffer<float> outReserved(2, 1024);
    juce::AudioBuffer<float> outPlain(2, 1024);

    // Two identical calls per instance (the first warms the window table).
    reserved.process(outReserved);
    plain.process(outPlain);
    reserved.process(outReserved);
    plain.process(outPlain);

    REQUIRE(bufferEnergy(outReserved, 0) > 0.0f);
    REQUIRE(bufferEnergy(outReserved, 0) == Catch::Approx(bufferEnergy(outPlain, 0)).margin(1.0e-6f));
    REQUIRE(bufferEnergy(outReserved, 1) == Catch::Approx(bufferEnergy(outPlain, 1)).margin(1.0e-6f));
}

//==============================================================================
// The grain pool scans only the slots that can hold grains.

TEST_CASE("GranularSynthesizer empties its grain pool between spawns", "[granular]")
{
    ana::GranularSynthesizer synth;
    const std::vector<float> source(48000, 0.5f);
    synth.setSourceBuffer(source, 48000.0);
    synth.setAmplitude(0.5f);
    synth.setPosition(0.5f);

    juce::AudioBuffer<float> buf(2, 512);

    auto render = [&](int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            synth.process(buf);
        }
    };

    // 1. Push the pool wide: 200 grains/s of 100 ms overlap ~20 grains, i.e.
    //    the scanned prefix really has to grow well past slot 0.
    synth.setGrainSize(100.0f);
    synth.setDensity(200.0f);
    render(94);                        // ~1 s at 48 kHz

    const int crowded = synth.getActiveGrainCount();
    REQUIRE(crowded >= 10);
    REQUIRE(buf.getMagnitude(0, buf.getNumSamples()) > 0.0f);

    // 2. Short grains + the minimum density: one 5 ms grain per second, so the
    //    pool must drain completely between spawns.  A scan prefix that stopped
    //    early would leave the grains from step 1 active forever, and
    //    getActiveGrainCount() scans the whole pool on purpose to catch that.
    synth.setGrainSize(5.0f);
    synth.setDensity(1.0f);

    int minActive = 1 << 30;
    for (int b = 0; b < 190; ++b)      // ~2 s
    {
        render(1);
        minActive = std::min(minActive, synth.getActiveGrainCount());
    }

    REQUIRE(minActive == 0);
}

//==============================================================================
// The GRAIN page draws these.

TEST_CASE("GranularSynthesizer hands the UI a normalised grain cloud", "[granular]")
{
    ana::GranularSynthesizer synth;
    ana::GranularSynthesizer::GrainSnapshot cloud[64];

    // Nothing to draw before a source is loaded, and bad arguments are ignored.
    REQUIRE(synth.getActiveGrainSnapshots(cloud, 64) == 0);
    REQUIRE(synth.getActiveGrainSnapshots(nullptr, 64) == 0);
    REQUIRE(synth.getActiveGrainSnapshots(cloud, 0) == 0);

    const std::vector<float> source(48000, 0.5f);   // 1 s
    synth.setSourceBuffer(source, 48000.0);
    synth.setGrainSize(100.0f);
    synth.setDensity(200.0f);                       // ~20 grains overlap
    synth.setAmplitude(0.5f);
    synth.setPosition(0.5f);

    juce::AudioBuffer<float> buf(2, 512);
    for (int b = 0; b < 40; ++b)
    {
        buf.clear();
        synth.process(buf);
    }

    const int count = synth.getActiveGrainSnapshots(cloud, 64);
    REQUIRE(count == synth.getActiveGrainCount());
    REQUIRE(count > 0);

    for (int i = 0; i < count; ++i)
    {
        REQUIRE(cloud[i].position  >= 0.0f);
        REQUIRE(cloud[i].position  <= 1.0f);
        REQUIRE(cloud[i].progress  >= 0.0f);
        REQUIRE(cloud[i].progress  <= 1.0f);
        REQUIRE(cloud[i].duration  >  0.0f);
        REQUIRE(cloud[i].duration  <= 1.0f);
        REQUIRE(cloud[i].amplitude == Catch::Approx(0.5f));
        REQUIRE(cloud[i].pan       >= -1.0f);
        REQUIRE(cloud[i].pan       <= 1.0f);
    }

    // 100 ms of a 1 s source, wherever the grain currently reads.
    REQUIRE(cloud[0].duration == Catch::Approx(0.1f).margin(0.01f));

    // A smaller buffer truncates instead of overflowing.
    REQUIRE(synth.getActiveGrainSnapshots(cloud, 3) == 3);

    synth.reset();
    REQUIRE(synth.getActiveGrainSnapshots(cloud, 64) == 0);
}

//==============================================================================
// The GRAIN page draws this strip above the cloud.

TEST_CASE("GranularSynthesizer publishes a source envelope for the display", "[granular]")
{
    ana::GranularSynthesizer synth;
    float peaks[512];

    REQUIRE(synth.getSourcePeaks(peaks, 512) == 0);
    REQUIRE(synth.getSourcePeaks(nullptr, 512) == 0);
    REQUIRE(synth.getSourcePeaks(peaks, 0) == 0);

    // A ramp: the last bucket is the loudest, every bucket is a fraction.
    std::vector<float> ramp(4096);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(i) / static_cast<float>(ramp.size() - 1);

    synth.setSourceBuffer(ramp, 48000.0);
    const int buckets = synth.getSourcePeaks(peaks, 512);
    REQUIRE(buckets == ana::GranularSynthesizer::kSourcePeakBuckets);
    REQUIRE(peaks[0] < peaks[buckets - 1]);

    for (int i = 0; i < buckets; ++i)
    {
        REQUIRE(peaks[i] >= 0.0f);
        REQUIRE(peaks[i] <= 1.0f);
    }

    // An impulse lands in the bucket that covers it and nowhere else: the
    // magnitude is used, not the sign.
    std::vector<float> impulse(4096, 0.0f);
    impulse[2048] = -0.9f;
    synth.setSourceBuffer(impulse, 48000.0);
    REQUIRE(synth.getSourcePeaks(peaks, 512) == buckets);

    int loudest = 0;
    for (int i = 1; i < buckets; ++i)
        if (peaks[i] > peaks[loudest])
            loudest = i;

    REQUIRE(loudest == 128);                        // 2048 / 4096 of the way in
    REQUIRE(peaks[loudest] == Catch::Approx(0.9f));

    // Silence: every bucket collapses, which proves the scan really runs.
    const std::vector<float> quiet(4096, 0.0f);
    synth.setSourceBuffer(quiet, 48000.0);
    REQUIRE(synth.getSourcePeaks(peaks, 512) == buckets);
    for (int i = 0; i < buckets; ++i)
        REQUIRE(peaks[i] == Catch::Approx(0.0f));

    // A smaller buffer truncates instead of overflowing.
    REQUIRE(synth.getSourcePeaks(peaks, 4) == 4);
}

//==============================================================================
// Stereo spread and reverse grains (GRAIN page: SPREAD / REVERSE).

namespace
{
    struct StereoResult { float leftRightDiff = 0.0f; float energy = 0.0f; };

    StereoResult renderGrainStereo(ana::GranularSynthesizer& synth,
                                   const std::vector<float>& source, double sr,
                                   int blocks)
    {
        synth.setSourceBuffer(source, sr);
        synth.setGrainSize(50.0f);
        synth.setDensity(200.0f);
        synth.setPosition(0.5f);
        synth.setAmplitude(0.5f);

        StereoResult result;
        juce::AudioBuffer<float> buf(2, 512);

        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            synth.process(buf);

            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float l = buf.getSample(0, i);
                const float r = buf.getSample(1, i);
                result.energy = juce::jmax(result.energy, std::abs(l));
                result.leftRightDiff = juce::jmax(result.leftRightDiff, std::abs(l - r));
            }
        }

        return result;
    }
}

TEST_CASE("GranularSynthesizer spreads grains across the stereo field", "[granular]")
{
    std::vector<float> source(48000);
    for (size_t i = 0; i < source.size(); ++i)
        source[i] = std::sin(static_cast<float>(i) * 0.05f);

    ana::GranularSynthesizer tight;
    const auto tightResult = renderGrainStereo(tight, source, 48000.0, 40);
    REQUIRE(tightResult.energy > 0.01f);                       // it is sounding
    REQUIRE(tightResult.leftRightDiff < tightResult.energy * 0.01f);

    ana::GranularSynthesizer wide;
    wide.setStereoSpread(1.0f);
    const auto wideResult = renderGrainStereo(wide, source, 48000.0, 40);
    REQUIRE(wideResult.energy > 0.01f);
    REQUIRE(wideResult.leftRightDiff > wideResult.energy * 0.05f);
}

TEST_CASE("GranularSynthesizer can play grains backwards", "[granular]")
{
    // A ramp makes direction audible: the same window over the same span reads
    // the material in the opposite order.
    std::vector<float> ramp(48000);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(i) / static_cast<float>(ramp.size() - 1);

    auto renderForward = [&ramp](bool backwards)
    {
        ana::GranularSynthesizer synth;
        synth.setSourceBuffer(ramp, 48000.0);
        synth.setGrainSize(50.0f);
        synth.setDensity(200.0f);
        synth.setPosition(0.5f);
        synth.setAmplitude(0.5f);
        synth.setReverseProbability(backwards ? 1.0f : 0.0f);

        juce::AudioBuffer<float> buf(2, 512);
        std::vector<float> rendered;

        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            synth.process(buf);

            for (int i = 0; i < buf.getNumSamples(); ++i)
                rendered.push_back(buf.getSample(0, i));
        }

        return rendered;
    };

    const auto forward  = renderForward(false);
    const auto reversed = renderForward(true);
    REQUIRE(forward.size() == reversed.size());

    float forwardPeak = 0.0f, reversedPeak = 0.0f, biggestGap = 0.0f;

    for (size_t i = 0; i < forward.size(); ++i)
    {
        forwardPeak  = juce::jmax(forwardPeak, std::abs(forward[i]));
        reversedPeak = juce::jmax(reversedPeak, std::abs(reversed[i]));
        biggestGap   = juce::jmax(biggestGap, std::abs(forward[i] - reversed[i]));
    }

    REQUIRE(forwardPeak > 0.01f);
    REQUIRE(reversedPeak > 0.01f);                       // still a full grain
    REQUIRE(reversedPeak == Catch::Approx(forwardPeak).epsilon(0.25));
    REQUIRE(biggestGap > 0.001f);                        // but not the same audio
}

TEST_CASE("GranularSynthesizer reports grain direction to the display", "[granular]")
{
    const std::vector<float> source(48000, 0.5f);
    ana::GranularSynthesizer::GrainSnapshot cloud[64];
    juce::AudioBuffer<float> buf(2, 512);

    auto render = [&](float reverseProbability)
    {
        ana::GranularSynthesizer synth;
        synth.setSourceBuffer(source, 48000.0);
        synth.setGrainSize(50.0f);
        synth.setDensity(200.0f);
        synth.setPosition(0.5f);
        synth.setAmplitude(0.5f);
        synth.setReverseProbability(reverseProbability);

        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            synth.process(buf);
        }

        const int count = synth.getActiveGrainSnapshots(cloud, 64);
        REQUIRE(count > 0);

        int reversedCount = 0;
        for (int i = 0; i < count; ++i)
            if (cloud[i].reversed)
                ++reversedCount;

        return reversedCount;
    };

    REQUIRE(render(0.0f) == 0);      // every grain forward
    REQUIRE(render(1.0f) > 0);       // the display can tell them apart
}

TEST_CASE("GranularSynthesizer reports each grain's pan to the display", "[granular]")
{
    const std::vector<float> source(48000, 0.5f);
    ana::GranularSynthesizer::GrainSnapshot cloud[64];
    juce::AudioBuffer<float> buf(2, 512);

    auto panSpread = [&](float spread)
    {
        ana::GranularSynthesizer synth;
        synth.setSourceBuffer(source, 48000.0);
        synth.setGrainSize(50.0f);
        synth.setDensity(200.0f);
        synth.setPosition(0.5f);
        synth.setAmplitude(0.5f);
        synth.setStereoSpread(spread);

        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            synth.process(buf);
        }

        const int count = synth.getActiveGrainSnapshots(cloud, 64);
        REQUIRE(count > 1);

        float lowest = 1.0f, highest = -1.0f;
        for (int i = 0; i < count; ++i)
        {
            REQUIRE(cloud[i].pan >= -1.0f);
            REQUIRE(cloud[i].pan <= 1.0f);
            lowest  = juce::jmin(lowest, cloud[i].pan);
            highest = juce::jmax(highest, cloud[i].pan);
        }

        return highest - lowest;
    };

    REQUIRE(panSpread(0.0f) < 1.0e-6f);   // no spread: every grain shares one pan
    REQUIRE(panSpread(1.0f) > 0.05f);     // spread: the clicks land apart
}






