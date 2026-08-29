#include "StepCell.h"

namespace ana {

//==============================================================================
StepCell::StepCell(int index, AnaPlugAudioProcessor& p)
    : index_(index), processor_(p)
{
    // Gate toggle button
    gateButton_.setToggleState(true, juce::dontSendNotification);
    gateButton_.setColour(juce::ToggleButton::tickColourId, ana::CyberpunkTheme::cyan_);
    gateButton_.setColour(juce::ToggleButton::tickDisabledColourId, ana::CyberpunkTheme::fg_.withAlpha(0.3f));
    gateButton_.onClick = [this]()
    {
        if (onGateChanged)
            onGateChanged(index_, gateButton_.getToggleState());
    };
    addAndMakeVisible(gateButton_);

    // Value slider
    valueSlider_.setRange(0.0, 1.0, 0.01);
    valueSlider_.setValue(static_cast<double>(index_) / 15.0);
    valueSlider_.setSliderStyle(juce::Slider::LinearVertical);
    valueSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    valueSlider_.setColour(juce::Slider::thumbColourId, ana::CyberpunkTheme::magenta_);
    valueSlider_.setColour(juce::Slider::trackColourId, ana::CyberpunkTheme::magenta_.withAlpha(0.5f));
    valueSlider_.setColour(juce::Slider::backgroundColourId, ana::CyberpunkTheme::bg_.brighter(0.15f));
    valueSlider_.onValueChange = [this]()
    {
        if (onValueChanged)
            onValueChanged(index_, static_cast<float>(valueSlider_.getValue()));
    };
    addAndMakeVisible(valueSlider_);
}

void StepCell::resized()
{
    auto b = getLocalBounds();
    // Gate button at top 40%
    auto gateArea = b.removeFromTop(static_cast<int>(b.getHeight() * 0.4f));
    gateButton_.setBounds(gateArea.reduced(2, 0));
    // Value slider fills the rest
    valueSlider_.setBounds(b.reduced(2, 0));
}

void StepCell::paint(juce::Graphics& g)
{
    // Highlight background if this step is the current one
    auto& seq = processor_.getStepSequencer();
    if (seq.getCurrentStep() == index_)
    {
        g.setColour(ana::CyberpunkTheme::cyan_.withAlpha(0.15f));
        g.fillRect(getLocalBounds().reduced(1));
    }

    // Border
    g.setColour(ana::CyberpunkTheme::fg_.withAlpha(0.1f));
    g.drawRect(getLocalBounds(), 1);
}

void StepCell::setActive(bool active)
{
    gateButton_.setToggleState(active, juce::dontSendNotification);
}

void StepCell::setValue(float val)
{
    valueSlider_.setValue(static_cast<double>(val), juce::dontSendNotification);
}

} // namespace ana
