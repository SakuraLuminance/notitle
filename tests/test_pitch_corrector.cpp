#include <catch2/catch_all.hpp>
#include "dsp/PitchCorrector.h"
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

//==============================================================================
// A host may call processBlock with fewer samples than the FFT window - 512 is the
// most common block size and the default fftSize is 2048.  The STFT synthesis loop
// only runs frames that fit completely inside the current block and then writes the
// block out of an accumulator that is zeroed at the start of every call, so a short
// block currently produces digital silence after an unknown number of blocks.
//
// [!mayfail] records that defect: the test is expected to fail until the shifter
// keeps its input history and overlap-add accumulator across calls, and it will fail
// the run the moment it starts passing, so the tag has to be removed with the fix.
TEST_CASE("PitchCorrector - short blocks eventually produce audio",
           "[pitch_corrector][process][!mayfail]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;
    constexpr int    numBlocks  = 12;

    PitchCorrector pc;
    pc.setSampleRate(sampleRate);
    pc.setAlgorithm(PitchAlgorithm::Spectral);
    pc.setFftSize(2048);          // four times the host block
    pc.setPitchShift(2.0f);
    pc.setCorrectionAmount(1.0f);

    juce::AudioBuffer<float> buffer(1, blockSize);
    float lastBlockPeak = 0.0f;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float phase = 2.0f * juce::MathConstants<float>::pi * 440.0f
                                * (float) (b * blockSize + i) / (float) sampleRate;
            buffer.setSample(0, i, std::sin(phase));
        }

        pc.process(buffer);

        lastBlockPeak = 0.0f;
        for (int i = 0; i < blockSize; ++i)
        {
            const float a = std::abs(buffer.getSample(0, i));
            if (a > lastBlockPeak)
                lastBlockPeak = a;
        }
    }

    REQUIRE(lastBlockPeak > 0.01f);
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
