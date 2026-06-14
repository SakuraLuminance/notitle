#include <catch2/catch_all.hpp>
#include "../src/dsp/AnaPlugEngine.h"

TEST_CASE("AnaPlugEngine - initial state", "[engine]")
{
    ana::AnaPlugEngine engine;

    REQUIRE_FALSE(engine.isLoaded());
    REQUIRE_FALSE(engine.isAnalyzed());
}

TEST_CASE("AnaPlugEngine - load non-existent file", "[engine]")
{
    ana::AnaPlugEngine engine;

    bool success = engine.loadSample(juce::File("/nonexistent/file.wav"));

    REQUIRE_FALSE(success);
    REQUIRE_FALSE(engine.isLoaded());
    REQUIRE_FALSE(engine.isAnalyzed());
}

TEST_CASE("AnaPlugEngine - analyze without load", "[engine]")
{
    ana::AnaPlugEngine engine;

    bool success = engine.analyze();

    REQUIRE_FALSE(success);
    REQUIRE_FALSE(engine.isAnalyzed());
}

TEST_CASE("AnaPlugEngine - resynthesize without analyze", "[engine]")
{
    ana::AnaPlugEngine engine;

    auto result = engine.resynthesize();

    REQUIRE(result.empty());
}

TEST_CASE("AnaPlugEngine - state transitions", "[engine]")
{
    ana::AnaPlugEngine engine;

    // Initial state
    REQUIRE_FALSE(engine.isLoaded());
    REQUIRE_FALSE(engine.isAnalyzed());

    // After failed load
    engine.loadSample(juce::File("/nonexistent/file.wav"));
    REQUIRE_FALSE(engine.isLoaded());
    REQUIRE_FALSE(engine.isAnalyzed());

    // After failed analyze
    engine.analyze();
    REQUIRE_FALSE(engine.isAnalyzed());
}
