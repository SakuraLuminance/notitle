#pragma once

#include "../../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{

class MasterSection : public juce::Component
{
public:
    explicit MasterSection(AnaPlugAudioProcessor& processor);

    void resized() override;

    juce::Slider& getVolumeSlider() noexcept { return volSlider_; }
    juce::Slider& getPanSlider() noexcept    { return panSlider_; }

private:
    AnaPlugAudioProcessor& processor_;
    juce::Slider volSlider_;
    juce::Slider panSlider_;
    juce::Label  volLabel_;
    juce::Label  panLabel_;
};

} // namespace ana
