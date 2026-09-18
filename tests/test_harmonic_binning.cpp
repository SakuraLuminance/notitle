#include <catch2/catch_all.hpp>
#include "dsp/HarmonicBinning.h"

#include <vector>

//==============================================================================
// Harmonic binning (P6 leftover): the STFT peak tracker emits each frame's
// partials sorted by frequency, so a peak appearing or disappearing used to
// shift every later row by one and AdditiveBank cross-faded unrelated
// harmonics between frames.  Binning every frame against one shared f0 makes
// row p mean harmonic p+1 in every frame.
//==============================================================================

namespace
{
ana::Partial peak(float frequency, float amplitude)
{
    return { frequency, amplitude, 0.0f };
}

/** f0, 2*f0, 3*f0 ... with a 1/h amplitude roll-off (as a real harmonic source). */
std::vector<ana::Partial> harmonicSeries(float f0, int count)
{
    std::vector<ana::Partial> partials;
    for (int h = 1; h <= count; ++h)
        partials.push_back(peak(f0 * static_cast<float>(h), 1.0f / static_cast<float>(h)));
    return partials;
}
} // namespace

TEST_CASE("HarmonicBinning - fundamental is the lowest strong peak", "[harmonic]")
{
    const auto partials = harmonicSeries(100.0f, 5);
    REQUIRE(ana::HarmonicBinning::estimateFundamental(partials) == Catch::Approx(100.0f));

    // A weak peak below the amplitude share cannot define the fundamental.
    auto withWeak = partials;
    withWeak.insert(withWeak.begin(), peak(50.0f, 0.02f));
    REQUIRE(ana::HarmonicBinning::estimateFundamental(withWeak) == Catch::Approx(100.0f));

    // Silence (and an empty frame) defines no fundamental at all.
    const std::vector<ana::Partial> quiet{ peak(120.0f, 0.001f) };
    REQUIRE(ana::HarmonicBinning::estimateFundamental(quiet) == 0.0f);
    REQUIRE(ana::HarmonicBinning::estimateFundamental({}) == 0.0f);
}

TEST_CASE("HarmonicBinning - a harmonic series lands on its harmonic rows", "[harmonic]")
{
    const auto partials = harmonicSeries(100.0f, 4);

    ana::PartialDataSIMD out;
    ana::HarmonicBinning::binInto(partials, 100.0f, ana::PartialDataSIMD::kMaxPartials, out);

    REQUIRE(out.activeCount == 4);
    REQUIRE(out.frequency[0] == Catch::Approx(100.0f));
    REQUIRE(out.frequency[1] == Catch::Approx(200.0f));
    REQUIRE(out.frequency[2] == Catch::Approx(300.0f));
    REQUIRE(out.frequency[3] == Catch::Approx(400.0f));
    REQUIRE(out.amplitude[0] == Catch::Approx(1.0f));
    REQUIRE(out.amplitude[1] == Catch::Approx(0.5f));
    REQUIRE(out.amplitude[4] == Catch::Approx(0.0f));
    REQUIRE(out.maxPartials == ana::PartialDataSIMD::kMaxPartials);
}

TEST_CASE("HarmonicBinning - collisions keep the louder peak, the top row absorbs overflow", "[harmonic]")
{
    std::vector<ana::Partial> partials{
        peak(30.0f,   0.9f),   // below 0.5 * f0 -> not part of the axis
        peak(102.0f,  0.2f),   // harmonic 1, weaker
        peak(98.0f,   0.6f),   // harmonic 1, louder -> wins the row
        peak(5000.0f, 0.4f)    // harmonic 50 (axis has 8 rows) -> folded to row 7
    };

    ana::PartialDataSIMD out;
    ana::HarmonicBinning::binInto(partials, 100.0f, 8, out);

    REQUIRE(out.maxPartials == 8);
    REQUIRE(out.amplitude[0] == Catch::Approx(0.6f));
    REQUIRE(out.frequency[0] == Catch::Approx(98.0f));
    REQUIRE(out.phase[0]     == Catch::Approx(0.0f));
    REQUIRE(out.frequency[7] == Catch::Approx(5000.0f));
    REQUIRE(out.amplitude[7] == Catch::Approx(0.4f));
    REQUIRE(out.activeCount == 2);
}

TEST_CASE("HarmonicBinning - without a fundamental the mapping stays copy by index", "[harmonic]")
{
    const std::vector<ana::Partial> partials{ peak(1200.0f, 0.5f), peak(2400.0f, 0.25f) };

    ana::PartialDataSIMD out;
    ana::HarmonicBinning::binInto(partials, 0.0f, ana::PartialDataSIMD::kMaxPartials, out);

    REQUIRE(out.activeCount == 2);
    REQUIRE(out.frequency[0] == Catch::Approx(1200.0f));
    REQUIRE(out.frequency[1] == Catch::Approx(2400.0f));
    REQUIRE(out.amplitude[0] == Catch::Approx(0.5f));
}

TEST_CASE("HarmonicBinning - one shared axis keeps harmonics aligned across frames", "[harmonic]")
{
    std::vector<ana::PartialFrame> frames;

    ana::PartialFrame frameA;
    frameA.partials = harmonicSeries(200.0f, 4);   // 200 / 400 / 600 / 800
    frames.push_back(frameA);

    ana::PartialFrame frameB;
    frameB.partials = harmonicSeries(200.0f, 5);
    frameB.partials.erase(frameB.partials.begin()); // the fundamental disappears
    frames.push_back(frameB);

    const float f0 = ana::HarmonicBinning::estimateSharedFundamental(frames);
    REQUIRE(f0 == Catch::Approx(200.0f));

    ana::PartialDataSIMD binA, binB;
    ana::HarmonicBinning::binInto(frames[0].partials, f0, ana::PartialDataSIMD::kMaxPartials, binA);
    ana::HarmonicBinning::binInto(frames[1].partials, f0, ana::PartialDataSIMD::kMaxPartials, binB);

    // Row 1 is H2 in BOTH frames; a plain copy would have put 600 Hz there in
    // frame B, so interpolating A -> B would glide 400 Hz up to 600 Hz.
    REQUIRE(binA.frequency[1] == Catch::Approx(400.0f));
    REQUIRE(binB.frequency[1] == Catch::Approx(400.0f));
    REQUIRE(binB.amplitude[1] == Catch::Approx(0.5f));
    REQUIRE(binB.amplitude[0] == Catch::Approx(0.0f));
    REQUIRE(binB.activeCount == 4);

    // Rows are harmonic numbers, so the implied frequency is exact.
    REQUIRE(ana::HarmonicBinning::binFrequency(0, f0) == Catch::Approx(200.0f));
    REQUIRE(ana::HarmonicBinning::binFrequency(3, f0) == Catch::Approx(800.0f));
    REQUIRE(ana::HarmonicBinning::binFrequency(3, 0.0f) == Catch::Approx(0.0f));
}
