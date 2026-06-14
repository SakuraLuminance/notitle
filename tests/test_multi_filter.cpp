#include <catch2/catch_all.hpp>
#include "../src/dsp/MultiFilter.h"
#include <cmath>

using namespace ana;

//==============================================================================
// Helpers
//==============================================================================

static constexpr double testSampleRate = 44100.0;

/** Creates a stereo sine-wave buffer for testing. */
static juce::AudioBuffer<float> makeSineBuffer(int numSamples,
                                                float freq = 440.0f,
                                                double sampleRate = testSampleRate)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    for (int s = 0; s < numSamples; ++s)
    {
        const float val = std::sin(2.0f * 3.14159265f * freq
                                   * static_cast<float>(s)
                                   / static_cast<float>(sampleRate));
        for (int ch = 0; ch < 2; ++ch)
            buf.setSample(ch, s, val);
    }
    return buf;
}

/** Creates a ProcessSpec for the given sample rate and block size. */
static juce::dsp::ProcessSpec makeSpec(double sampleRate = testSampleRate,
                                       int blockSize = 512)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(blockSize);
    spec.numChannels = 2;
    return spec;
}

/** Returns true if any sample in the buffer has non-zero magnitude. */
static bool hasAudio(const juce::AudioBuffer<float>& buf)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            if (std::abs(buf.getSample(ch, s)) > 0.0f)
                return true;
    return false;
}

//==============================================================================
// Slot management
//==============================================================================

TEST_CASE("MultiFilter - slot management", "[multifilter][slots]")
{
    MultiFilter filter;
    filter.prepare(makeSpec());

    SECTION("initially empty")
    {
        REQUIRE(filter.getNumSlots() == 0);
    }

    SECTION("addSlot returns consecutive indices")
    {
        REQUIRE(filter.addSlot(FilterType::LowPass) == 0);
        REQUIRE(filter.addSlot(FilterType::HighPass) == 1);
        REQUIRE(filter.getNumSlots() == 2);
    }

    SECTION("addSlot stores filter type and params")
    {
        FilterParams params;
        params.cutoff = 2000.0;
        params.resonance = 0.5f;
        params.drive = 0.3f;
        params.mix = 0.8f;

        const int idx = filter.addSlot(FilterType::BandPass, params);
        const auto& slot = filter.getSlot(idx);

        REQUIRE(slot.type == FilterType::BandPass);
        REQUIRE(slot.params.cutoff == Catch::Approx(2000.0));
        REQUIRE(slot.params.resonance == Catch::Approx(0.5f));
        REQUIRE(slot.params.drive == Catch::Approx(0.3f));
        REQUIRE(slot.params.mix == Catch::Approx(0.8f));
    }

    SECTION("removeSlot removes by index")
    {
        filter.addSlot(FilterType::LowPass);
        filter.addSlot(FilterType::HighPass);
        filter.addSlot(FilterType::BandPass);

        filter.removeSlot(1); // remove HighPass
        REQUIRE(filter.getNumSlots() == 2);
    }

    SECTION("removeSlot with invalid index is safe")
    {
        REQUIRE_NOTHROW(filter.removeSlot(0));  // empty
        filter.addSlot(FilterType::LowPass);
        REQUIRE_NOTHROW(filter.removeSlot(5));  // out of bounds
        REQUIRE(filter.getNumSlots() == 1);
    }

    SECTION("clearSlots removes all")
    {
        filter.addSlot(FilterType::LowPass);
        filter.addSlot(FilterType::HighPass);
        filter.clearSlots();
        REQUIRE(filter.getNumSlots() == 0);
    }
}

//==============================================================================
// Routing modes
//==============================================================================

TEST_CASE("MultiFilter - routing mode round-trip", "[multifilter][routing]")
{
    MultiFilter filter;

    SECTION("default is serial")
    {
        REQUIRE(filter.getRoutingMode() == RoutingMode::Serial);
    }

    SECTION("set and get all modes")
    {
        filter.setRoutingMode(RoutingMode::Parallel);
        REQUIRE(filter.getRoutingMode() == RoutingMode::Parallel);

        filter.setRoutingMode(RoutingMode::Split);
        REQUIRE(filter.getRoutingMode() == RoutingMode::Split);

        filter.setRoutingMode(RoutingMode::Serial);
        REQUIRE(filter.getRoutingMode() == RoutingMode::Serial);
    }
}

//==============================================================================
// Process serial
//==============================================================================

TEST_CASE("MultiFilter - process serial", "[multifilter][serial]")
{
    MultiFilter filter;
    filter.prepare(makeSpec());

    filter.addSlot(FilterType::LowPass, FilterParams{1000.0, 0.7f, 0.0f, 1.0f});
    filter.addSlot(FilterType::HighPass, FilterParams{5000.0, 0.7f, 0.0f, 1.0f});
    filter.setRoutingMode(RoutingMode::Serial);

    auto buffer = makeSineBuffer(256, 440.0f);
    REQUIRE_NOTHROW(filter.process(buffer));
    REQUIRE(hasAudio(buffer));
}

//==============================================================================
// Process parallel
//==============================================================================

TEST_CASE("MultiFilter - process parallel", "[multifilter][parallel]")
{
    MultiFilter filter;
    filter.prepare(makeSpec());

    filter.addSlot(FilterType::LowPass, FilterParams{1000.0, 0.7f, 0.0f, 0.5f});
    filter.addSlot(FilterType::HighPass, FilterParams{2000.0, 0.7f, 0.0f, 0.5f});
    filter.setRoutingMode(RoutingMode::Parallel);

    auto buffer = makeSineBuffer(256, 440.0f);
    REQUIRE_NOTHROW(filter.process(buffer));
    REQUIRE(hasAudio(buffer));
}

//==============================================================================
// Process split (crossover)
//==============================================================================

TEST_CASE("MultiFilter - process split", "[multifilter][split]")
{
    MultiFilter filter;
    filter.prepare(makeSpec());
    filter.setRoutingMode(RoutingMode::Split);

    filter.addSlot(FilterType::LowPass, FilterParams{500.0, 0.5f, 0.0f, 1.0f});
    filter.addSlot(FilterType::HighPass, FilterParams{3000.0, 0.5f, 0.0f, 1.0f});

    auto buffer = makeSineBuffer(256, 440.0f);
    REQUIRE_NOTHROW(filter.process(buffer));
    REQUIRE(hasAudio(buffer));
}

//==============================================================================
// Reset
//==============================================================================

TEST_CASE("MultiFilter - reset", "[multifilter][reset]")
{
    SECTION("reset empty filter")
    {
        MultiFilter filter;
        filter.prepare(makeSpec());
        REQUIRE_NOTHROW(filter.reset());
    }

    SECTION("reset after processing clears state")
    {
        MultiFilter filter;
        filter.prepare(makeSpec());

        filter.addSlot(FilterType::Comb, FilterParams{1000.0, 0.5f, 0.0f, 1.0f});
        filter.addSlot(FilterType::Formant, FilterParams{500.0, 0.5f, 0.0f, 1.0f});
        filter.setRoutingMode(RoutingMode::Serial);

        {
            auto buf = makeSineBuffer(512, 440.0f);
            filter.process(buf);
        }

        REQUIRE_NOTHROW(filter.reset());

        // Can still process after reset
        auto buf2 = makeSineBuffer(256, 440.0f);
        REQUIRE_NOTHROW(filter.process(buf2));
        REQUIRE(hasAudio(buf2));
    }
}

//==============================================================================
// Prepare with different specs
//==============================================================================

TEST_CASE("MultiFilter - prepare with different sample rates and block sizes",
          "[multifilter][prepare]")
{
    MultiFilter filter;

    SECTION("44.1 kHz, 64 samples")
    {
        filter.prepare(makeSpec(44100.0, 64));
        filter.addSlot(FilterType::LowPass);
        auto buf = makeSineBuffer(64, 440.0f, 44100.0);
        REQUIRE_NOTHROW(filter.process(buf));
    }

    SECTION("48 kHz, 256 samples")
    {
        filter.prepare(makeSpec(48000.0, 256));
        filter.addSlot(FilterType::HighPass);
        auto buf = makeSineBuffer(256, 440.0f, 48000.0);
        REQUIRE_NOTHROW(filter.process(buf));
    }

    SECTION("96 kHz, 1024 samples")
    {
        filter.prepare(makeSpec(96000.0, 1024));
        filter.addSlot(FilterType::BandPass);
        auto buf = makeSineBuffer(1024, 440.0f, 96000.0);
        REQUIRE_NOTHROW(filter.process(buf));
    }

    SECTION("prepare called twice re-initialises")
    {
        filter.prepare(makeSpec(44100.0, 128));
        filter.addSlot(FilterType::LowPass);
        filter.prepare(makeSpec(48000.0, 256));
        auto buf = makeSineBuffer(256, 440.0f, 48000.0);
        REQUIRE_NOTHROW(filter.process(buf));
        REQUIRE(hasAudio(buf));
    }
}

//==============================================================================
// Coefficient caching
//==============================================================================

TEST_CASE("MultiFilter - coefficient caching", "[multifilter][caching]")
{
    MultiFilter filter;
    filter.prepare(makeSpec());

    const int idx = filter.addSlot(FilterType::LowPass, FilterParams{1000.0, 0.5f, 0.0f, 1.0f});

    SECTION("new slot starts dirty")
    {
        REQUIRE(filter.getSlot(idx).coefficientsDirty);
    }

    SECTION("coefficients cleared after process")
    {
        auto buf = makeSineBuffer(128, 440.0f);
        filter.process(buf);
        REQUIRE_FALSE(filter.getSlot(idx).coefficientsDirty);
    }

    SECTION("markCoefficientsDirty re-dirties")
    {
        auto buf = makeSineBuffer(128, 440.0f);
        filter.process(buf);
        REQUIRE_FALSE(filter.getSlot(idx).coefficientsDirty);

        filter.markCoefficientsDirty();
        REQUIRE(filter.getSlot(idx).coefficientsDirty);
    }
}

//==============================================================================
// Frequency response
//==============================================================================

TEST_CASE("MultiFilter - frequency response", "[multifilter][freqresp]")
{
    MultiFilter filter;
    filter.prepare(makeSpec());

    SECTION("returns correct size matching input frequencies")
    {
        filter.addSlot(FilterType::LowPass, FilterParams{1000.0, 0.7f, 0.0f, 1.0f});

        const std::vector<float> frequencies = {100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f};
        const auto response = filter.getFrequencyResponse(frequencies);

        REQUIRE(response.size() == frequencies.size());
    }

    SECTION("all values are non-negative (linear magnitude)")
    {
        filter.addSlot(FilterType::LowPass, FilterParams{1000.0, 0.7f, 0.0f, 1.0f});

        const std::vector<float> frequencies = {100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f};
        const auto response = filter.getFrequencyResponse(frequencies);

        for (const auto r : response)
            REQUIRE(r >= 0.0f);
    }

    SECTION("empty filter returns correct size")
    {
        const std::vector<float> frequencies = {100.0f, 1000.0f, 10000.0f};
        const auto response = filter.getFrequencyResponse(frequencies);

        REQUIRE(response.size() == frequencies.size());
    }
}

//==============================================================================
// Edge cases
//==============================================================================

TEST_CASE("MultiFilter - edge cases", "[multifilter][edge]")
{
    SECTION("process empty buffer")
    {
        MultiFilter filter;
        filter.prepare(makeSpec());
        filter.addSlot(FilterType::LowPass);

        auto buf = juce::AudioBuffer<float>(2, 0);
        REQUIRE_NOTHROW(filter.process(buf));
    }

    SECTION("process with different filter types")
    {
        MultiFilter filter;
        filter.prepare(makeSpec());
        filter.setRoutingMode(RoutingMode::Serial);

        filter.addSlot(FilterType::AllPass, FilterParams{2000.0, 0.5f, 0.0f, 1.0f});
        filter.addSlot(FilterType::Comb, FilterParams{1000.0, 0.0f, 0.0f, 1.0f});
        filter.addSlot(FilterType::Formant, FilterParams{500.0, 0.5f, 0.0f, 1.0f});
        filter.addSlot(FilterType::Morph, FilterParams{1000.0, 0.5f, 0.0f, 1.0f});

        auto buf = makeSineBuffer(128, 440.0f);
        REQUIRE_NOTHROW(filter.process(buf));
        REQUIRE(hasAudio(buf));
    }

    SECTION("bypassed slot")
    {
        MultiFilter filter;
        filter.prepare(makeSpec());

        const int idx = filter.addSlot(FilterType::LowPass,
                                        FilterParams{100.0, 0.0f, 0.0f, 1.0f});
        filter.getSlot(idx).bypassed = true;

        auto buf = makeSineBuffer(256, 440.0f);
        REQUIRE_NOTHROW(filter.process(buf));
        REQUIRE(hasAudio(buf));
    }

    SECTION("master gain round-trip")
    {
        MultiFilter filter;
        filter.setMasterGain(0.5f);
        REQUIRE(filter.getMasterGain() == Catch::Approx(0.5f));

        filter.setMasterGain(2.0f);
        REQUIRE(filter.getMasterGain() == Catch::Approx(2.0f));
    }

    SECTION("process produces no NaN or Inf")
    {
        MultiFilter filter;
        filter.prepare(makeSpec());

        filter.addSlot(FilterType::LowPass, FilterParams{500.0, 0.9f, 0.0f, 1.0f});
        filter.addSlot(FilterType::HighPass, FilterParams{4000.0, 0.9f, 0.0f, 1.0f});
        filter.addSlot(FilterType::Comb, FilterParams{1000.0, 0.7f, 0.0f, 1.0f});
        filter.setRoutingMode(RoutingMode::Serial);

        auto buf = makeSineBuffer(1024, 440.0f);
        filter.process(buf);

        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int s = 0; s < buf.getNumSamples(); ++s)
            {
                const float sample = buf.getSample(ch, s);
                REQUIRE_FALSE(std::isnan(sample));
                REQUIRE_FALSE(std::isinf(sample));
            }
    }
}
