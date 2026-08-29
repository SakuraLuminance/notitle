#include "FilterPanel.h"
#include "../CyberpunkTheme.h"
#include <cmath>
#include <vector>

namespace ana
{

FilterPanel::FilterPanel(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    title_.setText("FILTER", juce::dontSendNotification);
    title_.setFont(CyberpunkTheme::getCyberFont(11.0f, true));
    title_.setColour(juce::Label::textColourId, CyberpunkTheme::cyan_);
    addAndMakeVisible(title_);

    typeCombo_.addItem("LP", 1); typeCombo_.addItem("HP", 2);
    typeCombo_.addItem("BP", 3); typeCombo_.addItem("Notch", 4);
    typeCombo_.addItem("Comb", 5);
    typeCombo_.setSelectedId(1);
    typeCombo_.setTooltip("Filter type: LP/HP/BP/Notch/Comb");
    addAndMakeVisible(typeCombo_);

    cutoffSlider_.setRange(20.0, 20000.0, 1.0);
    cutoffSlider_.setValue(1000.0);
    cutoffSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    cutoffSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    cutoffSlider_.setSkewFactor(0.3);
    cutoffSlider_.setDoubleClickReturnValue(true, 1000.0);
    cutoffSlider_.setTooltip("Filter cutoff frequency (20-20000Hz)");
    addAndMakeVisible(cutoffSlider_);

    resSlider_.setRange(0.0, 1.0, 0.01);
    resSlider_.setValue(0.3);
    resSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    resSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    resSlider_.setDoubleClickReturnValue(true, 0.3);
    resSlider_.setTooltip("Filter resonance (0-100%)");
    addAndMakeVisible(resSlider_);

    addAndMakeVisible(viz_);

    // Wire filter controls to MultiFilter
    typeCombo_.onChange = [this]() {
        auto& slot = processor_.getMultiFilter().getSlot(0);
        switch (typeCombo_.getSelectedId())
        {
            case 1: slot.type = FilterType::LowPass;   break;
            case 2: slot.type = FilterType::HighPass;  break;
            case 3: slot.type = FilterType::BandPass;  break;
            case 4: slot.type = FilterType::Notch;     break;
            case 5: slot.type = FilterType::Comb;      break;
            default: slot.type = FilterType::LowPass;  break;
        }
        processor_.getMultiFilter().markCoefficientsDirty();
    };
    cutoffSlider_.onValueChange = [this]() {
        processor_.getMultiFilter().getSlot(0).params.cutoff
            = cutoffSlider_.getValue();
        processor_.getMultiFilter().markCoefficientsDirty();
    };
    resSlider_.onValueChange = [this]() {
        processor_.getMultiFilter().getSlot(0).params.resonance
            = static_cast<float>(resSlider_.getValue());
        processor_.getMultiFilter().markCoefficientsDirty();
    };
}

void FilterPanel::resized()
{
    const int pad = 3;
    auto filterArea = getLocalBounds().reduced(pad);
    title_.setBounds(filterArea.removeFromTop(14));
    typeCombo_.setBounds(filterArea.removeFromTop(18).reduced(pad));
    cutoffSlider_.setBounds(filterArea.removeFromTop(16).reduced(pad));
    resSlider_.setBounds(filterArea.removeFromTop(16).reduced(pad));
    viz_.setBounds(filterArea.reduced(pad));
}

void FilterPanel::updateFrequencyResponse()
{
    // Generate log-spaced frequencies once (100 points, 20 Hz - 20 kHz)
    static const std::vector<float> vizFrequencies = []()
    {
        std::vector<float> freqs;
        freqs.reserve(100);
        constexpr float minF = 20.0f;
        constexpr float maxF = 20000.0f;
        for (int i = 0; i < 100; ++i)
            freqs.push_back(minF * std::pow(maxF / minF, i / 99.0f));
        return freqs;
    }();

    auto magnitudes = processor_.getMultiFilter().getFrequencyResponse(vizFrequencies);
    viz_.setFrequencyResponse(vizFrequencies, magnitudes);
}

} // namespace ana
