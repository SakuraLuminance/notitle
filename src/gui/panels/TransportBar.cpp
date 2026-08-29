#include "TransportBar.h"
#include "PanelWidgets.h"
#include "../CyberpunkTheme.h"
#include <cmath>

namespace ana
{

TransportBar::TransportBar(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    panelwidgets::cyberButton(*this, loadButton_);
    loadButton_.setTooltip("Load sample (WAV file)");

    panelwidgets::cyberButton(*this, playButton_);
    playButton_.setTooltip("Play/pause");
    playButton_.onClick = [this]()
    {
        processor_.startPlayback();
        stopButton_.setEnabled(true);
        playButton_.setEnabled(false);
    };
    playButton_.setEnabled(false);

    panelwidgets::cyberButton(*this, stopButton_);
    stopButton_.setTooltip("Stop");
    stopButton_.onClick = [this]()
    {
        processor_.stopPlayback();
        playButton_.setEnabled(true);
        stopButton_.setEnabled(false);
    };
    stopButton_.setEnabled(false);

    panelwidgets::cyberButton(*this, flattenButton_);
    flattenButton_.setTooltip("Flatten to audio");
    flattenButton_.setEnabled(false);

    panelwidgets::cyberKnob(*this, rootNoteKnob_, rootNoteLabel_, "ROOT", 0.0, 127.0, 60.0, 1.0);
    rootNoteKnob_.setTooltip("Root note (0-127)");
    rootNoteKnob_.textFromValueFunction = [](double v) { return midiNoteToName(static_cast<int>(v)); };
    rootNoteKnob_.setDoubleClickReturnValue(true, 60.0);
    rootNoteKnob_.onValueChange = [this]() {
        processor_.setRootNote(static_cast<int>(rootNoteKnob_.getValue()));
        updatePitchDisplay(midiNoteToName(static_cast<int>(rootNoteKnob_.getValue())));
    };

    panelwidgets::cyberKnob(*this, rootFineTuneKnob_, rootFineTuneLabel_, "FINE", -50.0, 50.0, 0.0, 1.0);
    rootFineTuneKnob_.setTooltip("Fine tune (-50 to +50 cents)");
    rootFineTuneKnob_.setDoubleClickReturnValue(true, 0.0);
    rootFineTuneKnob_.onValueChange = [this]() {
        processor_.setRootFineTune(static_cast<float>(rootFineTuneKnob_.getValue()));
    };

    pitchDetectLabel_.setText("--", juce::dontSendNotification);
    pitchDetectLabel_.setFont(CyberpunkTheme::getCyberFont(10.0f, false));
    pitchDetectLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::yellow_);
    addAndMakeVisible(pitchDetectLabel_);
}

void TransportBar::resized()
{
    const int pad = 3;
    auto smpArea = getLocalBounds().reduced(pad);
    auto smpTop = smpArea.removeFromTop(22).reduced(pad);
    const int btnW = smpTop.getWidth() / 4;
    auto loadRect = smpTop.removeFromLeft(btnW).reduced(pad);
    loadButton_.setBounds(loadRect);
    auto playRect = smpTop.removeFromLeft(btnW).reduced(1, pad);
    playButton_.setBounds(playRect);
    auto stopRect = smpTop.removeFromLeft(btnW).reduced(1, pad);
    stopButton_.setBounds(stopRect);
    auto flattenRect = smpTop.reduced(pad);
    flattenButton_.setBounds(flattenRect);

    pitchDetectLabel_.setBounds(smpArea.removeFromTop(14).reduced(pad));
    auto rootArea = smpArea.reduced(pad);
    const int rkW = rootArea.getWidth() / 2;

    auto rootCell1 = rootArea.removeFromLeft(rkW).reduced(2);
    rootNoteKnob_.setBounds(rootCell1.removeFromTop(rootCell1.getWidth()));
    rootNoteLabel_.setBounds(rootCell1);

    auto rootCell2 = rootArea.reduced(2);
    rootFineTuneKnob_.setBounds(rootCell2.removeFromTop(rootCell2.getWidth()));
    rootFineTuneLabel_.setBounds(rootCell2);
}

void TransportBar::updatePitchDisplay(const juce::String& text)
{
    float freq = 440.0f * std::pow(2.0f, (processor_.getRootNote() - 69) / 12.0f);
    pitchDetectLabel_.setText(text + " " + juce::String(freq, 1) + "Hz",
                              juce::dontSendNotification);
}

juce::String TransportBar::midiNoteToName(int note)
{
    note = juce::jlimit(0, 127, note);
    static const char* nn[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String(nn[note % 12]) + juce::String(note / 12 - 1);
}

} // namespace ana
