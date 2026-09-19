#include <catch2/catch_all.hpp>

#include "dsp/WavLoader.h"
#include "dsp/MultiFilter.h"
#include "dsp/MeteringEngine.h"

#include <vector>

using namespace ana;

//==============================================================================
// Regression tests for defects found in the algorithm/vulnerability audit:
//   * WavLoader sized buffers from header fields it never validated
//   * MultiFilter's comb delay line was never sized, so its delay was always 2 samples
//   * MeteringEngine grew its interleave buffer from the audio thread
//==============================================================================

namespace
{

/** Writes a RIFF/WAVE file whose header is allowed to lie about the payload. */
juce::File writeWav (const juce::String& name,
                     juce::uint16 numChannels,
                     juce::uint32 sampleRate,
                     juce::uint32 declaredDataBytes,
                     int payloadSamples)
{
    std::vector<juce::uint8> bytes;

    auto put32 = [&bytes] (juce::uint32 v)
    {
        bytes.push_back (static_cast<juce::uint8> (v & 0xff));
        bytes.push_back (static_cast<juce::uint8> ((v >> 8) & 0xff));
        bytes.push_back (static_cast<juce::uint8> ((v >> 16) & 0xff));
        bytes.push_back (static_cast<juce::uint8> ((v >> 24) & 0xff));
    };
    auto put16 = [&bytes] (juce::uint16 v)
    {
        bytes.push_back (static_cast<juce::uint8> (v & 0xff));
        bytes.push_back (static_cast<juce::uint8> ((v >> 8) & 0xff));
    };
    auto putStr = [&bytes] (const char* s)
    {
        while (*s != 0)
            bytes.push_back (static_cast<juce::uint8> (*s++));
    };

    putStr ("RIFF");
    put32 (36u + declaredDataBytes);
    putStr ("WAVE");
    putStr ("fmt ");
    put32 (16);
    put16 (1);                                            // PCM
    put16 (numChannels);
    put32 (sampleRate);
    put32 (sampleRate * static_cast<juce::uint32> (numChannels) * 2u);
    put16 (static_cast<juce::uint16> (numChannels * 2));
    put16 (16);
    putStr ("data");
    put32 (declaredDataBytes);

    for (int i = 0; i < payloadSamples; ++i)
        put16 (0);

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile (name);
    file.deleteFile();
    file.replaceWithData (bytes.data(), bytes.size());
    return file;
}

/** Index of the first non-zero sample after index 0. */
int firstEchoIndex (const juce::AudioBuffer<float>& buffer)
{
    for (int i = 1; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (0, i)) > 1.0e-6f)
            return i;

    return -1;
}

/** Runs one impulse through a single Comb slot and returns the echo index. */
int combEchoIndexForCutoff (double cutoffHz)
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 128;

    MultiFilter filter;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };
    filter.prepare (spec);

    FilterParams params;
    params.cutoff    = cutoffHz;
    params.resonance = 0.5f;   // 0.475 feedback, so the echo is audible in the output
    params.drive     = 0.0f;   // damping 0.5
    params.mix       = 1.0f;   // fully wet
    filter.addSlot (FilterType::Comb, params);

    juce::AudioBuffer<float> buffer (1, blockSize);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);

    filter.process (buffer);
    return firstEchoIndex (buffer);
}

} // namespace

//==============================================================================
TEST_CASE ("WavLoader refuses a header that claims impossible channel counts",
           "[wav_loader][hardening]")
{
    // 0x8000 channels passes JUCE's own gate (numChannels > 0) and would be handed
    // to AudioBuffer as a 32768-channel allocation.
    const auto file = writeWav ("anap_hardening_channels.wav", 0x8000u, 44100u,
                                0x20000000u, 4);

    WavLoader loader;
    REQUIRE_FALSE (loader.loadWav (file, 44100.0).has_value());

    file.deleteFile();
}

TEST_CASE ("WavLoader refuses a header with an absurd sample rate",
           "[wav_loader][hardening]")
{
    // sampleRate = 1 makes the resample length numSamples * 44100, which overflows
    // int and turns into a negative AudioBuffer size.
    const auto file = writeWav ("anap_hardening_rate.wav", 1u, 1u, 0x00010000u, 16);

    WavLoader loader;
    REQUIRE_FALSE (loader.loadWav (file, 44100.0).has_value());

    file.deleteFile();
}

TEST_CASE ("WavLoader still loads a well-formed file", "[wav_loader][hardening]")
{
    constexpr int numSamples = 1000;

    const auto file = writeWav ("anap_hardening_valid.wav", 1u, 44100u,
                                (juce::uint32) (numSamples * 2), numSamples);

    WavLoader loader;
    const auto loaded = loader.loadWav (file, 44100.0);

    REQUIRE (loaded.has_value());
    REQUIRE (loaded->samples.size() == (size_t) numSamples);
    REQUIRE (loaded->numChannels == 1);

    file.deleteFile();
}

//==============================================================================
TEST_CASE ("MultiFilter comb delay follows its cutoff", "[multi_filter][comb][hardening]")
{
    // juce::dsp::DelayLine starts with a maximum delay of 0, i.e. a two-sample line,
    // and setDelay() clamps into that.  The comb maps cutoff to sampleRate/cutoff
    // samples, so without sizing the line every cutoff gave the same 2-sample
    // feedback loop and the cutoff control did nothing.
    const int lowCutoffEcho  = combEchoIndexForCutoff (1200.0);   // 40 samples
    const int highCutoffEcho = combEchoIndexForCutoff (4800.0);   // 10 samples

    REQUIRE (lowCutoffEcho > 0);
    REQUIRE (highCutoffEcho > 0);

    REQUIRE (std::abs (highCutoffEcho - 10) <= 2);
    REQUIRE (std::abs (lowCutoffEcho - 40) <= 2);
}

//==============================================================================
TEST_CASE ("MeteringEngine tolerates a block larger than it was prepared for",
           "[metering][hardening]")
{
    MeteringEngine meter;
    meter.prepare (48000.0, 2, 64);

    // A host may deliver a block larger than the announced maximum (offline bounce,
    // render-ahead).  Growing the interleave buffer there means allocating on the
    // audio thread, so the engine must clamp instead.
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, 0.5f * std::sin (0.01f * (float) i));

    meter.process (buffer);

    REQUIRE (std::isfinite (meter.getMomentaryLUFS()));
}
