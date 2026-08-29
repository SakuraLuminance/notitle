#pragma once

#include "../../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{

class TimbrePanel : public juce::Component
{
public:
    TimbrePanel(AnaPlugAudioProcessor& processor, bool isA);

    void resized() override;

    juce::Slider& getSubSlider() noexcept    { return subSlider_; }
    juce::Slider& getBrightSlider() noexcept { return brightSlider_; }
    juce::Slider& getBlurSlider() noexcept   { return blurSlider_; }
    juce::Slider& getHpfSlider() noexcept    { return hpfSlider_; }

private:
    AnaPlugAudioProcessor& processor_;
    juce::Slider subSlider_;
    juce::Slider brightSlider_;
    juce::Slider blurSlider_;
    juce::Slider hpfSlider_;
    juce::Label  subLabel_, brightLabel_, blurLabel_, hpfLabel_;
};

} // namespace ana
