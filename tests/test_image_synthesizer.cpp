#include <catch2/catch_all.hpp>
#include "dsp/ImageSynthesizer.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers — create simple test images
// ---------------------------------------------------------------------------

static juce::Image makeImage(int w, int h, float r, float g, float b, float a = 1.0f)
{
    juce::Image img(juce::Image::ARGB, w, h, true);
    img.clear(img.getBounds(), juce::Colour::fromFloatRGBA(r, g, b, a));
    return img;
}

static void fillPixel(juce::Image& img, int x, int y, float r, float g, float b, float a = 1.0f)
{
    img.setPixelAt(x, y, juce::Colour::fromFloatRGBA(r, g, b, a));
}

// ============================================================================
// Default State
// ============================================================================

TEST_CASE("ImageSynthesizer - default state returns empty data", "[image]")
{
    const ana::ImageSynthesizer synth;

    REQUIRE(synth.getWidth()      == 0);
    REQUIRE(synth.getHeight()     == 0);
    REQUIRE(synth.getNumFrames()  == 0);
    REQUIRE(synth.getNumPartials() == 0);

    const auto data = synth.getPartialData();
    REQUIRE(data.frames.empty());
}

// ============================================================================
// Image Dimensions
// ============================================================================

TEST_CASE("ImageSynthesizer - dimensions after setGainPlane", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setGainPlane(makeImage(100, 50, 0.5f, 0.5f, 0.5f));

    REQUIRE(synth.getWidth()      == 100);
    REQUIRE(synth.getHeight()     == 50);
    REQUIRE(synth.getNumFrames()  == 100);
    REQUIRE(synth.getNumPartials() == 50);
}

TEST_CASE("ImageSynthesizer - zero-size image produces no frames", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setGainPlane(juce::Image(juce::Image::ARGB, 0, 0, true));
    REQUIRE(synth.getWidth()  == 0);
    REQUIRE(synth.getHeight() == 0);

    const auto data = synth.getPartialData();
    REQUIRE(data.frames.empty());
}

TEST_CASE("ImageSynthesizer - null image is rejected", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setGainPlane(juce::Image{});
    REQUIRE(synth.getWidth() == 0);
}

// ============================================================================
// Partial Extraction — count and structure
// ============================================================================

TEST_CASE("ImageSynthesizer - correct number of frames and partials", "[image]")
{
    ana::ImageSynthesizer synth;
    // 4 columns × 3 rows, uniform bright grey → all pixels pass threshold
    synth.setGainPlane(makeImage(4, 3, 0.8f, 0.8f, 0.8f));

    const auto data = synth.getPartialData();
    REQUIRE(data.frames.size() == 4);           // width

    for (const auto& frame : data.frames)
        REQUIRE(frame.partials.size() == 3);    // height (all non-zero)
}

// ============================================================================
// Amplitude Mapping  (brightness → amplitude)
// ============================================================================

TEST_CASE("ImageSynthesizer - amplitude from uniform white image", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setGainPlane(makeImage(3, 3, 1.0f, 1.0f, 1.0f));

    const auto data = synth.getPartialData();
    REQUIRE(data.frames.size() == 3);

    for (const auto& frame : data.frames)
        for (const auto& p : frame.partials)
            // White → brightness ≈ 1.0  (within 8‑bit quantisation)
            REQUIRE(p.amplitude == Catch::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("ImageSynthesizer - amplitude from uniform 50% gray image", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setGainPlane(makeImage(2, 2, 0.5f, 0.5f, 0.5f));

    const auto data = synth.getPartialData();

    for (const auto& frame : data.frames)
        for (const auto& p : frame.partials)
            // 0.5 → stored as 8‑bit (127 or 128) → float ~0.498-0.502
            REQUIRE(p.amplitude == Catch::Approx(0.5f).epsilon(0.02f));
}

TEST_CASE("ImageSynthesizer - black pixels are skipped", "[image]")
{
    ana::ImageSynthesizer synth;

    // 2×2: top row black, bottom row white
    auto img = makeImage(2, 2, 0.0f, 0.0f, 0.0f);
    fillPixel(img, 0, 1, 1.0f, 1.0f, 1.0f);
    fillPixel(img, 1, 1, 1.0f, 1.0f, 1.0f);
    synth.setGainPlane(img);

    const auto data = synth.getPartialData();

    // Each column: y=0 (black) skipped, y=1 (white) included → 1 partial/frame
    for (const auto& frame : data.frames)
        REQUIRE(frame.partials.size() == 1);
}

// ============================================================================
// Frequency Mapping  (Y axis → frequency, logarithmic)
// ============================================================================

TEST_CASE("ImageSynthesizer - frequency increases from bottom to top", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setFrequencyRange(100.0f, 10000.0f);

    // 1 column × 3 rows, all white
    auto img = makeImage(1, 3, 1.0f, 1.0f, 1.0f);
    synth.setGainPlane(img);

    const auto data = synth.getPartialData();
    REQUIRE(data.frames.size() == 1);
    REQUIRE(data.frames[0].partials.size() == 3);

    const auto& p = data.frames[0].partials;

    // partials[0] = y=0 (top)    → normalisedY=1.0 → maxFreq  (10000)
    // partials[1] = y=1 (middle) → normalisedY=0.5 → midFreq  (1000)
    // partials[2] = y=2 (bottom) → normalisedY=0.0 → minFreq  (100)
    REQUIRE(p[0].frequency > p[1].frequency);
    REQUIRE(p[1].frequency > p[2].frequency);

    REQUIRE(p[2].frequency == Catch::Approx(100.0f).margin(0.5f));
    REQUIRE(p[0].frequency == Catch::Approx(10000.0f).margin(0.5f));
}

// ============================================================================
// Pitch Plane  (brightness → frequency deviation)
// ============================================================================

TEST_CASE("ImageSynthesizer - pitch plane shifts frequency upward", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setFrequencyRange(20.0f, 20000.0f);
    synth.setPitchDeviationRange(12.0f);  // ±12 semitones

    // 1×2 gain plane, all white
    synth.setGainPlane(makeImage(1, 2, 1.0f, 1.0f, 1.0f));

    // 1×2 pitch plane, all white → brightness ≈ 1.0 → deviation = +12 st
    synth.setPitchPlane(makeImage(1, 2, 1.0f, 1.0f, 1.0f));

    const auto data = synth.getPartialData();
    REQUIRE(data.frames.size() == 1);
    REQUIRE(data.frames[0].partials.size() == 2);

    // Bottom pixel (y=1, partials[1]): baseFreq = 20 Hz
    //   deviation = (1.0 - 0.5)*2*12 = +12 st   →  20 * 2^(12/12) = 40 Hz
    const float expected = 20.0f * std::pow(2.0f, 12.0f / 12.0f);
    REQUIRE(data.frames[0].partials[1].frequency == Catch::Approx(expected).epsilon(0.01f));
}

TEST_CASE("ImageSynthesizer - pitch plane shifts frequency downward", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setFrequencyRange(20.0f, 20000.0f);
    synth.setPitchDeviationRange(12.0f);

    synth.setGainPlane(makeImage(1, 2, 1.0f, 1.0f, 1.0f));

    // Pitch plane all black → brightness ≈ 0.0 → deviation = -12 st
    synth.setPitchPlane(makeImage(1, 2, 0.0f, 0.0f, 0.0f));

    const auto data = synth.getPartialData();
    REQUIRE(data.frames.size() == 1);

    // Bottom pixel (y=1): baseFreq = 20 Hz
    //   deviation = (0.0 - 0.5)*2*12 = -12 st  →  20 / 2^(12/12) = 10 Hz
    const float expected = 20.0f / std::pow(2.0f, 12.0f / 12.0f);
    REQUIRE(data.frames[0].partials[1].frequency == Catch::Approx(expected).epsilon(0.01f));
}

TEST_CASE("ImageSynthesizer - gray pitch plane causes negligible shift", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setFrequencyRange(20.0f, 20000.0f);
    synth.setPitchDeviationRange(12.0f);

    synth.setGainPlane(makeImage(1, 2, 1.0f, 1.0f, 1.0f));

    // Pitch plane uniform 50 % grey → brightness ≈ 0.5 → deviation ≈ 0 st
    synth.setPitchPlane(makeImage(1, 2, 0.5f, 0.5f, 0.5f));

    const auto data = synth.getPartialData();
    REQUIRE(data.frames.size() == 1);

    // Bottom pixel (y=1): baseFreq = 20, deviation ~0 → still ~20 Hz
    REQUIRE(data.frames[0].partials[1].frequency == Catch::Approx(20.0f).epsilon(0.03f));
}

// ============================================================================
// getPartialData — output structure / metadata
// ============================================================================

TEST_CASE("ImageSynthesizer - PartialData metadata is populated", "[image]")
{
    ana::ImageSynthesizer synth;
    synth.setSampleRate(48000.0);
    synth.setGainPlane(makeImage(16, 8, 1.0f, 1.0f, 1.0f));

    const auto data = synth.getPartialData();

    REQUIRE(data.sampleRate  == 48000.0);
    REQUIRE(data.hopSize     == 512.0);
    REQUIRE(data.maxPartials == 8);      // height
    REQUIRE(data.frames.size() == 16);   // width

    // Timestamps increase by hopSize/sampleRate per frame
    REQUIRE(data.frames[0].timestamp == 0.0);
    REQUIRE(data.frames[1].timestamp == Catch::Approx(512.0 / 48000.0));
}

// ============================================================================
// loadImage — invalid file returns false
// ============================================================================

TEST_CASE("ImageSynthesizer - loadImage with non-existent file", "[image]")
{
    ana::ImageSynthesizer synth;
    REQUIRE_FALSE(synth.loadImage(juce::File{}));
    REQUIRE_FALSE(synth.loadImage(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("__does_not_exist__.png")));
}

} // namespace
