#pragma once

#include "../../PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ana
{

class TransportBar : public juce::Component
{
public:
    explicit TransportBar(AnaPlugAudioProcessor& processor);

    void resized() override;

    void updatePitchDisplay(const juce::String& text);
    static juce::String midiNoteToName(int note);

    juce::TextButton& getLoadButton() noexcept     { return loadButton_; }
    juce::TextButton& getPlayButton() noexcept     { return playButton_; }
    juce::TextButton& getStopButton() noexcept     { return stopButton_; }
    juce::TextButton& getFlattenButton() noexcept  { return flattenButton_; }
    juce::Slider& getRootNoteSlider() noexcept     { return rootNoteKnob_; }
    juce::Slider& getRootFineTuneSlider() noexcept { return rootFineTuneKnob_; }

private:
    AnaPlugAudioProcessor& processor_;
    juce::TextButton loadButton_{"LOAD"};
    juce::TextButton playButton_{">"};
    juce::TextButton stopButton_{"#"};
    juce::TextButton flattenButton_{"FLATTEN"};
    juce::Slider rootNoteKnob_;
    juce::Slider rootFineTuneKnob_;
    juce::Label  rootNoteLabel_;
    juce::Label  rootFineTuneLabel_;
    juce::Label  pitchDetectLabel_;
};

} // namespace ana
