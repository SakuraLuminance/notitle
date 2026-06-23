#include <catch2/catch_all.hpp>
#include "dsp/STFTAnalyzer.h"
#include <cmath>

TEST_CASE("STFTAnalyzer - analyze silence", "[stft]")
{
    ana::STFTAnalyzer analyzer;
    ana::AudioFileData audio;
    ana::STFTConfig config;

    // Create 1 second of silence
    audio.samples.resize(44100, 0.0f);
    audio.sampleRate = 44100.0;

    auto frames = analyzer.analyze(audio, config);
    REQUIRE_FALSE(frames.empty());

    // All magnitudes should be near zero
    for (const auto& frame : frames)
    {
        for (const auto& bin : frame)
        {
            REQUIRE(std::abs(bin) < 0.01f);
        }
    }
}

TEST_CASE("STFTAnalyzer - analyze 440Hz sine", "[stft]")
{
    ana::STFTAnalyzer analyzer;
    ana::AudioFileData audio;
    ana::STFTConfig config;

    // Create 1 second of 440Hz sine
    const double sampleRate = 44100.0;
    const double frequency = 440.0;
    const int numSamples = static_cast<int>(sampleRate);

    audio.samples.resize(numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        audio.samples[i] = static_cast<float>(
            std::sin(2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate));
    }
    audio.sampleRate = sampleRate;

    auto frames = analyzer.analyze(audio, config);
    REQUIRE_FALSE(frames.empty());

    // Find peak bin in first frame
    int peakBin = 0;
    float peakMag = 0.0f;
    const auto& firstFrame = frames[0];
    for (int i = 0; i < static_cast<int>(firstFrame.size()); ++i)
    {
        float mag = std::abs(firstFrame[i]);
        if (mag > peakMag)
        {
            peakMag = mag;
            peakBin = i;
        }
    }

    // Peak should be near bin 440 * 2048 / 44100 ≈ 20.4
    float expectedBin = static_cast<float>(frequency * config.fftSize / sampleRate);
    REQUIRE(std::abs(peakBin - expectedBin) < 2.0f);
}

TEST_CASE("STFTAnalyzer - frame count", "[stft]")
{
    ana::STFTAnalyzer analyzer;
    ana::AudioFileData audio;
    ana::STFTConfig config;

    // Create 1 second of audio
    audio.samples.resize(44100, 0.0f);
    audio.sampleRate = 44100.0;

    auto frames = analyzer.analyze(audio, config);

    // Expected frames: (44100 - 2048) / 512 + 1 ≈ 83
    int expectedFrames = (44100 - 2048) / 512 + 1;
    REQUIRE(static_cast<int>(frames.size()) == expectedFrames);
}

TEST_CASE("STFTAnalyzer - spectrum size", "[stft]")
{
    ana::STFTAnalyzer analyzer;
    ana::AudioFileData audio;
    ana::STFTConfig config;

    audio.samples.resize(44100, 0.0f);
    audio.sampleRate = 44100.0;

    auto frames = analyzer.analyze(audio, config);

    // Each frame should have fftSize/2 + 1 bins
    int expectedBins = config.fftSize / 2 + 1;
    REQUIRE(static_cast<int>(frames[0].size()) == expectedBins);
}
