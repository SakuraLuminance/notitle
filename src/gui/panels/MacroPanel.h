#pragma once

#include "../../PluginProcessor.h"
#include "../MacroKnob.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{

class MacroPanel : public juce::Component
{
public:
    explicit MacroPanel(AnaPlugAudioProcessor& processor);

    void resized() override;
    void updateFromController();

    juce::Slider& getMacroSlider(int index) noexcept { return macroSliders_[(size_t) index]; }

private:
    AnaPlugAudioProcessor& processor_;
    std::array<MacroKnob, 4> macroSliders_;
    std::array<juce::Label, 4> macroLabels_;
};

} // namespace ana
