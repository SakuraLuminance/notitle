#include <catch2/catch_all.hpp>
#include "gui/ThemePalettes.h"
#include "gui/CyberpunkTheme.h"

using namespace ana;

namespace
{
/** Restores the default palette after a case that switches themes. */
struct ThemeGuard
{
    ~ThemeGuard() { CyberpunkTheme::getInstance().applyPalette(0); }
};
}

TEST_CASE("Theme palettes: line-up is distinct and defaults to NEON", "[ui][theme]")
{
    REQUIRE(ThemePalettes::count >= 6);
    REQUIRE(juce::String(ThemePalettes::get(0).name) == "NEON");
    REQUIRE(ThemePalettes::get(0).bg     == juce::Colour(0x0a, 0x0a, 0x05));
    REQUIRE(ThemePalettes::get(0).accent == juce::Colour(0x00, 0xcc, 0xff));

    // Out-of-range indices clamp instead of reading past the table.
    REQUIRE(ThemePalettes::get(-5).bg == ThemePalettes::get(0).bg);
    REQUIRE(ThemePalettes::get(99).bg == ThemePalettes::get(ThemePalettes::count - 1).bg);

    // Every palette must be distinguishable by its background.
    for (int i = 0; i < ThemePalettes::count; ++i)
        for (int j = i + 1; j < ThemePalettes::count; ++j)
            REQUIRE(ThemePalettes::get(i).bg != ThemePalettes::get(j).bg);
}

TEST_CASE("Theme palettes: applying one updates slots, fonts and LookAndFeel", "[ui][theme]")
{
    ThemeGuard guard;

    auto& theme = CyberpunkTheme::getInstance();

    theme.applyPalette(0);
    REQUIRE(CyberpunkTheme::getThemeIndex() == 0);
    REQUIRE_FALSE(CyberpunkTheme::isMonoBody());

    const auto neonFg = CyberpunkTheme::fg_;

    theme.applyPalette(2);   // CRT: amber, mono body
    REQUIRE(CyberpunkTheme::getThemeIndex() == 2);
    REQUIRE(CyberpunkTheme::cyan_ == ThemePalettes::get(2).accent);
    REQUIRE(CyberpunkTheme::fg_   != neonFg);
    REQUIRE(CyberpunkTheme::isMonoBody());

    // The LookAndFeel's registered colour ids follow the palette.
    REQUIRE(theme.findColour(juce::Slider::rotarySliderFillColourId)
            == ThemePalettes::get(2).accent);

    theme.applyPalette(0);
    REQUIRE(CyberpunkTheme::bg_ == juce::Colour(0x0a, 0x0a, 0x05));
    REQUIRE(CyberpunkTheme::fg_ == neonFg);
    REQUIRE_FALSE(CyberpunkTheme::isMonoBody());
}

TEST_CASE("Theme palettes: live remap rewrites captured colours", "[ui][theme]")
{
    ThemeGuard guard;

    auto& theme = CyberpunkTheme::getInstance();
    theme.applyPalette(0);

    juce::Label direct;
    direct.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.5f));

    juce::Label derived;
    derived.setColour(juce::Label::textColourId, CyberpunkTheme::cyan_.darker(0.5f));

    juce::Label custom;
    custom.setColour(juce::Label::textColourId, juce::Colours::red);

    juce::Component root;
    root.addAndMakeVisible(direct);
    root.addAndMakeVisible(derived);
    root.addAndMakeVisible(custom);

    const auto oldPalette = ThemePalettes::get(0);
    theme.applyPalette(1);   // VAPOR
    CyberpunkTheme::remapComponentColours(root, oldPalette);

    REQUIRE(direct.findColour(juce::Label::textColourId)
            == CyberpunkTheme::fg_.withAlpha(0.5f));
    REQUIRE(derived.findColour(juce::Label::textColourId)
            == CyberpunkTheme::cyan_.darker(0.5f));
    REQUIRE(custom.findColour(juce::Label::textColourId) == juce::Colours::red);
}
