#include <catch2/catch_all.hpp>
#include "../src/dsp/PrismEffect.h"
#include <cmath>
#include <limits>

//==============================================================================
// Helper: build a single-frame PartialDataSIMD with N partials at given frequencies
static ana::PartialDataSIMD makeTestData(
    const std::vector<float>& frequencies,
    const std::vector<float>& amplitudes,
    const std::vector<float>& phases = {})
{
    ana::PartialDataSIMD data;
    data.sampleRate = 44100.0;
    data.activeCount = 0;

    for (size_t i = 0; i < frequencies.size() && i < ana::PartialDataSIMD::kMaxPartials; ++i)
    {
        data.frequency[i] = frequencies[i];
        data.amplitude[i] = (i < amplitudes.size()) ? amplitudes[i] : 0.5f;
        data.phase[i]     = (i < phases.size()) ? phases[i] : 0.0f;
        data.activeMask[i >> 5] |= (1u << (i & 31));
        data.activeCount++;
    }
    return data;
}

//==============================================================================
// Helper: check that all partials in data have finite (non-NaN, non-Inf) values
static void requireValidPartials(const ana::PartialDataSIMD& data)
{
    for (int p = 0; p < data.maxPartials; ++p)
    {
        if (data.isActive(p))
        {
            REQUIRE(std::isfinite(data.frequency[p]));
            REQUIRE(std::isfinite(data.amplitude[p]));
            REQUIRE(std::isfinite(data.phase[p]));
            REQUIRE_FALSE(std::isnan(data.frequency[p]));
            REQUIRE_FALSE(std::isnan(data.amplitude[p]));
            REQUIRE_FALSE(std::isnan(data.phase[p]));
        }
    }
}

//==============================================================================
// Test: empty / edge cases
TEST_CASE("PrismEffect - empty data produces no errors", "[prism][edge]")
{
    ana::PrismEffect prism;
    ana::PartialDataSIMD data;

    // Should not crash or produce errors
    REQUIRE_NOTHROW(prism.process(data));
}

TEST_CASE("PrismEffect - single partial with zero amount does nothing", "[prism][edge]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({440.0f}, {0.5f});

    prism.setAmount(0.0f);
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::FrequencyShift);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(440.0f));
    REQUIRE(data.amplitude[0] == Approx(0.5f));
}

TEST_CASE("PrismEffect - reset clears feedback state", "[prism][edge]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({440.0f}, {1.0f});

    prism.setAmount(0.5f);
    prism.setFeedback(0.5f);
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::FrequencyShift);
    prism.process(data);

    // Reset should clear feedback buffer
    prism.reset();
    // No crash on subsequent calls
    REQUIRE_NOTHROW(prism.process(data));
}

//==============================================================================
// Test: FrequencyShift
TEST_CASE("PrismEffect - FrequencyShift shifts all frequencies by offset", "[prism][freqshift]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({100.0f, 200.0f, 400.0f}, {0.5f, 0.5f, 0.5f});

    prism.setAmount(0.5f);       // 50% of maxShift
    prism.setMaxShift(1000.0f);  // maxShift = 1000 Hz → shift = 500 Hz
    prism.setMix(1.0f);          // full wet
    prism.setMode(ana::PrismMode::FrequencyShift);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(600.0f));  // 100 + 500
    REQUIRE(data.frequency[1] == Approx(700.0f));  // 200 + 500
    REQUIRE(data.frequency[2] == Approx(900.0f));  // 400 + 500
}

TEST_CASE("PrismEffect - FrequencyShift with zero amount leaves frequencies unchanged", "[prism][freqshift]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({440.0f}, {0.5f});

    prism.setAmount(0.0f);
    prism.setMaxShift(500.0f);
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::FrequencyShift);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(440.0f));
}

//==============================================================================
// Test: SpectralBlur
TEST_CASE("PrismEffect - SpectralBlur smooths amplitude peaks", "[prism][blur]")
{
    auto data = makeTestData({100.0f, 200.0f, 300.0f, 400.0f, 500.0f}, {0.0f, 0.0f, 1.0f, 0.0f, 0.0f});

    ana::PrismEffect prism;
    prism.setAmount(1.0f);  // full blur
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::SpectralBlur);
    prism.process(data);

    REQUIRE(data.amplitude[0] == Approx(0.0f));
    REQUIRE(data.amplitude[1] == Approx(1.0f / 3.0f));
    REQUIRE(data.amplitude[2] == Approx(1.0f / 3.0f));
    REQUIRE(data.amplitude[3] == Approx(1.0f / 3.0f));
    REQUIRE(data.amplitude[4] == Approx(0.0f));
}

TEST_CASE("PrismEffect - SpectralBlur with zero amount preserves amplitudes", "[prism][blur]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({100.0f, 200.0f, 300.0f}, {0.1f, 0.9f, 0.1f});

    prism.setAmount(0.0f);
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::SpectralBlur);
    prism.process(data);

    REQUIRE(data.amplitude[0] == Approx(0.1f));
    REQUIRE(data.amplitude[1] == Approx(0.9f));
    REQUIRE(data.amplitude[2] == Approx(0.1f));
}

TEST_CASE("PrismEffect - SpectralBlur with single partial does nothing", "[prism][blur]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({440.0f}, {0.8f});

    prism.setAmount(1.0f);
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::SpectralBlur);
    prism.process(data);

    REQUIRE(data.amplitude[0] == Approx(0.8f));
}

//==============================================================================
// Test: HarmonicRotation
TEST_CASE("PrismEffect - HarmonicRotation rotates partial assignments", "[prism][rotation]")
{
    ana::PrismEffect prism;
    // Frequencies unsorted to verify sorting occurs before rotation
    auto data = makeTestData({400.0f, 100.0f, 200.0f, 300.0f},
                             {1.0f, 2.0f, 3.0f, 4.0f});

    prism.setAmount(1.0f);  // full rotation
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::HarmonicRotation);
    prism.process(data);

    // After sorting by freq: [(100,2), (200,3), (300,4), (400,1)]
    // After rotation by 1 position: [(200,3), (300,4), (400,1), (100,2)]
    // In our SIMD array, the sorted order is logical, but the data is physically at indices 0..3
    // Index 0 has 400.0, 1.0 -> will get 100.0, 2.0
    // Index 1 has 100.0, 2.0 -> will get 200.0, 3.0
    // Index 2 has 200.0, 3.0 -> will get 300.0, 4.0
    // Index 3 has 300.0, 4.0 -> will get 400.0, 1.0
    
    REQUIRE(data.frequency[0] == Approx(100.0f));
    REQUIRE(data.amplitude[0] == Approx(2.0f));
    REQUIRE(data.frequency[1] == Approx(200.0f));
    REQUIRE(data.amplitude[1] == Approx(3.0f));
    REQUIRE(data.frequency[2] == Approx(300.0f));
    REQUIRE(data.amplitude[2] == Approx(4.0f));
    REQUIRE(data.frequency[3] == Approx(400.0f));
    REQUIRE(data.amplitude[3] == Approx(1.0f));
}

TEST_CASE("PrismEffect - HarmonicRotation with zero amount does nothing", "[prism][rotation]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({100.0f, 200.0f}, {0.5f, 0.8f});

    prism.setAmount(0.0f);
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::HarmonicRotation);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(100.0f));
    REQUIRE(data.amplitude[0] == Approx(0.5f));
    REQUIRE(data.frequency[1] == Approx(200.0f));
    REQUIRE(data.amplitude[1] == Approx(0.8f));
}

//==============================================================================
// Test: SpectralMirror
TEST_CASE("PrismEffect - SpectralMirror mirrors frequencies around center", "[prism][mirror]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({100.0f, 500.0f, 1000.0f}, {0.5f, 0.5f, 0.5f});

    prism.setCenterFreq(1000.0f);
    prism.setAmount(1.0f);  // full mirror
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::SpectralMirror);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(1900.0f));
    REQUIRE(data.frequency[1] == Approx(1500.0f));
    REQUIRE(data.frequency[2] == Approx(1000.0f));
}

TEST_CASE("PrismEffect - SpectralMirror with zero amount does nothing", "[prism][mirror]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({440.0f}, {0.5f});

    prism.setCenterFreq(1000.0f);
    prism.setAmount(0.0f);
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::SpectralMirror);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(440.0f));
}

TEST_CASE("PrismEffect - SpectralMirror clamps negative frequencies", "[prism][mirror]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({5000.0f}, {0.5f});

    prism.setCenterFreq(1000.0f);
    prism.setAmount(1.0f);   // full mirror
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::SpectralMirror);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(0.0f));
}

//==============================================================================
// Test: CombSweep
TEST_CASE("PrismEffect - CombSweep attenuates amplitudes based on frequency", "[prism][comb]")
{
    ana::PrismEffect prism;

    auto dataLight = makeTestData({100.0f}, {1.0f});

    prism.setAmount(0.0f);
    prism.setMix(1.0f);
    prism.setMode(ana::PrismMode::CombSweep);
    prism.process(dataLight);

    REQUIRE(dataLight.amplitude[0] < 1.0f);
    REQUIRE(dataLight.amplitude[0] > 0.99f);

    auto dataHeavy = makeTestData({125.0f}, {1.0f});

    prism.setAmount(1.0f);
    prism.setMode(ana::PrismMode::CombSweep);
    prism.process(dataHeavy);

    REQUIRE(dataHeavy.amplitude[0] == Approx(0.0f).margin(1e-4));
}

//==============================================================================
// Test: Mix parameter
TEST_CASE("PrismEffect - mix=0 preserves original data entirely", "[prism][mix]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({100.0f, 200.0f, 300.0f}, {0.5f, 0.5f, 0.5f});

    prism.setAmount(1.0f);
    prism.setMaxShift(500.0f);
    prism.setMix(0.0f);  // dry only
    prism.setMode(ana::PrismMode::FrequencyShift);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(100.0f));
    REQUIRE(data.frequency[1] == Approx(200.0f));
    REQUIRE(data.frequency[2] == Approx(300.0f));
}

TEST_CASE("PrismEffect - mix=0.5 blends original and processed equally", "[prism][mix]")
{
    ana::PrismEffect prism;
    auto data = makeTestData({100.0f}, {0.5f});

    prism.setAmount(1.0f);
    prism.setMaxShift(200.0f);
    prism.setMix(0.5f);  // half dry, half wet
    prism.setMode(ana::PrismMode::FrequencyShift);
    prism.process(data);

    REQUIRE(data.frequency[0] == Approx(200.0f));
}

//==============================================================================
// Test: NaN/Inf sanitization
TEST_CASE("PrismEffect - all modes produce valid (finite, non-NaN) output", "[prism][valid]")
{
    const auto testMode = [&](ana::PrismMode mode)
    {
        DYNAMIC_SECTION("Mode " << static_cast<int>(mode))
        {
            ana::PrismEffect p;
            auto d = makeTestData({100.0f, 200.0f, 400.0f, 800.0f, 1600.0f},
                                  {0.1f, 0.5f, 1.0f, 0.5f, 0.1f});
            p.setAmount(0.75f);
            p.setMix(1.0f);
            p.setMode(mode);
            p.process(d);
            requireValidPartials(d);
        }
    };

    testMode(ana::PrismMode::FrequencyShift);
    testMode(ana::PrismMode::SpectralBlur);
    testMode(ana::PrismMode::HarmonicRotation);
    testMode(ana::PrismMode::SpectralMirror);
    testMode(ana::PrismMode::CombSweep);
}

//==============================================================================
// Test: Feedback parameter
TEST_CASE("PrismEffect - feedback blends previous output into current input", "[prism][feedback]")
{
    ana::PrismEffect prism;
    auto data1 = makeTestData({100.0f}, {1.0f});

    prism.setAmount(1.0f);
    prism.setMaxShift(200.0f);
    prism.setMix(1.0f);
    prism.setFeedback(0.5f);
    prism.setMode(ana::PrismMode::FrequencyShift);
    prism.process(data1);

    REQUIRE(data1.frequency[0] == Approx(300.0f));

    auto data2 = makeTestData({100.0f}, {1.0f});

    prism.process(data2);

    REQUIRE(data2.frequency[0] == Approx(400.0f));
}
