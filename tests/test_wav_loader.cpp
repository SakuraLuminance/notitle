#include <catch2/catch_all.hpp>
#include "dsp/WavLoader.h"
#include <juce_core/juce_core.h>

TEST_CASE("WavLoader - load non-existent file", "[wav]")
{
    ana::WavLoader loader;
    auto result = loader.loadWav(juce::File("/nonexistent/file.wav"));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("WavLoader - load invalid file", "[wav]")
{
    ana::WavLoader loader;
    // Create a temporary invalid file
    juce::File tempFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("invalid.wav");
    tempFile.replaceWithText("not a wav file");

    auto result = loader.loadWav(tempFile);
    REQUIRE_FALSE(result.has_value());

    tempFile.deleteFile();
}

TEST_CASE("WavLoader - class instantiation", "[wav]")
{
    ana::WavLoader loader;
    // Just verify it can be instantiated without errors
    REQUIRE(true);
}

// Helper: create a temporary WAV file at a given sample rate with a test tone
static juce::File createTestWav(double sampleRate, int numSamples, const juce::String& suffix)
{
    juce::File wavFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("test_" + suffix + ".wav");
    wavFile.deleteFile();

    auto* fileStream = wavFile.createOutputStream();
    REQUIRE(fileStream != nullptr);

    juce::WavAudioFormat wavFormat;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        wavFormat.createWriterFor(fileStream, sampleRate, 1, 16, {}, 0));
    REQUIRE(writer != nullptr);

    juce::AudioBuffer<float> buffer(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        buffer.setSample(0, i, std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f *
                                         static_cast<float>(i) / static_cast<float>(sampleRate)));

    writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    writer->flush();

    return wavFile;
}

TEST_CASE("WavLoader - same sample rate no resampling", "[wav][resample]")
{
    constexpr double sourceRate = 44100.0;
    constexpr int numSamples = 4410;  // 0.1 seconds
    juce::File wavFile = createTestWav(sourceRate, numSamples, "same_rate");
    REQUIRE(wavFile.existsAsFile());

    ana::WavLoader loader;
    auto result = loader.loadWav(wavFile, sourceRate);

    REQUIRE(result.has_value());
    CHECK(result->sampleRate == sourceRate);
    CHECK(result->numChannels == 1);
    CHECK(result->samples.size() == static_cast<size_t>(numSamples));
    CHECK(std::abs(result->durationSeconds - 0.1) < 0.001);

    wavFile.deleteFile();
}

TEST_CASE("WavLoader - resample 48000Hz to 44100Hz", "[wav][resample]")
{
    constexpr double sourceRate = 48000.0;
    constexpr int numSamples = 4800;  // 0.1 seconds at 48k
    juce::File wavFile = createTestWav(sourceRate, numSamples, "48k");
    REQUIRE(wavFile.existsAsFile());

    constexpr double targetRate = 44100.0;
    ana::WavLoader loader;
    auto result = loader.loadWav(wavFile, targetRate);

    REQUIRE(result.has_value());
    CHECK(result->sampleRate == targetRate);
    CHECK(result->numChannels == 1);
    // Expected output length: 4800 * (44100/48000) ≈ 4410 samples
    CHECK(result->samples.size() >= 4400);
    CHECK(result->samples.size() <= 4420);
    CHECK(std::abs(result->durationSeconds - 0.1) < 0.005);

    wavFile.deleteFile();
}

TEST_CASE("WavLoader - upsample 22050Hz to 44100Hz", "[wav][resample]")
{
    constexpr double sourceRate = 22050.0;
    constexpr int numSamples = 2205;  // 0.1 seconds at 22.05k
    juce::File wavFile = createTestWav(sourceRate, numSamples, "22k");
    REQUIRE(wavFile.existsAsFile());

    constexpr double targetRate = 44100.0;
    ana::WavLoader loader;
    auto result = loader.loadWav(wavFile, targetRate);

    REQUIRE(result.has_value());
    CHECK(result->sampleRate == targetRate);
    // Expected output: 2205 * (44100/22050) = 4410 samples
    CHECK(result->samples.size() >= 4400);
    CHECK(result->samples.size() <= 4420);

    wavFile.deleteFile();
}

TEST_CASE("WavLoader - resample 96000Hz to 44100Hz", "[wav][resample]")
{
    constexpr double sourceRate = 96000.0;
    constexpr int numSamples = 9600;  // 0.1 seconds at 96k
    juce::File wavFile = createTestWav(sourceRate, numSamples, "96k");
    REQUIRE(wavFile.existsAsFile());

    constexpr double targetRate = 44100.0;
    ana::WavLoader loader;
    auto result = loader.loadWav(wavFile, targetRate);

    REQUIRE(result.has_value());
    CHECK(result->sampleRate == targetRate);
    // Expected output: 9600 * (44100/96000) ≈ 4410 samples
    CHECK(result->samples.size() >= 4390);
    CHECK(result->samples.size() <= 4430);

    wavFile.deleteFile();
}
