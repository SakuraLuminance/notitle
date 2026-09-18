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
      [GRAIN] [MIX ▬▬▬ 60%]  [JITTER ▬▬▬ 0%]
      SIZE  ▬▬▬  60 ms     DENSITY ▬▬▬  20 /s
      SPACE ▬▬▬  25%       PITCH   ▬▬▬   0 st
      WINDOW [HANN ▾]      MOD [OFF ▾]  DEPTH ▬▬▬  RATE ▬▬▬
      SPREAD ▬▬▬  0%       REVERSE ▬▬▬  0%
      [ sample envelope strip ][ grain cloud: x = position, y = age, w = size ]
      status: N GRAINS ACTIVE / NO SAMPLE LOADED

    The cloud is the page's feedback: density turns into more bars, size into
    wider ones, SPACE + MOD/DEPTH into the x spread, RATE into how the bars
    drift, and REVERSE into bars that read right-to-left (the bar is centred on
    the read head either way, so direction shows up in the audio, not the bar).
*/
class GranularPage : public juce::Component
{
public:
    explicit GranularPage(AnaPlugAudioProcessor& processor);

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Pulls the processor state into the controls (editor timer). */
    void syncFromProcessor();

    // The editor registers these for right-click MIDI Learn; the page itself
    // never talks to the mapping system.
    juce::Slider& getMixSlider()       noexcept { return mixSlider_; }
    juce::Slider& getSizeSlider()      noexcept { return sizeSlider_; }
    juce::Slider& getDensitySlider()   noexcept { return densitySlider_; }
    juce::Slider& getPositionSlider()  noexcept { return positionSlider_; }
    juce::Slider& getPitchSlider()     noexcept { return pitchSlider_; }
    juce::Slider& getModDepthSlider()  noexcept { return modDepthSlider_; }
    juce::Slider& getModRateSlider()   noexcept { return modRateSlider_; }
    juce::Slider& getSpreadSlider()    noexcept { return spreadSlider_; }
    juce::Slider& getReverseSlider()   noexcept { return reverseSlider_; }
    juce::Slider& getJitterSlider()    noexcept { return jitterSlider_; }

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

    juce::Label  spreadLabel_, reverseLabel_, jitterLabel_;
    juce::Slider spreadSlider_, reverseSlider_, jitterSlider_;
    juce::Label  spreadReadout_, reverseReadout_, jitterReadout_;

    juce::Label statusLabel_;

    // Grain cloud (written by the audio thread, copied under a seqlock).
    ana::GranularSynthesizer::GrainSnapshot cloud_[AnaPlugAudioProcessor::kGrainVisualMax];
    int cloudCount_ = 0;
    float peaks_[AnaPlugAudioProcessor::kGrainSourcePeakBuckets] = {};
    juce::Rectangle<int> cloudBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranularPage)
};

} // namespace ana
