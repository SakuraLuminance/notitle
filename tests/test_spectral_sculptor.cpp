#include <catch2/catch_all.hpp>
#include "../src/dsp/SpectralSculptor.h"
#include <cmath>
#include <vector>

using namespace ana;

TEST_CASE("SpectralSculptor - initial state", "[sculptor][init]")
{
    SpectralSculptor ss;
    SUCCEED();
}

TEST_CASE("SpectralSculptor - set parameters", "[sculptor][params]")
{
    SpectralSculptor ss;
    ss.setActiveTool(SpectralSculptor::Tool::Brush);
    ss.setBrushSize(0.5f);
    ss.setBrushStrength(0.8f);
    ss.setPosition(0.5f);
    ss.setCenterFrequency(1000.0f);
    ss.setSourcePosition(0.2f);
    ss.setWarpFactor(2.0f);
    ss.setWarpCenter(0.5f);
    ss.setFractalIterations(4);
    ss.setFractalSeed(0.3f);
    SUCCEED();
}

TEST_CASE("SpectralSculptor - process tools", "[sculptor][process]")
{
    SpectralSculptor ss;
    
    PartialDataSIMD partials;
    partials.activeCount = 10;
    for (int i = 0; i < 10; ++i) {
        partials.frequency[i] = 100.0f * (i + 1);
        partials.amplitude[i] = 1.0f;
    }

    SECTION("Brush")
    {
        ss.setActiveTool(SpectralSculptor::Tool::Brush);
        ss.process(partials);
        SUCCEED();
    }

    SECTION("Eraser")
    {
        ss.setActiveTool(SpectralSculptor::Tool::Eraser);
        ss.process(partials);
        SUCCEED();
    }

    SECTION("Smudge")
    {
        ss.setActiveTool(SpectralSculptor::Tool::Smudge);
        ss.process(partials);
        SUCCEED();
    }

    SECTION("Sharpen")
    {
        ss.setActiveTool(SpectralSculptor::Tool::Sharpen);
        ss.process(partials);
        SUCCEED();
    }

    SECTION("Warp")
    {
        ss.setActiveTool(SpectralSculptor::Tool::Warp);
        ss.process(partials);
        SUCCEED();
    }

    SECTION("Clone")
    {
        ss.setActiveTool(SpectralSculptor::Tool::Clone);
        ss.cloneToPosition(0.5f); // triggers paste
        ss.process(partials);
        SUCCEED();
    }

    SECTION("Mirror")
    {
        ss.setActiveTool(SpectralSculptor::Tool::Mirror);
        ss.process(partials);
        SUCCEED();
    }

    SECTION("Fractal")
    {
        ss.setActiveTool(SpectralSculptor::Tool::Fractal);
        ss.process(partials);
        SUCCEED();
    }
}

TEST_CASE("SpectralSculptor - reset", "[sculptor][reset]")
{
    SpectralSculptor ss;
    ss.reset();
    SUCCEED();
}
