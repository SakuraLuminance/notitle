#include "ParticleDisplay.h"

#include <cmath>

namespace ana
{

namespace
{
constexpr float kMinHz = 20.0f;
constexpr float kMaxHz = 20000.0f;
}

void ParticleDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour(CyberpunkTheme::bg_.darker(CyberpunkTheme::kCanvasBgDarken));
    g.fillRect(bounds);

    const auto area = bounds.reduced(8.0f, 12.0f);

    g.setColour(CyberpunkTheme::fg_.withAlpha(CyberpunkTheme::kCanvasBorderAlpha));
    g.drawRect(area, 1.0f);

    // Log frequency grid
    g.setColour(CyberpunkTheme::fg_.withAlpha(CyberpunkTheme::kCanvasGridAlpha));
    const float minLog = std::log2(kMinHz);
    const float maxLog = std::log2(kMaxHz);
    for (float f = 100.0f; f <= 10000.0f; f *= 10.0f)
    {
        const float x = area.getX() + (std::log2(f) - minLog) / (maxLog - minLog) * area.getWidth();
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
    }

    if (system_ != nullptr && area.getWidth() > 1.0f && area.getHeight() > 1.0f)
    {
        for (const auto& p : system_->getParticles())
        {
            if (! p.active || p.amplitude <= 0.0005f)
                continue;

            const float freq = juce::jlimit(kMinHz, kMaxHz, p.frequency);
            const float x = area.getX()
                          + (std::log2(freq) - minLog) / (maxLog - minLog) * area.getWidth();
            const float y = area.getBottom()
                          - juce::jlimit(0.0f, 1.0f, p.amplitude) * area.getHeight();
            const float r = 1.0f + juce::jlimit(0.0f, 1.0f, p.brightness) * 3.0f;
            const float alpha = juce::jlimit(0.15f, 1.0f, p.life);

            g.setColour(juce::Colour::fromHSV(juce::jlimit(0.0f, 1.0f, p.hue),
                                              0.8f, 1.0f, alpha));
            g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);
        }
    }

    // Frequency axis labels, matching the IMAGE view's axis treatment.
    if (area.getWidth() > 40.0f)
    {
        g.setFont(CyberpunkTheme::getCyberFont(9.0f));
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.45f));

        const float gridMinLog = std::log2(kMinHz);
        const float gridMaxLog = std::log2(kMaxHz);

        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = area.getX()
                          + (std::log2(f) - gridMinLog) / (gridMaxLog - gridMinLog) * area.getWidth();
            const juce::String text = (f >= 1000.0f) ? juce::String(f / 1000.0f, 0) + "k"
                                                     : juce::String(f, 0);
            g.drawText(text,
                       juce::Rectangle<float>(x - 14.0f, area.getBottom() + 1.0f, 28.0f, 10.0f),
                       juce::Justification::centred);
        }
    }

    g.setColour(CyberpunkTheme::fg_.withAlpha(0.75f));
    g.setFont(CyberpunkTheme::getCyberFont(CyberpunkTheme::kReadoutFontH));
    g.drawText("PARTICLES: " + juce::String(system_ != nullptr ? system_->getActiveCount() : 0),
               getLocalBounds().reduced(10, 3), juce::Justification::topLeft);
}

} // namespace ana
