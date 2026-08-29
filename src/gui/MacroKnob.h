#pragma once

#include "CyberpunkTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace ana {

//==============================================================================
/** A macro knob that paints a coloured arc based on its mapping curve. */
class MacroKnob : public juce::Slider
{
public:
    MacroKnob() { setSliderStyle(juce::Slider::RotaryVerticalDrag); }

    void setCurveExponent(float exp) noexcept { curveExp_ = exp; }
    float getCurveExponent() const noexcept { return curveExp_; }

    juce::Colour getCurveColour() const
    {
        // Linear (1.0) → cyan_ (neon purple)
        // Exponential (2.0) → magenta_ (electric magenta-purple)
        // S-curve (0.5) → yellow_ (cool lavender)
        // Other → fg_ (light lavender)
        if (curveExp_ > 1.5f)
            return CyberpunkTheme::magenta_;
        if (curveExp_ < 0.75f)
            return CyberpunkTheme::yellow_;
        if (curveExp_ == 1.0f)
            return CyberpunkTheme::cyan_;
        return CyberpunkTheme::fg_;
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(2);
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin(bounds.getWidth() * 0.5f,
                                         bounds.getHeight() * 0.5f);
        const float startAngle = juce::MathConstants<float>::pi * 1.25f;
        const float endAngle   = juce::MathConstants<float>::pi * 0.25f;
        const float sliderPos = static_cast<float>(
            getValue() - getMinimum()) / static_cast<float>(getMaximum() - getMinimum());
        const float arcAngle = juce::jmap(sliderPos, 0.0f, 1.0f, startAngle, endAngle);

        // Background track
        g.setColour(CyberpunkTheme::bg_.brighter(0.15f));
        g.fillEllipse(centre.getX() - radius, centre.getY() - radius,
                      radius * 2, radius * 2);

        // Active arc with curve-specific colour
        const auto arcColour = getCurveColour();
        juce::Path arc;
        arc.addArc(centre.getX() - radius + 5, centre.getY() - radius + 5,
                   radius * 2 - 10, radius * 2 - 10,
                   startAngle, arcAngle, true);
        g.setColour(arcColour);
        g.strokePath(arc, juce::PathStrokeType(2.5f));

        // Glow
        g.setColour(arcColour.withAlpha(0.15f));
        g.strokePath(arc, juce::PathStrokeType(6.0f));

        // Thumb dot
        const float thumbAngle = juce::jmap(sliderPos, 0.0f, 1.0f,
                                             startAngle, endAngle);
        const float dotX = centre.getX() + (radius - 9) * std::cos(thumbAngle);
        const float dotY = centre.getY() + (radius - 9) * std::sin(thumbAngle);
        g.setColour(arcColour);
        g.fillEllipse(dotX - 3, dotY - 3, 6, 6);

        // Center cap
        g.setColour(CyberpunkTheme::bg_.brighter(0.3f));
        g.fillEllipse(centre.getX() - 4, centre.getY() - 4, 8, 8);
        g.setColour(arcColour.withAlpha(0.5f));
        g.drawEllipse(centre.getX() - 4, centre.getY() - 4, 8, 8, 1.0f);
    }

private:
    float curveExp_ = 1.0f;
};

} // namespace ana
