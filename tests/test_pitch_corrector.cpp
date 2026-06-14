#include <catch2/catch_all.hpp>
#include "../src/dsp/PitchCorrector.h"
#include <cmath>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

using namespace ana;

TEST_CASE("PitchCorrector - initial state", "[pitch_corrector][init]")
{
    PitchCorrector pc;
    SUCCEED();
}

TEST_CASE("PitchCorrector - basic parameters", "[pitch_corrector][params]")
{
    PitchCorrector pc;
    pc.setAlgorithm(PitchAlgorithm::Spectral);
    pc.setPitchShift(2.0f);
    pc.setFormantPreservation(0.8f);
    pc.setCorrectionAmount(0.5f);
    pc.setSampleRate(48000.0);
    pc.setFftSize(1024);
    SUCCEED();
}

TEST_CASE("PitchCorrector - detect pitch", "[pitch_corrector][detect]")
{
    PitchCorrector pc;
    pc.setSampleRate(44100.0);
    
    // Generate a 440Hz sine wave (A4, MIDI note 69)
    std::vector<float> sine(44100);
    for (size_t i = 0; i < sine.size(); ++i) {
        sine[i] = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 44100.0f);
    }
    
    float midiNote = pc.detectPitch(sine, 44100.0);
    // Should be close to 69.0
    REQUIRE(midiNote > 68.0f);
    REQUIRE(midiNote < 70.0f);
}

TEST_CASE("PitchCorrector - algorithms process", "[pitch_corrector][process]")
{
    PitchCorrector pc;
    pc.setSampleRate(44100.0);
    pc.setPitchShift(1.0f); // shift up 1 semitone

    juce::AudioBuffer<float> buffer(2, 2048);
    for (int ch = 0; ch < 2; ++ch) {
        auto* writePtr = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            writePtr[i] = std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / 44100.0f);
        }
    }

    SECTION("Simple")
    {
        pc.setAlgorithm(PitchAlgorithm::Simple);
        REQUIRE_NOTHROW(pc.process(buffer));
    }

    SECTION("PhaseVocoder")
    {
        pc.setAlgorithm(PitchAlgorithm::PhaseVocoder);
        REQUIRE_NOTHROW(pc.process(buffer));
    }

    SECTION("Spectral")
    {
        pc.setAlgorithm(PitchAlgorithm::Spectral);
        REQUIRE_NOTHROW(pc.process(buffer));
    }

    SECTION("Formant")
    {
        pc.setAlgorithm(PitchAlgorithm::Formant);
        REQUIRE_NOTHROW(pc.process(buffer));
    }

    SECTION("Granular")
    {
        pc.setAlgorithm(PitchAlgorithm::Granular);
        REQUIRE_NOTHROW(pc.process(buffer));
    }
}

TEST_CASE("PitchCorrector - edge cases", "[pitch_corrector][edge]")
{
    PitchCorrector pc;
    
    SECTION("Empty buffer")
    {
        juce::AudioBuffer<float> emptyBuffer(2, 0);
        REQUIRE_NOTHROW(pc.process(emptyBuffer));
    }

    SECTION("Reset")
    {
        pc.reset();
        SUCCEED();
    }
}
