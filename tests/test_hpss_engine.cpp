#include <catch2/catch_all.hpp>
#include "dsp/HPSSEngine.h"
#include <cmath>
#include <vector>

namespace
{
double energyOf(const std::vector<float>& v)
{
    double e = 0.0;
    for (float x : v)
        e += static_cast<double>(x) * static_cast<double>(x);
    return e;
}
}

TEST_CASE("HPSSEngine keeps a steady tone in the harmonic component", "[hpss]")
{
    constexpr double sr = 44100.0;
    const int n = 16384;

    std::vector<float> tone(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        tone[static_cast<size_t>(i)] = 0.5f * std::sin(
            2.0f * juce::MathConstants<float>::pi * 440.0f * static_cast<float>(i)
            / static_cast<float>(sr));

    ana::HPSSEngine hpss;
    const auto res = hpss.separate(tone, sr);

    REQUIRE(res.harmonic.size() == tone.size());
    REQUIRE(res.percussive.size() == tone.size());
    REQUIRE(res.sampleRate == Catch::Approx(sr));

    const double eH = energyOf(res.harmonic);
    const double eP = energyOf(res.percussive);

    REQUIRE(eH > 0.0);
    REQUIRE(eH > eP);
}

TEST_CASE("HPSSEngine is safe for silence and very short input", "[hpss]")
{
    ana::HPSSEngine hpss;

    std::vector<float> silence(4096, 0.0f);
    const auto res = hpss.separate(silence, 44100.0);

    REQUIRE(res.harmonic.size() == silence.size());
    REQUIRE(res.percussive.size() == silence.size());

    for (float v : res.harmonic)
        REQUIRE(std::isfinite(v));
    for (float v : res.percussive)
        REQUIRE(std::isfinite(v));

    // Below the analysis window: must not crash (may return empty vectors).
    std::vector<float> tiny(64, 0.1f);
    REQUIRE_NOTHROW(hpss.separate(tiny, 44100.0));
}
