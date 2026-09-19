#include "TimbrePanel.h"
#include "PanelWidgets.h"

namespace ana
{

TimbrePanel::TimbrePanel(AnaPlugAudioProcessor& processor, bool isA)
    : processor_(processor), isA_(isA)
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

    // P6 Round 2: per-partial treatment for this side (A or B)
    brightSlider_.onValueChange = [this]() {
        processor_.setTimbreBright(isA_, static_cast<float>(brightSlider_.getValue()));
    };
    blurSlider_.onValueChange = [this]() {
        processor_.setTimbreBlur(isA_, static_cast<float>(blurSlider_.getValue()));
    };
    hpfSlider_.onValueChange = [this]() {
        processor_.setTimbreHpf(isA_, static_cast<float>(hpfSlider_.getValue()));
    };
}

void TimbrePanel::resized()
{
    const int pad = 3;
    auto area = getLocalBounds().reduced(6, pad * 2);

    // The knob was sized from the panel's width alone - half of it, which at
    // 900x660 asks for 136-pixel knobs in a column 288 pixels tall.  Two knob rows
    // plus the HPF row need 340, so the second row collapsed to zero height and the
    // audit found zero-height labels and sliders that were never placed at all
    // (0,0 0x0).  The height gets a say now.
    const int captionH = 14;
    const int hpfH = 16 + 12;
    const int rows = 2;
    const int reserved = captionH * rows + hpfH + pad * 4;
    const int knobSize = juce::jmax (24,
                            juce::jmin ((area.getWidth() - pad * 2) / 2,
                                        (area.getHeight() - reserved) / rows));
    auto row = [&](juce::Slider& s, juce::Label& l) {
        auto cell = area.removeFromTop(knobSize + captionH).reduced(pad);
        s.setBounds(cell.removeFromTop(knobSize));
        l.setBounds(cell);
    };
    row(subSlider_, subLabel_);
    row(brightSlider_, brightLabel_);
    hpfSlider_.setBounds(area.removeFromTop(16).reduced(pad));
    hpfLabel_.setBounds(area.removeFromTop(12).reduced(pad));
}

} // namespace ana
