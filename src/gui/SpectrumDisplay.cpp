#include "SpectrumDisplay.h"
#include "CyberpunkTheme.h"

namespace ana {

SpectrumDisplay::SpectrumDisplay()
{
}

SpectrumDisplay::~SpectrumDisplay()
{
}

void SpectrumDisplay::paint(juce::Graphics& g)
{
    g.fillAll(CyberpunkTheme::bg_);

    if (partials.empty())
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.5f));
        g.drawText("No partials detected", getLocalBounds(),
                   juce::Justification::centred);
        return;
    }

    auto area = getLocalBounds().toFloat();
    const float width = area.getWidth();
    const float height = area.getHeight();

    // Draw frequency grid lines
    g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));
    for (int freq = 1000; freq < 20000; freq += 1000)
    {
        float x = area.getX() + (freq / maxFrequency) * width;
        g.drawLine(x, area.getY(), x, area.getBottom(), 0.5f);
    }

    // Draw partials as vertical lines
    for (const auto& partial : partials)
    {
        float x = area.getX() + (partial.frequency / maxFrequency) * width;
        float barHeight = partial.amplitude * height * 0.8f;

        // Color based on amplitude
        float hue = 0.6f - (partial.amplitude * 0.6f); // blue to red
        g.setColour(juce::Colour::fromHSV(hue, 0.8f, 0.9f, 0.8f));
        g.drawLine(x, area.getBottom(), x, area.getBottom() - barHeight, 2.0f);
    }

    // Draw frequency labels
    g.setColour(juce::Colours::grey);
    g.setFont(10.0f);
    for (int freq = 1000; freq < 20000; freq += 5000)
    {
        float x = area.getX() + (freq / maxFrequency) * width;
        g.drawText(juce::String(freq / 1000) + "k",
                   static_cast<int>(x) - 15, static_cast<int>(area.getBottom()) - 15,
                   30, 15, juce::Justification::centred);
    }
}

void SpectrumDisplay::resized()
{
}

void SpectrumDisplay::setPartials(const std::vector<Partial>& newPartials)
{
    partials = newPartials;
    repaint();
}

void SpectrumDisplay::clear()
{
    partials.clear();
    repaint();
}

} // namespace ana
