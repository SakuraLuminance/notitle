#include <catch2/catch_all.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/ParticleDisplay.h"

TEST_CASE("ParticleDisplay paint is headless safe", "[ui][particles]")
{
    ana::ParticleDisplay display;
    display.setBounds(0, 0, 400, 200);

    juce::Image img(juce::Image::ARGB, 400, 200, true);
    juce::Graphics g(img);

    // No system bound yet.
    REQUIRE_NOTHROW(display.paint(g));

    ana::SpectralParticleSystem system;
    display.setParticleSystem(&system);
    REQUIRE_NOTHROW(display.paint(g));   // empty system

    ana::PartialDataSIMD partials;
    partials.sampleRate = 44100.0;
    partials.maxPartials = ana::PartialDataSIMD::kMaxPartials;
    for (int i = 0; i < 16; ++i)
    {
        partials.frequency[i] = 100.0f * static_cast<float>(i + 1);
        partials.amplitude[i] = static_cast<float>(i % 4 + 1) * 0.2f;
    }
    partials.updateActiveMask();

    system.emitFromPartials(partials);
    system.update(0.05);
    REQUIRE_NOTHROW(display.paint(g));

    SECTION("tiny bounds do not crash")
    {
        display.setBounds(0, 0, 1, 1);
        REQUIRE_NOTHROW(display.paint(g));
    }
}
