#include <catch2/catch_all.hpp>
#include "dsp/BpmDetector.h"
#include <cmath>
#include <vector>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("BpmDetector - empty data", "[bpm][edge]")
{
    BpmDetector detector;
    detector.setSampleRate(testSampleRate);
    std::vector<float> emptyData;

    REQUIRE(detector.detectBpm(emptyData) == Approx(120.0f));
    REQUIRE(detector.detectFirstBeatOffset(emptyData) == 0);
}

TEST_CASE("BpmDetector - detect BPM", "[bpm][detect]")
{
    BpmDetector detector;
    detector.setSampleRate(testSampleRate);

    // Create a 120 BPM signal: 2 beats per second -> beat every 22050 samples
    std::vector<float> data(static_cast<size_t>(testSampleRate * 4), 0.0f); // 4 seconds
    int beatInterval = static_cast<int>(testSampleRate * 0.5); // 120 BPM = 0.5s per beat

    for (size_t i = 0; i < data.size(); ++i)
    {
        if (i % beatInterval < 100) // 100 sample impulse
            data[i] = 1.0f;
    }

    float bpm = detector.detectBpm(data);
    
    // Autocorrelation might not be perfectly exact due to downsampling, 
    // but should be very close to 120
    REQUIRE(bpm > 115.0f);
    REQUIRE(bpm < 125.0f);
}

TEST_CASE("BpmDetector - detect first beat", "[bpm][onset]")
{
    BpmDetector detector;
    detector.setSampleRate(testSampleRate);

    // Silence for 10000 samples, then an impulse
    std::vector<float> data(44100, 0.0f);
    int offset = 10000;
    
    // Smooth attack
    for (int i = 0; i < 100; ++i)
    {
        data[offset + i] = static_cast<float>(i) / 100.0f;
    }
    
    int detectedOffset = detector.detectFirstBeatOffset(data);
    
    // Should be close to 10000
    REQUIRE(detectedOffset > 9900);
    REQUIRE(detectedOffset < 10100);
}
