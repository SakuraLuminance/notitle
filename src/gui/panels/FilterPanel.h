#pragma once

#include "../../PluginProcessor.h"
#include "../FilterVisualization.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{

class FilterPanel : public juce::Component
{
public:
    explicit FilterPanel(AnaPlugAudioProcessor& processor);

    void resized() override;
    void updateFrequencyResponse();

    /** Pulls cutoff / resonance / type back out of the multi-filter.  The panel
        only pushes values on user gestures, so without this a preset load left the
        sliders showing the previous preset while the engine ran the new one. */
    void syncFromProcessor();

    juce::Slider& getCutoffSlider() noexcept    { return cutoffSlider_; }
    juce::Slider& getResonanceSlider() noexcept { return resSlider_; }

private:
    AnaPlugAudioProcessor& processor_;
    juce::ComboBox typeCombo_;
    juce::Slider cutoffSlider_;
    juce::Slider resSlider_;
    FilterVisualization viz_;
    juce::Label title_;
};

} // namespace ana
