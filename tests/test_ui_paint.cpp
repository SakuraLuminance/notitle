#include <catch2/catch_all.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/CyberpunkTheme.h"
#include "gui/SpectrumDisplay.h"

TEST_CASE("CyberpunkTheme static paint helpers do not crash", "[ui][paint]")
{
    juce::Image image(juce::Image::ARGB, 200, 200, true);
    juce::Graphics g(image);

    // Static grid background — should not throw or crash
    REQUIRE_NOTHROW(
        ana::CyberpunkTheme::drawGridBackground(
            g, juce::Rectangle<int>(0, 0, 200, 200))
    );

    // Static panel border — should not throw or crash
    REQUIRE_NOTHROW(
        ana::CyberpunkTheme::drawPanelBorder(
            g, juce::Rectangle<int>(0, 0, 200, 200), "Test Panel")
    );

    REQUIRE(true);
}

TEST_CASE("SpectrumDisplay paint does not crash in headless context", "[ui][paint]")
{
    juce::Image image(juce::Image::ARGB, 200, 200, true);
    juce::Graphics g(image);

    ana::SpectrumDisplay display;
    display.setBounds(0, 0, 200, 200);

    REQUIRE_NOTHROW(display.paint(g));

    REQUIRE(true);
}
