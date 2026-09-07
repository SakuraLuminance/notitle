#include <catch2/catch_all.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/CyberpunkTheme.h"
#include "gui/SpectrumDisplay.h"
#include "gui/LiveSpectrumPanel.h"
#include "gui/EffectParamPanel.h"
#include "dsp/effects/EffectParamRegistry.h"
#include <cmath>
#include <vector>

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

TEST_CASE("LiveSpectrumPanel paint does not crash without data", "[ui][paint]")
{
    ana::LiveSpectrumPanel panel;
    panel.setBounds(0, 0, 200, 200);

    juce::Image image(juce::Image::ARGB, 200, 200, true);
    juce::Graphics g(image);
    REQUIRE_NOTHROW(panel.paint(g));
    REQUIRE(true);
}

TEST_CASE("LiveSpectrumPanel FFT path handles sine input", "[ui][paint]")
{
    ana::LiveSpectrumPanel panel;
    panel.setBounds(0, 0, 200, 200);
    panel.resized();

    std::vector<float> sine(2048, 0.0f);
    for (int i = 0; i < 2048; ++i)
        sine[(size_t) i] = 0.8f * std::sin(2.0f * juce::MathConstants<float>::pi
            * 1000.0f * static_cast<float>(i) / 48000.0f);

    juce::Image image(juce::Image::ARGB, 200, 200, true);
    juce::Graphics g(image);
    REQUIRE_NOTHROW(panel.updateFromSamples(sine.data(), 2048));
    REQUIRE_NOTHROW(panel.paint(g));

    // Zero-length / null guards
    REQUIRE_NOTHROW(panel.updateFromSamples(nullptr, 0));
    REQUIRE_NOTHROW(panel.paint(g));
    REQUIRE(true);
}

TEST_CASE("EffectParamPanel paint does not crash", "[ui][paint]")
{
    auto effect = ana::EffectParamRegistry::create("Delay");
    REQUIRE(effect != nullptr);

    ana::EffectParamPanel panel(effect.get());
    panel.setBounds(0, 0, 400, 70);

    juce::Image image(juce::Image::ARGB, 400, 70, true);
    juce::Graphics g(image);
    REQUIRE_NOTHROW(panel.paint(g));
    REQUIRE_NOTHROW(panel.resized());

    // Null effect → idle text path
    ana::EffectParamPanel emptyPanel(nullptr);
    emptyPanel.setBounds(0, 0, 200, 18);
    juce::Image image2(juce::Image::ARGB, 200, 18, true);
    juce::Graphics g2(image2);
    REQUIRE_NOTHROW(emptyPanel.paint(g2));
    REQUIRE(true);
}
