#include <catch2/catch_all.hpp>
#include "../src/dsp/SampleProcessor.h"
#include <cmath>
#include <vector>

using namespace ana;

static constexpr double testSampleRate = 44100.0;

TEST_CASE("SampleProcessor - initial state", "[sample][init]")
{
    SampleProcessor sp;
    sp.setSampleRate(testSampleRate);
    SUCCEED();
}

TEST_CASE("SampleProcessor - midiNoteToFrequency", "[sample][utils]")
{
    REQUIRE(SampleProcessor::midiNoteToFrequency(69) == Catch::Approx(440.0f));
    REQUIRE(SampleProcessor::midiNoteToFrequency(69, 100.0f) == Catch::Approx(440.0f * std::pow(2.0f, 100.0f/1200.0f)));
}

TEST_CASE("SampleProcessor - getPitchDisplayText", "[sample][utils]")
{
    SampleProcessor sp;
    REQUIRE(sp.getPitchDisplayText(0.0f) == "---");
    // 440Hz is A4
    juce::String text = sp.getPitchDisplayText(440.0f);
    REQUIRE(text.contains("A4"));
}

TEST_CASE("SampleProcessor - get/set properties", "[sample][props]")
{
    SampleProcessor sp;
    sp.setRootNote(69);
    REQUIRE(sp.getRootNote() == 69);
    
    sp.setRootFineTune(10.0f);
    REQUIRE(sp.getRootFineTune() == Catch::Approx(10.0f));
}

TEST_CASE("SampleProcessor - detectRootNote", "[sample][detect]")
{
    SampleProcessor sp;
    std::vector<float> audio(44100);
    // 440Hz sine wave (A4, midi note 69)
    for (int i = 0; i < 44100; ++i)
    {
        audio[i] = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / testSampleRate);
    }

    int note = sp.detectRootNote(audio, testSampleRate);
    REQUIRE(note == 69);
}

TEST_CASE("SampleProcessor - detectPitch", "[sample][detect]")
{
    SampleProcessor sp;
    std::vector<float> audio(44100);
    // 440Hz sine wave (A4, midi note 69)
    for (int i = 0; i < 44100; ++i)
    {
        audio[i] = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / testSampleRate);
    }

    auto result = sp.detectPitch(audio, testSampleRate);
    REQUIRE(result.detectedFreq == Catch::Approx(440.0f).margin(5.0f));
    REQUIRE(result.detectedMidiNote == 69);
}

TEST_CASE("SampleProcessor - flattenPitch", "[sample][process]")
{
    SampleProcessor sp;
    sp.setSampleRate(testSampleRate);
    
    std::vector<float> audio(4096, 0.0f);
    
    SECTION("No formant preservation")
    {
        auto out = sp.flattenPitch(audio, testSampleRate, 69, 0.7f, false);
        REQUIRE(out.size() == audio.size());
    }
    
    SECTION("With formant preservation")
    {
        auto out = sp.flattenPitch(audio, testSampleRate, 69, 0.7f, true);
        REQUIRE(out.size() == audio.size());
    }
}

TEST_CASE("SampleProcessor - reset", "[sample][reset]")
{
    SampleProcessor sp;
    sp.reset();
    SUCCEED();
}
