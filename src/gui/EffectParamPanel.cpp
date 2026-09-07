#include "EffectParamPanel.h"
#include "CyberpunkTheme.h"
#include <cmath>

namespace ana
{

EffectParamPanel::EffectParamPanel(EffectBase* liveEffect)
    : effect_(liveEffect)
{
    setInterceptsMouseClicks(true, true);
    rebuildKnobs();
}

void EffectParamPanel::rebuildKnobs()
{
    knobs_.clear();
    labels_.clear();

    if (effect_ == nullptr)
        return;

    const int n = effect_->getNumParams();
    knobs_.resize(static_cast<size_t>(n));
    labels_.resize(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i)
    {
        const auto& spec = effect_->getParamSpec(i);

        auto& knob = knobs_[(size_t) i];
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setRange(spec.min, spec.max, spec.isInt ? 1.0 : 0.0);
        knob.setSkewFactor(juce::jlimit(0.1f, 10.0f, spec.skew));
        knob.setDoubleClickReturnValue(true, spec.def);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob.setValue(effect_->getParamValue(i), juce::dontSendNotification);
        knob.setColour(juce::Slider::rotarySliderFillColourId, CyberpunkTheme::cyan_);
        knob.setColour(juce::Slider::thumbColourId, CyberpunkTheme::cyan_);
        knob.setColour(juce::Slider::outlineColourId, CyberpunkTheme::bg_.brighter(0.2f));
        knob.setTooltip(spec.label);
        const int idx = i;
        knob.onValueChange = [this, idx]()
        {
            if (effect_ != nullptr)
                effect_->setParamValue(idx, static_cast<float>(knobs_[(size_t) idx].getValue()));
        };
        addAndMakeVisible(knob);

        auto& label = labels_[(size_t) i];
        label.setText(spec.label != nullptr ? spec.label : "", juce::dontSendNotification);
        label.setFont(CyberpunkTheme::getCyberFont(8.0f, false));
        label.setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.8f));
        label.setJustificationType(juce::Justification::centred);
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
    }
}

void EffectParamPanel::resized()
{
    if (knobs_.empty())
        return;

    auto area = getLocalBounds();
    const int perRow = juce::jmax(1, area.getWidth() / knobW);
    const int n = static_cast<int>(knobs_.size());

    for (int i = 0; i < n; ++i)
    {
        const int row = i / perRow;
        const int col = i % perRow;
        auto cell = juce::Rectangle<int>(area.getX() + col * knobW,
                                         area.getY() + row * rowH,
                                         knobW, rowH);
        knobs_[(size_t) i].setBounds(cell.removeFromTop(rowH - 14));
        labels_[(size_t) i].setBounds(cell);
    }
}

int EffectParamPanel::getPreferredHeight(int width) const
{
    if (effect_ == nullptr || knobs_.empty())
        return 18;

    const int perRow = juce::jmax(1, width / knobW);
    const int rows = static_cast<int>(std::ceil(
        static_cast<float>(knobs_.size()) / static_cast<float>(perRow)));
    return rows * rowH + 6;
}

void EffectParamPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour(CyberpunkTheme::bg_.brighter(0.06f));
    g.fillRect(bounds);
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.10f));
    g.drawRect(bounds.toFloat(), 1.0f);

    if (effect_ == nullptr || effect_->getNumParams() == 0)
    {
        g.setColour(CyberpunkTheme::fg_.withAlpha(0.35f));
        g.setFont(CyberpunkTheme::getCyberFont(10.0f, false));
        g.drawText("No editable params", bounds, juce::Justification::centred);
    }
}

} // namespace ana
