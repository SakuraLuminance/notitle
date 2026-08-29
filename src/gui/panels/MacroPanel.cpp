#include "MacroPanel.h"
#include "../CyberpunkTheme.h"
#include <cmath>

namespace ana
{

MacroPanel::MacroPanel(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    static const char* macroNames[] = { "M1", "M2", "M3", "M4" };
    for (int i = 0; i < 4; ++i)
    {
        macroSliders_[i].setRange(0.0, 1.0, 0.01);
        macroSliders_[i].setValue(0.0);
        macroSliders_[i].setDoubleClickReturnValue(true, 0.0);
        macroSliders_[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        macroSliders_[i].setTooltip("Macro " + juce::String(i + 1) + " (0-100%)");
        addAndMakeVisible(macroSliders_[i]);

        macroLabels_[i].setText(macroNames[i], juce::dontSendNotification);
        macroLabels_[i].setFont(CyberpunkTheme::getCyberFont(9.0f, false));
        macroLabels_[i].setColour(juce::Label::textColourId,
                                  CyberpunkTheme::fg_.withAlpha(0.8f));
        macroLabels_[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(macroLabels_[i]);

        // Push slider changes to MacroController
        const int idx = i;
        macroSliders_[i].onValueChange = [this, idx]()
        {
            processor_.getMacroController().setMacroValue(
                idx, static_cast<float>(macroSliders_[idx].getValue()));
        };
    }
}

void MacroPanel::resized()
{
    const int pad = 3;
    auto macroArea = getLocalBounds().reduced(pad);
    auto macroTitle = macroArea.removeFromTop(14);
    juce::ignoreUnused(macroTitle);
    auto mkArea = macroArea.reduced(pad);
    const int mkW = mkArea.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = mkArea.removeFromLeft(mkW).reduced(2);
        macroSliders_[i].setBounds(cell.removeFromTop(cell.getWidth()));
        macroLabels_[i].setBounds(cell);
    }
}

void MacroPanel::updateFromController()
{
    auto& macroCtrl = processor_.getMacroController();
    for (int i = 0; i < 4; ++i)
    {
        const auto data = macroCtrl.getVisualData(i);

        // Sync slider position if it differs from the controller value
        const double currentSlider = macroSliders_[i].getValue();
        if (std::abs(currentSlider - static_cast<double>(data.value)) > 0.001)
        {
            macroSliders_[i].setValue(static_cast<double>(data.value),
                                      juce::dontSendNotification);
        }

        // Update the curve exponent for ring colour
        macroSliders_[i].setCurveExponent(data.curveExponent);
        macroSliders_[i].repaint();
    }
}

} // namespace ana
