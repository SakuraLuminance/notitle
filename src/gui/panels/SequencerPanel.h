#pragma once

#include "../../PluginProcessor.h"
#include "../StepCell.h"
#include <array>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{

class SequencerPanel : public juce::Component
{
public:
    explicit SequencerPanel(AnaPlugAudioProcessor& processor);

    void resized() override;
    void updateFromSequencer();

    juce::Slider& getBpmSlider() noexcept  { return bpmSlider_; }
    juce::Slider& getRateSlider() noexcept { return rateSlider_; }

private:
    AnaPlugAudioProcessor& processor_;
    juce::Label title_;
    juce::ComboBox playModeCombo_;
    juce::ComboBox clockSourceCombo_;
    juce::Slider bpmSlider_;
    juce::Label  bpmLabel_;
    juce::Slider rateSlider_;
    juce::Label  rateLabel_;
    std::array<std::unique_ptr<StepCell>, 16> stepCells_;
    juce::Label currentStepLabel_;
};

} // namespace ana
