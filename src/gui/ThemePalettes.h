#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ana
{

//==============================================================================
/**
    One selectable visual theme ("visual kei" line-up).

    The five colours map onto the existing CyberpunkTheme slots so every panel
    keeps working without changes:

        bg        background                 (was CyberpunkTheme::bg_)
        fg        foreground / body text     (fg_)
        accent    primary interactive accent (cyan_)
        accent2   secondary accent           (magenta_)
        highlight value highlight            (yellow_)

    monoBody renders plain body text monospaced as well (CRT / mono themes).
*/
struct ThemePalette
{
    const char* name;
    juce::Colour bg;
    juce::Colour fg;
    juce::Colour accent;
    juce::Colour accent2;
    juce::Colour highlight;
    bool monoBody;
};

namespace ThemePalettes
{
inline constexpr int count = 7;

/** Built-in palettes.  Index 0 must stay the original NEON palette: existing
    colour tests assert those exact values. */
inline const ThemePalette& get(int index) noexcept
{
    static const ThemePalette palettes[count] =
    {
        // name        bg                    fg                    accent                accent2               highlight             mono
        { "NEON",   { 0x0a, 0x0a, 0x05 }, { 0xd0, 0xe0, 0xc8 }, { 0x00, 0xcc, 0xff }, { 0xff, 0x00, 0xff }, { 0x39, 0xff, 0x14 }, false },
        { "VAPOR",  { 0x0d, 0x06, 0x20 }, { 0xed, 0xe4, 0xff }, { 0xff, 0x2f, 0xb9 }, { 0x00, 0xe5, 0xff }, { 0xb4, 0x7c, 0xff }, false },
        { "CRT",    { 0x0f, 0x0b, 0x06 }, { 0xff, 0xd9, 0xa0 }, { 0xff, 0xb0, 0x00 }, { 0xff, 0x6a, 0x00 }, { 0x2e, 0xe6, 0xc6 }, true  },
        { "ICE",    { 0x05, 0x08, 0x0f }, { 0xdc, 0xe8, 0xf5 }, { 0x6e, 0xc6, 0xff }, { 0xb3, 0x88, 0xff }, { 0x7d, 0xff, 0xd4 }, false },
        { "MONO",   { 0x0b, 0x0b, 0x0c }, { 0xe6, 0xe6, 0xe6 }, { 0xff, 0xff, 0xff }, { 0x9a, 0x9a, 0x9a }, { 0xd4, 0xd4, 0xd4 }, true  },
        { "TOXIC",  { 0x07, 0x0b, 0x04 }, { 0xd8, 0xf0, 0xc0 }, { 0xa8, 0xff, 0x00 }, { 0x00, 0xff, 0xa3 }, { 0xff, 0xe6, 0x00 }, false },
        { "SAKURA", { 0x12, 0x0a, 0x12 }, { 0xff, 0xe6, 0xf2 }, { 0xff, 0x7a, 0xb6 }, { 0xc7, 0x7d, 0xff }, { 0x7b, 0xe0, 0xff }, false }
    };

    return palettes[juce::jlimit(0, count - 1, index)];
}

/** Display name for the theme selector. */
inline juce::String name(int index)
{
    return juce::String(get(index).name);
}
}

} // namespace ana
