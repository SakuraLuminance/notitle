#include <catch2/catch_all.hpp>
#include "gui/CyberpunkTheme.h"

TEST_CASE("Theme colours are monochrome purple", "[ui][theme]")
{
    // Dark-mode palette values declared in CyberpunkTheme.h
    REQUIRE(ana::CyberpunkTheme::bg_ == juce::Colour(0x08, 0x05, 0x12));
    REQUIRE(ana::CyberpunkTheme::fg_ == juce::Colour(0xc8, 0xc0, 0xd8));
    REQUIRE(ana::CyberpunkTheme::cyan_ == juce::Colour(0x88, 0x00, 0xff));
    REQUIRE(ana::CyberpunkTheme::magenta_ == juce::Colour(0xcf, 0x00, 0xff));
    REQUIRE(ana::CyberpunkTheme::yellow_ == juce::Colour(0xbb, 0x88, 0xff));

    // LookAndFeel colour registration via setupColours()
    auto& theme = ana::CyberpunkTheme::getInstance();
    REQUIRE(theme.findColour(juce::ResizableWindow::backgroundColourId)
            == ana::CyberpunkTheme::bg_);
    REQUIRE(theme.findColour(juce::Slider::rotarySliderFillColourId)
            == ana::CyberpunkTheme::cyan_);
}
