#include "TimbrePanel.h"
#include "PanelWidgets.h"

namespace ana
{

TimbrePanel::TimbrePanel(AnaPlugAudioProcessor& processor, bool isA)
    : processor_(processor)
{
    panelwidgets::cyberKnob(*this, subSlider_, subLabel_,    "SUB",     0.0, 1.0, 0.0, 0.01);
    panelwidgets::cyberKnob(*this, brightSlider_, brightLabel_, "BRIGHT", 0.0, 1.0, 0.5, 0.01);
    panelwidgets::cyberKnob(*this, blurSlider_, blurLabel_,  "BLUR",    0.0, 1.0, 0.0, 0.01);
    panelwidgets::cyberKnob(*this, hpfSlider_, hpfLabel_,    "HPF",     20.0, 20000.0, 20.0, 1.0,
                            juce::Slider::LinearHorizontal);
    hpfSlider_.setSkewFactor(0.3);
    subSlider_.setTooltip("Sub level (0-100%)");
    brightSlider_.setTooltip("Brightness (0-100%)");
    blurSlider_.setTooltip("Blur amount (0-100%)");
    hpfSlider_.setTooltip("HPF cutoff (20-20000Hz)");

    if (isA)
    {
        subSlider_.onValueChange = [this]() {
            processor_.setSubHarmonicLevel(static_cast<float>(subSlider_.getValue()));
        };
    }
    else
    {
        subSlider_.onValueChange = [this]() {
            processor_.getSubHarmonicGenerator().setSubLevel(1,
                static_cast<float>(subSlider_.getValue()));
        };
    }
}

void TimbrePanel::resized()
{
    const int pad = 3;
    auto area = getLocalBounds().reduced(6, pad * 2);
    const int knobSize = (area.getWidth() - pad * 2) / 2;
    auto row = [&](juce::Slider& s, juce::Label& l) {
        auto cell = area.removeFromTop(knobSize + 14).reduced(pad);
        s.setBounds(cell.removeFromTop(knobSize));
        l.setBounds(cell);
    };
    row(subSlider_, subLabel_);
    row(brightSlider_, brightLabel_);
    hpfSlider_.setBounds(area.removeFromTop(16).reduced(pad));
    hpfLabel_.setBounds(area.removeFromTop(12).reduced(pad));
}

} // namespace ana
