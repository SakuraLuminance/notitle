#pragma once

#include "../PluginProcessor.h"
#include "CyberpunkTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace ana {

//==============================================================================
/** A single step cell: gate toggle button + vertical value slider. */
class StepCell : public juce::Component
{
public:
    StepCell(int index, AnaPlugAudioProcessor& p);

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setActive(bool active);
    void setValue(float val);
    bool isActive() const { return gateButton_.getToggleState(); }
    float getValue() const { return static_cast<float>(valueSlider_.getValue()); }

    std::function<void(int, bool)> onGateChanged;
    std::function<void(int, float)> onValueChanged;

private:
    int index_;
    AnaPlugAudioProcessor& processor_;
    juce::ToggleButton gateButton_;
    juce::Slider valueSlider_;
};

} // namespace ana
