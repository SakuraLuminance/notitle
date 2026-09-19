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

    // Four controls, not three.  The constructor makes SUB, BRIGHT, BLUR and HPF,
    // but resized() only ever placed SUB, BRIGHT and HPF - blurSlider_ and
    // blurLabel_ were never given a rectangle at all.  That single omission is what
    // the audit kept reporting as 0,0 0x0 sliders and labels and as "BLUR"
    // overflowing a zero-width label, on the page and on all eight view modes.
    //
    // Three rotary rows above a linear HPF strip now, with every height derived from
    // what the panel actually has, so no row can be handed an empty rectangle.
    const int captionH = 14;
    const int hpfH = 28;
    const int knobArea = juce::jmax (3 * (captionH + 8), area.getHeight() - hpfH);
    const int knobSize = juce::jmax (24,
                            juce::jmin ((area.getWidth() - pad * 2) / 2,
                                        knobArea / 3 - captionH));
    auto row = [&](juce::Slider& s, juce::Label& l) {
        auto cell = area.removeFromTop(knobSize + captionH).reduced(pad);
        s.setBounds(cell.removeFromTop(juce::jmin(knobSize, cell.getHeight())));
        l.setBounds(cell);
    };
    row(subSlider_, subLabel_);
    row(brightSlider_, brightLabel_);
    row(blurSlider_, blurLabel_);
    hpfSlider_.setBounds(area.removeFromTop(juce::jmin(16, area.getHeight())).reduced(pad));
    hpfLabel_.setBounds(area.removeFromTop(juce::jmin(12, area.getHeight())).reduced(pad));
}

} // namespace ana
