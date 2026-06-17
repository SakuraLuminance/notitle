#include <catch2/catch_all.hpp>
#include "gui/CyberpunkTheme.h"

TEST_CASE("Theme colours are toxic cyberpunk", "[ui][theme]")
{
    // Dark-mode palette values declared in CyberpunkTheme.h
    REQUIRE(ana::CyberpunkTheme::bg_ == juce::Colour(0x0a, 0x0a, 0x05));
    REQUIRE(ana::CyberpunkTheme::fg_ == juce::Colour(0xd0, 0xe0, 0xc8));
    REQUIRE(ana::CyberpunkTheme::cyan_ == juce::Colour(0x00, 0xcc, 0xff));
    REQUIRE(ana::CyberpunkTheme::magenta_ == juce::Colour(0xff, 0x00, 0xff));
    REQUIRE(ana::CyberpunkTheme::yellow_ == juce::Colour(0x39, 0xff, 0x14));

    // LookAndFeel colour registration via setupColours()
    auto& theme = ana::CyberpunkTheme::getInstance();
    REQUIRE(theme.findColour(juce::ResizableWindow::backgroundColourId)
            == ana::CyberpunkTheme::bg_);
    REQUIRE(theme.findColour(juce::Slider::rotarySliderFillColourId)
            == ana::CyberpunkTheme::cyan_);
}
