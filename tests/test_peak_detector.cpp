#include <catch2/catch_all.hpp>
#include "../src/dsp/PeakDetector.h"
#include <cmath>

TEST_CASE("PeakDetector - detect single peak", "[peak]")
{
    ana::PeakDetector detector;
    ana::STFTConfig config;
    config.peakThresholdDB = -60.0f;
    config.maxPartials = 512;

    // Create a spectrum with a single peak at bin 10
    std::vector<std::complex<float>> spectrum(1025, {0.0f, 0.0f});
    spectrum[10] = {1.0f, 0.0f};  // Peak at bin 10

    auto peaks = detector.detectPeaks(spectrum, config, 44100.0);

    REQUIRE(peaks.size() == 1);
    REQUIRE(peaks[0].amplitude == Approx(1.0f));
}

TEST_CASE("PeakDetector - no peaks in silence", "[peak]")
{
    ana::PeakDetector detector;
    ana::STFTConfig config;
    config.peakThresholdDB = -60.0f;
    config.maxPartials = 512;

    // Create a spectrum with all zeros
    std::vector<std::complex<float>> spectrum(1025, {0.0f, 0.0f});

    auto peaks = detector.detectPeaks(spectrum, config, 44100.0);

    REQUIRE(peaks.empty());
}

TEST_CASE("PeakDetector - multiple peaks", "[peak]")
{
    ana::PeakDetector detector;
    ana::STFTConfig config;
    config.peakThresholdDB = -60.0f;
    config.maxPartials = 512;

    // Create a spectrum with two peaks
    std::vector<std::complex<float>> spectrum(1025, {0.0f, 0.0f});
    spectrum[10] = {0.8f, 0.0f};  // Peak at bin 10
    spectrum[20] = {1.0f, 0.0f};  // Peak at bin 20

    auto peaks = detector.detectPeaks(spectrum, config, 44100.0);

    REQUIRE(peaks.size() == 2);
    // Should be sorted by amplitude descending
    REQUIRE(peaks[0].amplitude >= peaks[1].amplitude);
}

TEST_CASE("PeakDetector - max partials limit", "[peak]")
{
    ana::PeakDetector detector;
    ana::STFTConfig config;
    config.peakThresholdDB = -60.0f;
    config.maxPartials = 2;  // Limit to 2 partials

    // Create a spectrum with three peaks
    std::vector<std::complex<float>> spectrum(1025, {0.0f, 0.0f});
    spectrum[10] = {1.0f, 0.0f};
    spectrum[20] = {0.8f, 0.0f};
    spectrum[30] = {0.6f, 0.0f};

    auto peaks = detector.detectPeaks(spectrum, config, 44100.0);

    REQUIRE(peaks.size() == 2);
}

TEST_CASE("PeakDetector - threshold filtering", "[peak]")
{
    ana::PeakDetector detector;
    ana::STFTConfig config;
    config.peakThresholdDB = -20.0f;  // Higher threshold
    config.maxPartials = 512;

    // Create a spectrum with a quiet peak
    std::vector<std::complex<float>> spectrum(1025, {0.0f, 0.0f});
    spectrum[10] = {0.001f, 0.0f};  // Very quiet peak

    auto peaks = detector.detectPeaks(spectrum, config, 44100.0);

    // Should be filtered out by threshold
    REQUIRE(peaks.empty());
}
