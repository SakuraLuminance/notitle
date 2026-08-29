#include "MasterSection.h"
#include "PanelWidgets.h"

namespace ana
{

MasterSection::MasterSection(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    panelwidgets::cyberKnob(*this, volSlider_, volLabel_, "VOL", 0.0, 2.0, 0.8, 0.01,
                            juce::Slider::RotaryVerticalDrag);
    volSlider_.setTooltip("Master Volume (0-200%)");
    volSlider_.onValueChange = [this]()
    {
        processor_.setMasterVol(static_cast<float>(volSlider_.getValue()));
    };

    panelwidgets::cyberKnob(*this, panSlider_, panLabel_, "PAN", -1.0, 1.0, 0.0, 0.01,
                            juce::Slider::RotaryVerticalDrag);
    panSlider_.setTooltip("Pan: stereo balance (-100% to +100%)");
    panSlider_.onValueChange = [this]()
    {
        processor_.setMasterPan(static_cast<float>(panSlider_.getValue()));
    };
}

void MasterSection::resized()
{
    const int pad = 3;
    auto mstArea = getLocalBounds().reduced(pad);
    mstArea = mstArea.withWidth(juce::jmin(mstArea.getWidth(), 150));
    mstArea.removeFromTop(4);
    {
        auto volRow = mstArea.removeFromTop(50).reduced(2, 0);
        volSlider_.setBounds(volRow.removeFromLeft(volRow.getWidth() - 28));
        volLabel_.setBounds(volRow);
        volLabel_.setJustificationType(juce::Justification::centredRight);
    }
    {
        auto panRow = mstArea.removeFromTop(50).reduced(2, 0);
        panSlider_.setBounds(panRow.removeFromLeft(panRow.getWidth() - 28));
        panLabel_.setBounds(panRow);
        panLabel_.setJustificationType(juce::Justification::centredRight);
    }
}

} // namespace ana
