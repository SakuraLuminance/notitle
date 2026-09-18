#include "EffectParamPanel.h"
#include "CyberpunkTheme.h"
#include <cmath>

namespace ana
{

EffectParamPanel::EffectParamPanel(EffectBase* liveEffect)
    : effect_(liveEffect)
{
    setInterceptsMouseClicks(true, true);
    rebuildControls();
}

void EffectParamPanel::rebuildControls()
{
    knobs_.clear();
    labels_.clear();
    menus_.clear();
    menuLabels_.clear();
    knobParamIndex_.clear();
    menuParamIndex_.clear();

    if (effect_ == nullptr)
        return;

    const int n = effect_->getNumParams();
    for (int i = 0; i < n; ++i)
    {
        const auto& spec = effect_->getParamSpec(i);

        // Enumerations are menus, not knobs: a discrete choice has no useful
        // rotary position and no meaningful read-out.
        if (spec.isChoice())
        {
            const auto choices = spec.getChoiceLabels();

            auto menu = std::make_unique<juce::ComboBox>();
            for (int c = 0; c < choices.size(); ++c)
                menu->addItem(choices[c], c + 1);
            menu->setSelectedId(juce::jlimit(1, juce::jmax(1, choices.size()),
                                             static_cast<int>(std::lround(effect_->getParamValue(i))) + 1),
                               juce::dontSendNotification);
            menu->setTooltip(juce::String(spec.label != nullptr ? spec.label : "")
                             + "\n" + spec.values);

            menu->onChange = [this, i, menuPtr = menu.get()]()
            {
                if (effect_ != nullptr)
                    effect_->setParamValue(i, static_cast<float>(menuPtr->getSelectedId() - 1));
            };

            addAndMakeVisible(menu.get());
            menus_.add(std::move(menu));
            menuParamIndex_.push_back(i);

            auto menuLabel = std::make_unique<juce::Label>();
            menuLabel->setText(spec.label != nullptr ? spec.label : "", juce::dontSendNotification);
            menuLabel->setFont(CyberpunkTheme::getCyberFont(9.0f, true));
            menuLabel->setColour(juce::Label::textColourId, CyberpunkTheme::cyan_.withAlpha(0.85f));
            menuLabel->setJustificationType(juce::Justification::centredLeft);
            menuLabel->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(menuLabel.get());
            menuLabels_.add(std::move(menuLabel));
            continue;
        }

        auto knob = std::make_unique<juce::Slider>();
        knob->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob->setRange(spec.min, spec.max, spec.isInt ? 1.0 : 0.0);
        knob->setSkewFactor(juce::jlimit(0.1f, 10.0f, spec.skew));
        knob->setDoubleClickReturnValue(true, spec.def);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob->setValue(effect_->getParamValue(i), juce::dontSendNotification);
        knob->setColour(juce::Slider::rotarySliderFillColourId, CyberpunkTheme::cyan_);
        knob->setColour(juce::Slider::thumbColourId, CyberpunkTheme::cyan_);
        knob->setColour(juce::Slider::rotarySliderOutlineColourId, CyberpunkTheme::bg_.brighter(0.2f));
        knob->setTooltip((spec.values != nullptr
            ? juce::String(spec.label) + "\n" + spec.values
            : juce::String(spec.label))
            + "\nRight-click: MIDI Learn");
        // The knob's own pointer, not an index: with enumeration menus in the
        // panel the knob order no longer matches the parameter order.
        auto* knobPtr = knob.get();
        knob->onValueChange = [this, i, knobPtr]()
        {
            if (effect_ != nullptr)
                effect_->setParamValue(i, static_cast<float>(knobPtr->getValue()));
        };
        addAndMakeVisible(knob.get());
        knobs_.add(std::move(knob));
        knobParamIndex_.push_back(i);

        auto label = std::make_unique<juce::Label>();
        label->setText(spec.label != nullptr ? spec.label : "", juce::dontSendNotification);
        label->setFont(CyberpunkTheme::getCyberFont(8.0f, false));
        label->setColour(juce::Label::textColourId, CyberpunkTheme::fg_.withAlpha(0.8f));
        label->setJustificationType(juce::Justification::centred);
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label.get());
        labels_.add(std::move(label));
    }
}

void EffectParamPanel::visitKnobs(const std::function<void(int, juce::Slider&)>& fn)
{
    if (! fn)
        return;

    for (int i = 0; i < knobs_.size(); ++i)
        if (auto* knob = knobs_[i])
            fn(knobParamIndex_[(size_t) i], *knob);
}

void EffectParamPanel::visitMenus(const std::function<void(int, juce::ComboBox&)>& fn)
{
    if (! fn)
        return;

    for (int i = 0; i < menus_.size(); ++i)
        if (auto* menu = menus_[i])
            fn(menuParamIndex_[(size_t) i], *menu);
}

void EffectParamPanel::resized()
{
    if (knobs_.isEmpty() && menus_.isEmpty())
        return;

    auto area = getLocalBounds();

    const int perRow = juce::jmax(1, area.getWidth() / knobW);
    const int n      = knobs_.size();
    const int rows   = (n + perRow - 1) / perRow;

    // Menus and knob rows share whatever height we were given.  When a caller
    // hands out less than getPreferredHeight() asked for, both blocks are
    // squeezed proportionally rather than letting the tail fall outside the
    // panel: a clipped control is invisible and therefore unreachable.
    const int menuBlock = menus_.size() * menuRowH;
    const int knobBlock = rows * rowH;
    const int needed    = menuBlock + knobBlock;
    const int available = area.getHeight();

    int menuRowHeight = menuRowH;
    int knobRowHeight = rowH;

    if (needed > available && needed > 0)
    {
        // Scale each *unit* height by the same fraction, so the blocks keep
        // their proportions and the total lands on the space available.
        const auto share = [available, needed](int natural, int floor)
        {
            const auto scaled = static_cast<int>((static_cast<juce::int64>(natural)
                                                  * available) / needed);
            return juce::jmax(floor, scaled);
        };

        menuRowHeight = share(menuRowH, 10);
        knobRowHeight = share(rowH, 18);
    }

    // Enumeration menus first, one per row: they need the full width to show
    // their labels, which a 46 px knob column cannot do.
    for (int i = 0; i < menus_.size(); ++i)
    {
        auto row = area.removeFromTop(menuRowHeight).reduced(2, 1);
        menuLabels_.getUnchecked(i)->setBounds(row.removeFromLeft(menuLabelW));
        menus_.getUnchecked(i)->setBounds(row);
    }

    if (knobs_.isEmpty())
        return;

    for (int i = 0; i < n; ++i)
    {
        const int row = i / perRow;
        const int col = i % perRow;
        auto cell = juce::Rectangle<int>(area.getX() + col * knobW,
                                         area.getY() + row * knobRowHeight,
                                         knobW, knobRowHeight);
        knobs_.getUnchecked(i)->setBounds(cell.removeFromTop(
            juce::jmax(6, knobRowHeight - 14)));
        labels_.getUnchecked(i)->setBounds(cell);
    }
}

int EffectParamPanel::getPreferredHeight(int width) const
{
    if (effect_ == nullptr || (knobs_.isEmpty() && menus_.isEmpty()))
        return 18;

    const int perRow = juce::jmax(1, width / knobW);
    const int rows = static_cast<int>(std::ceil(
        static_cast<float>(knobs_.size()) / static_cast<float>(perRow)));
    return menus_.size() * menuRowH + rows * rowH + 6;
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
