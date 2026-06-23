#include <catch2/catch_all.hpp>
#include <cmath>

#include "dsp/SpectralMorpher.h"

using namespace ana;

//==============================================================================
// Helper: fill a PartialDataSIMD with a known pattern
//==============================================================================
static void fillPattern(PartialDataSIMD& data,
                        float freqBase,
                        float ampBase,
                        float phaseBase) noexcept
{
    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        data.frequency[i] = freqBase + static_cast<float>(i) * 10.0f;
        data.amplitude[i] = ampBase * (1.0f - static_cast<float>(i)
                           / static_cast<float>(PartialDataSIMD::kMaxPartials));
        data.phase[i]     = phaseBase + static_cast<float>(i) * 0.1f;
    }
    data.updateActiveMask();
}

//==============================================================================
// morphLinear
//==============================================================================
TEST_CASE("SpectralMorpher::morphLinear - t=0 yields pure A", "[morpher][linear]")
{
    PartialDataSIMD a, b, out;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    SpectralMorpher::morphLinear(out, a, b, 0.0f);

    REQUIRE(out.activeCount == a.activeCount);
    REQUIRE(out.sampleRate == a.sampleRate);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        CHECK(out.frequency[i] == Catch::Approx(a.frequency[i]));
        CHECK(out.amplitude[i] == Catch::Approx(a.amplitude[i]));
        CHECK(out.phase[i]     == Catch::Approx(a.phase[i]));
    }
}

TEST_CASE("SpectralMorpher::morphLinear - t=1 yields pure B", "[morpher][linear]")
{
    PartialDataSIMD a, b, out;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    SpectralMorpher::morphLinear(out, a, b, 1.0f);

    REQUIRE(out.activeCount == b.activeCount);
    REQUIRE(out.sampleRate == a.sampleRate);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        CHECK(out.frequency[i] == Catch::Approx(b.frequency[i]));
        CHECK(out.amplitude[i] == Catch::Approx(b.amplitude[i]));
        CHECK(out.phase[i]     == Catch::Approx(b.phase[i]));
    }
}

TEST_CASE("SpectralMorpher::morphLinear - t=0.5 yields midpoint",
          "[morpher][linear]")
{
    PartialDataSIMD a, b, out;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    SpectralMorpher::morphLinear(out, a, b, 0.5f);

    REQUIRE(out.activeCount > 0);
    REQUIRE(out.sampleRate == a.sampleRate);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        const float expectedFreq = a.frequency[i] * 0.5f + b.frequency[i] * 0.5f;
        const float expectedAmp  = a.amplitude[i] * 0.5f + b.amplitude[i] * 0.5f;
        const float expectedPhase = a.phase[i] * 0.5f + b.phase[i] * 0.5f;

        CHECK(out.frequency[i] == Catch::Approx(expectedFreq));
        CHECK(out.amplitude[i] == Catch::Approx(expectedAmp));
        CHECK(out.phase[i]     == Catch::Approx(expectedPhase));
    }
}

TEST_CASE("SpectralMorpher::morphLinear - clamps t to [0,1]", "[morpher][linear]")
{
    PartialDataSIMD a, b, out, outClamped;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    // t=1.5 should behave like t=1.0
    SpectralMorpher::morphLinear(out, a, b, 1.5f);
    SpectralMorpher::morphLinear(outClamped, a, b, 1.0f);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        CHECK(out.frequency[i] == Catch::Approx(outClamped.frequency[i]));
        CHECK(out.amplitude[i] == Catch::Approx(outClamped.amplitude[i]));
        CHECK(out.phase[i]     == Catch::Approx(outClamped.phase[i]));
    }
}

//==============================================================================
// morphWeighted
//==============================================================================
TEST_CASE("SpectralMorpher::morphWeighted - differs from linear",
          "[morpher][weighted]")
{
    PartialDataSIMD a, b, linearOut, weightedOut;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    // With non-uniform amplitudes, weighted should differ from linear
    SpectralMorpher::morphLinear(linearOut, a, b, 0.5f);
    SpectralMorpher::morphWeighted(weightedOut, a, b, 0.5f);

    // At least some partials should differ
    bool anyDiffers = false;
    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (std::abs(linearOut.frequency[i] - weightedOut.frequency[i]) > 1e-6f
            || std::abs(linearOut.amplitude[i] - weightedOut.amplitude[i]) > 1e-6f
            || std::abs(linearOut.phase[i] - weightedOut.phase[i]) > 1e-6f)
        {
            anyDiffers = true;
            break;
        }
    }
    REQUIRE(anyDiffers);
}

TEST_CASE("SpectralMorpher::morphWeighted - t=0 yields pure A",
          "[morpher][weighted]")
{
    PartialDataSIMD a, b, out;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    SpectralMorpher::morphWeighted(out, a, b, 0.0f);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        CHECK(out.frequency[i] == Catch::Approx(a.frequency[i]));
        CHECK(out.amplitude[i] == Catch::Approx(a.amplitude[i]));
    }
}

TEST_CASE("SpectralMorpher::morphWeighted - t=1 yields pure B",
          "[morpher][weighted]")
{
    PartialDataSIMD a, b, out;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    SpectralMorpher::morphWeighted(out, a, b, 1.0f);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        CHECK(out.frequency[i] == Catch::Approx(b.frequency[i]));
        CHECK(out.amplitude[i] == Catch::Approx(b.amplitude[i]));
    }
}

TEST_CASE("SpectralMorpher::morphWeighted - higher amplitude partials morph faster",
          "[morpher][weighted]")
{
    PartialDataSIMD a, b, out1, out2;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    // Force partial 0 to have low amplitude in both A and B
    a.amplitude[0]  = 0.01f;
    b.amplitude[0]  = 0.01f;
    // Force partial 1 to have high amplitude
    a.amplitude[1]  = 0.9f;
    b.amplitude[1]  = 0.9f;
    a.updateActiveMask();
    b.updateActiveMask();

    SpectralMorpher::morphWeighted(out1, a, b, 0.3f);
    SpectralMorpher::morphWeighted(out2, a, b, 0.6f);

    // Low-amp partial should morph slower (closer to A at t=0.3)
    // High-amp partial should morph faster (closer to B at t=0.3)
    // So at t=0.3, partial 0 freq should be closer to a, partial 1 closer to b
    const float distLowA  = std::abs(out1.frequency[0] - a.frequency[0]);
    const float distLowB  = std::abs(out1.frequency[0] - b.frequency[0]);
    const float distHighA = std::abs(out1.frequency[1] - a.frequency[1]);
    const float distHighB = std::abs(out1.frequency[1] - b.frequency[1]);

    // Low-amp partial should be closer to A than to B
    CHECK(distLowA < distLowB);
    // High-amp partial should be closer to B than to A
    CHECK(distHighB < distHighA);
}

//==============================================================================
// morphMulti
//==============================================================================
TEST_CASE("SpectralMorpher::morphMulti - single source yields source copy",
          "[morpher][multi]")
{
    PartialDataSIMD src, out;
    fillPattern(src, 440.0f, 0.7f, 0.5f);

    std::vector<PartialDataSIMD> sources = { src };
    std::vector<float> weights = { 1.0f };

    SpectralMorpher::morphMulti(out, sources, weights);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        CHECK(out.frequency[i] == Catch::Approx(src.frequency[i]));
        CHECK(out.amplitude[i] == Catch::Approx(src.amplitude[i]));
    }
    REQUIRE(out.activeCount == src.activeCount);
    REQUIRE(out.sampleRate == src.sampleRate);
}

TEST_CASE("SpectralMorpher::morphMulti - three sources weighted blend",
          "[morpher][multi]")
{
    PartialDataSIMD s1, s2, s3, out;
    fillPattern(s1, 100.0f, 0.5f, 0.0f);
    fillPattern(s2, 200.0f, 0.3f, 0.5f);
    fillPattern(s3, 300.0f, 0.2f, 1.0f);

    std::vector<PartialDataSIMD> sources = { s1, s2, s3 };
    std::vector<float> weights = { 0.5f, 0.3f, 0.2f };

    SpectralMorpher::morphMulti(out, sources, weights);

    REQUIRE(out.activeCount > 0);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        const float expectedFreq = 0.5f * s1.frequency[i]
                                 + 0.3f * s2.frequency[i]
                                 + 0.2f * s3.frequency[i];
        const float expectedAmp  = 0.5f * s1.amplitude[i]
                                 + 0.3f * s2.amplitude[i]
                                 + 0.2f * s3.amplitude[i];
        const float expectedPhase = 0.5f * s1.phase[i]
                                  + 0.3f * s2.phase[i]
                                  + 0.2f * s3.phase[i];

        CHECK(out.frequency[i] == Catch::Approx(expectedFreq));
        CHECK(out.amplitude[i] == Catch::Approx(expectedAmp));
        CHECK(out.phase[i]     == Catch::Approx(expectedPhase));
    }
}

TEST_CASE("SpectralMorpher::morphMulti - weights sum > 1.0 are normalized",
          "[morpher][multi]")
{
    PartialDataSIMD s1, s2, out, outNorm;
    fillPattern(s1, 100.0f, 0.5f, 0.0f);
    fillPattern(s2, 200.0f, 0.3f, 0.5f);

    // Weights that sum > 1.0
    std::vector<PartialDataSIMD> sources = { s1, s2 };
    std::vector<float> weights = { 1.0f, 1.0f }; // sum = 2.0, should be normalized

    SpectralMorpher::morphMulti(out, sources, weights);

    // With sum=2.0, effective weights are 0.5, 0.5
    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        const float expectedFreq = 0.5f * s1.frequency[i] + 0.5f * s2.frequency[i];
        const float expectedAmp  = 0.5f * s1.amplitude[i] + 0.5f * s2.amplitude[i];
        CHECK(out.frequency[i] == Catch::Approx(expectedFreq));
        CHECK(out.amplitude[i] == Catch::Approx(expectedAmp));
    }
}

TEST_CASE("SpectralMorpher::morphMulti - empty sources yields zeroed output",
          "[morpher][multi]")
{
    PartialDataSIMD out;
    fillPattern(out, 999.0f, 0.9f, 2.0f); // fill with non-zero

    std::vector<PartialDataSIMD> sources = {};
    std::vector<float> weights = {};

    SpectralMorpher::morphMulti(out, sources, weights);

    REQUIRE(out.activeCount == 0);
    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        CHECK(out.frequency[i] == Catch::Approx(0.0f));
        CHECK(out.amplitude[i] == Catch::Approx(0.0f));
        CHECK(out.phase[i]     == Catch::Approx(0.0f));
    }
}

//==============================================================================
// Edge cases / safety
//==============================================================================
TEST_CASE("SpectralMorpher - NaN inputs are sanitized", "[morpher][safety]")
{
    PartialDataSIMD a, b, out;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    // Inject NaN into a
    a.frequency[10] = std::nanf("");
    a.amplitude[20] = std::nanf("");
    a.phase[30]     = std::nanf("");

    // Should not crash or propagate NaN
    SpectralMorpher::morphLinear(out, a, b, 0.5f);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        REQUIRE(std::isfinite(out.frequency[i]));
        REQUIRE(std::isfinite(out.amplitude[i]));
        REQUIRE(std::isfinite(out.phase[i]));
    }
}

TEST_CASE("SpectralMorpher - Inf inputs are sanitized", "[morpher][safety]")
{
    PartialDataSIMD a, b, out;
    fillPattern(a, 100.0f, 0.8f, 0.0f);
    fillPattern(b, 400.0f, 0.4f, 1.0f);

    // Inject Inf into b
    b.amplitude[5] = std::numeric_limits<float>::infinity();

    // Should not crash
    SpectralMorpher::morphWeighted(out, a, b, 0.5f);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        REQUIRE(std::isfinite(out.frequency[i]));
        REQUIRE(std::isfinite(out.amplitude[i]));
        REQUIRE(std::isfinite(out.phase[i]));
    }
}
