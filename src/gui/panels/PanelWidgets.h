#pragma once

#include "../CyberpunkTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{
namespace panelwidgets
{

inline void cyberKnob(juce::Component& parent, juce::Slider& slider, juce::Label& label,
                      const juce::String& name, double min, double max,
                      double init, double step,
                      juce::Slider::SliderStyle style = juce::Slider::RotaryVerticalDrag)
{
    slider.setRange(min, max, step);
    slider.setValue(init);
    slider.setSliderStyle(style);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue(true, init);
    parent.addAndMakeVisible(slider);

    label.setText(name, juce::dontSendNotification);
    label.setFont(CyberpunkTheme::getCyberFont(9.0f, false));
    label.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.8f));
    label.setJustificationType(juce::Justification::centred);
    parent.addAndMakeVisible(label);
}

inline juce::TextButton& cyberButton(juce::Component& parent, juce::TextButton& btn)
{
    parent.addAndMakeVisible(btn);
    btn.setColour(juce::TextButton::buttonColourId, CyberpunkTheme::cyan_.darker(0.7f));
    btn.setColour(juce::TextButton::textColourOffId, CyberpunkTheme::fg_);
    return btn;
}

} // namespace panelwidgets
} // namespace ana
