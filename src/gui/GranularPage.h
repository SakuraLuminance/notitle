#pragma once

#include "../PluginProcessor.h"
#include "CyberpunkTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{

//==============================================================================
/**
    GRAIN page (P7): the control surface for the granular layer.

    GranularSynthesizer was implemented and compiled from the start but had no
    UI and no audio path at all.  This page drives the processor-side atomics
    that PluginProcessor::renderGranularLayer() pushes into the engine once per
    block, so every control here is audible.

    Layout (top to bottom, kControlHeight rows):
      [GRAIN] [MIX ▬▬▬ 60%]
      SIZE  ▬▬▬  60 ms     DENSITY ▬▬▬  20 /s
      SPACE ▬▬▬  25%       PITCH   ▬▬▬   0 st
      WINDOW [HANN ▾]      MOD [OFF ▾]  DEPTH ▬▬▬  RATE ▬▬▬
      status: N GRAINS ACTIVE / NO SAMPLE LOADED
*/
class GranularPage : public juce::Component
{
public:
    explicit GranularPage(AnaPlugAudioProcessor& processor);

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Pulls the processor state into the controls (editor timer). */
    void syncFromProcessor();

private:
    void addReadout(juce::Label& readout, const juce::String& tooltip);
    void layoutSliderRow(juce::Rectangle<int>& area,
                         juce::Label& label, juce::Slider& slider, juce::Label& readout);

    AnaPlugAudioProcessor& processor_;

    juce::TextButton enableButton_{ "GRAIN" };
    juce::Slider mixSlider_;
    juce::Label  mixReadout_;

    juce::Label  sizeLabel_, densityLabel_, positionLabel_, pitchLabel_;
    juce::Slider sizeSlider_, densitySlider_, positionSlider_, pitchSlider_;
    juce::Label  sizeReadout_, densityReadout_, positionReadout_, pitchReadout_;

    juce::Label  windowLabel_, modLabel_, modDepthLabel_, modRateLabel_;
    juce::ComboBox windowCombo_, modCombo_;
    juce::Slider modDepthSlider_, modRateSlider_;
    juce::Label  modDepthReadout_, modRateReadout_;

    juce::Label statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranularPage)
};

} // namespace ana
