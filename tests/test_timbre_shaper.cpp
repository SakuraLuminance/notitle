#include <catch2/catch_all.hpp>
#include "dsp/TimbreShaper.h"

#include <utility>

namespace
{
ana::PartialDataSIMD makeSet(std::initializer_list<std::pair<float, float>> ps)
{
    ana::PartialDataSIMD d;
    d.sampleRate = 44100.0;
    int i = 0;
    for (const auto& p : ps)
    {
        if (i >= ana::PartialDataSIMD::kMaxPartials) break;
        d.frequency[i] = p.first;
        d.amplitude[i] = p.second;
        ++i;
    }
    d.updateActiveMask();
    return d;
}
} // namespace

TEST_CASE("TimbreShaper bright tilts the spectrum", "[timbreshaper]")
{
    auto up = makeSet({ { 100.0f, 0.5f }, { 5000.0f, 0.5f } });
    ana::TimbreShaper::applyBright(up, 1.0f);          // tilt = +2
    REQUIRE(up.amplitude[1] > up.amplitude[0]);

    auto down = makeSet({ { 100.0f, 0.5f }, { 5000.0f, 0.5f } });
    ana::TimbreShaper::applyBright(down, 0.0f);        // tilt = -2
    REQUIRE(down.amplitude[0] > down.amplitude[1]);

    auto neutral = makeSet({ { 100.0f, 0.5f }, { 5000.0f, 0.5f } });
    ana::TimbreShaper::applyBright(neutral, 0.5f);
    REQUIRE(neutral.amplitude[0] == Catch::Approx(0.5f));
    REQUIRE(neutral.amplitude[1] == Catch::Approx(0.5f));
}

TEST_CASE("TimbreShaper HPF removes partials below the cutoff", "[timbreshaper]")
{
    auto p = makeSet({ { 100.0f, 0.8f }, { 1000.0f, 0.8f } });
    ana::TimbreShaper::applyHpf(p, 500.0f);

    REQUIRE(p.amplitude[0] == 0.0f);
    REQUIRE(p.amplitude[1] == Catch::Approx(0.8f));
    REQUIRE(p.activeCount == 1);
    REQUIRE(p.isActive(0) == false);
    REQUIRE(p.isActive(1) == true);
}

TEST_CASE("TimbreShaper blur spreads energy across neighbouring partials", "[timbreshaper]")
{
    auto p = makeSet({ { 200.0f, 1.0f }, { 400.0f, 0.2f }, { 600.0f, 1.0f } });
    const float before = p.amplitude[1];

    ana::TimbreShaper::applyBlur(p, 1.0f, 44100.0);

    REQUIRE(p.amplitude[1] > before);
    REQUIRE(p.activeCount == 3);
}

TEST_CASE("TimbreShaper blend crossfades amplitude", "[timbreshaper]")
{
    auto a = makeSet({ { 440.0f, 1.0f } });
    auto b = makeSet({ { 440.0f, 0.0f } });

    const auto m0 = ana::TimbreShaper::blend(a, b, 0.0f);
    REQUIRE(m0.amplitude[0] == Catch::Approx(1.0f));

    const auto m1 = ana::TimbreShaper::blend(a, b, 1.0f);
    REQUIRE(m1.amplitude[0] == Catch::Approx(0.0f));

    const auto mh = ana::TimbreShaper::blend(a, b, 0.5f);
    REQUIRE(mh.amplitude[0] == Catch::Approx(0.5f));
}

TEST_CASE("TimbreShaper neutral params leave the set unchanged", "[timbreshaper]")
{
    auto p = makeSet({ { 100.0f, 0.3f }, { 1000.0f, 0.7f } });
    ana::TimbreShaper::shape(p, ana::TimbreShapeParams{}, 44100.0);

    REQUIRE(p.amplitude[0] == Catch::Approx(0.3f));
    REQUIRE(p.amplitude[1] == Catch::Approx(0.7f));
}
