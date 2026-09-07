#include "EffectRackComponent.h"
#include "../dsp/Crumb.h"
#include "../dsp/EffectParamRegistry.h"

namespace ana {

//==============================================================================
// Effect slot constants
static constexpr int slotHeight = 30;
static constexpr int buttonSize = 14;
static constexpr int bypassW = 30;
static constexpr int labelW = 62;
static constexpr int removeW = 16;
static constexpr int sliderMinW = 28;

//==============================================================================
// Effect type registry — delegated to the test-safe EffectParamRegistry so
// rack-added and preset-loaded slots share the same adapters/param tables.
juce::StringArray EffectSlotWidget::getAvailableEffectTypes()
{
    return EffectParamRegistry::getTypeNames();
}

//==============================================================================
// Effect factory for dynamic addition
std::unique_ptr<EffectBase> EffectSlotWidget::createEffectByName(const juce::String& typeName)
{
    return EffectParamRegistry::create(typeName);
}

//==============================================================================
// EffectSlotWidget
//==============================================================================
EffectSlotWidget::EffectSlotWidget(AnaPlugAudioProcessor& processor, int slotIndex, bool isLast)
    : processor_(processor), slotIndex_(slotIndex), isLast_(isLast)
{
    // --- Up / Down reorder buttons ---
    upButton_.setButtonText("▲");
    upButton_.setTooltip("Move effect up");
    upButton_.setColour(juce::TextButton::buttonColourId, CyberpunkTheme::cyan_.withAlpha(0.3f));
    upButton_.setColour(juce::TextButton::textColourOffId, CyberpunkTheme::fg_.withAlpha(0.6f));
    upButton_.onClick = [this] { if (onMoveUp) onMoveUp(slotIndex_); };
    addAndMakeVisible(upButton_);

    downButton_.setButtonText("▼");
    downButton_.setTooltip("Move effect down");
    downButton_.setColour(juce::TextButton::buttonColourId, CyberpunkTheme::cyan_.withAlpha(0.3f));
    downButton_.setColour(juce::TextButton::textColourOffId, CyberpunkTheme::fg_.withAlpha(0.6f));
    downButton_.onClick = [this] { if (onMoveDown) onMoveDown(slotIndex_); };
    addAndMakeVisible(downButton_);

    // --- Effect type label ---
    typeLabel_.setFont(CyberpunkTheme::getCyberFont(10.0f, true));
    typeLabel_.setColour(juce::Label::textColourId, CyberpunkTheme::cyan_);
    typeLabel_.setJustificationType(juce::Justification::centredLeft);
    typeLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(typeLabel_);

    // --- Bypass toggle button ---
    bypassButton_.setButtonText("BYP");
    bypassButton_.setTooltip("Toggle effect bypass");
    bypassButton_.setClickingTogglesState(true);
    bypassButton_.setColour(juce::TextButton::buttonColourId, CyberpunkTheme::cyan_.darker(0.6f));
    bypassButton_.setColour(juce::TextButton::buttonOnColourId, CyberpunkTheme::yellow_);
    bypassButton_.setColour(juce::TextButton::textColourOffId, CyberpunkTheme::fg_.withAlpha(0.7f));
    bypassButton_.setColour(juce::TextButton::textColourOnId, CyberpunkTheme::bg_);
    bypassButton_.onClick = [this]() {
        const bool bypassed = bypassButton_.getToggleState();
        processor_.getEffectsChain().bypassEffect(slotIndex_, bypassed);
    };
    addAndMakeVisible(bypassButton_);

    // --- Mix slider ---
    mixSlider_.setRange(0.0, 1.0, 0.01);
    mixSlider_.setValue(1.0);
    mixSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    mixSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mixSlider_.setDoubleClickReturnValue(true, 1.0);
    mixSlider_.setTooltip("Wet/Dry mix");
    mixSlider_.onValueChange = [this]() {
        processor_.getEffectsChain().setMix(slotIndex_,
            static_cast<float>(mixSlider_.getValue()));
    };
    addAndMakeVisible(mixSlider_);

    // --- Low-cut slider ---
    lowCutSlider_.setRange(20.0, 20000.0, 1.0);
    lowCutSlider_.setValue(20.0);
    lowCutSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    lowCutSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    lowCutSlider_.setSkewFactor(0.3);
    lowCutSlider_.setDoubleClickReturnValue(true, 20.0);
    lowCutSlider_.setTooltip("Low cut frequency");
    lowCutSlider_.onValueChange = [this]() {
        processor_.getEffectsChain().setWetLowCut(slotIndex_,
            static_cast<float>(lowCutSlider_.getValue()));
    };
    addAndMakeVisible(lowCutSlider_);

    // --- High-cut slider ---
    highCutSlider_.setRange(20.0, 20000.0, 1.0);
    highCutSlider_.setValue(20000.0);
    highCutSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    highCutSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    highCutSlider_.setSkewFactor(0.3);
    highCutSlider_.setDoubleClickReturnValue(true, 20000.0);
    highCutSlider_.setTooltip("High cut frequency");
    highCutSlider_.onValueChange = [this]() {
        processor_.getEffectsChain().setWetHighCut(slotIndex_,
            static_cast<float>(highCutSlider_.getValue()));
    };
    addAndMakeVisible(highCutSlider_);

    // --- Expand / collapse (replaces the old decorative mod slider) ---
    expandButton_.setButtonText("\u25BE");
    expandButton_.setTooltip("Show effect parameters");
    expandButton_.setColour(juce::TextButton::buttonColourId, CyberpunkTheme::cyan_.withAlpha(0.15f));
    expandButton_.setColour(juce::TextButton::textColourOffId, CyberpunkTheme::cyan_);
    expandButton_.onClick = [this] { setExpanded(!expanded_); };
    addAndMakeVisible(expandButton_);

    // --- Remove button ---
    removeButton_.setButtonText("✕");
    removeButton_.setTooltip("Remove this effect");
    removeButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    removeButton_.setColour(juce::TextButton::textColourOffId, CyberpunkTheme::fg_.withAlpha(0.5f));
    removeButton_.onClick = [this] { if (onRemove) onRemove(slotIndex_); };
    addAndMakeVisible(removeButton_);

    // Sync from processor state
    syncFromProcessor();
}

//==============================================================================
void EffectSlotWidget::setSlotIndex(int index)
{
    slotIndex_ = index;
}

//==============================================================================
void EffectSlotWidget::setExpanded(bool expand)
{
    if (expanded_ == expand)
        return;

    expanded_ = expand;
    expandButton_.setButtonText(expanded_ ? "\u25B8" : "\u25BE");
    expandButton_.setTooltip(expanded_ ? "Hide effect parameters" : "Show effect parameters");

    if (expanded_)
    {
        auto& chain = processor_.getEffectsChain();
        if (slotIndex_ >= 0 && slotIndex_ < chain.getNumEffects())
        {
            paramPanel_ = std::make_unique<EffectParamPanel>(&chain.getEffect(slotIndex_));
            addAndMakeVisible(paramPanel_.get());
        }
    }
    else
    {
        paramPanel_.reset();
    }

    resized();
    if (onExpandToggled)
        onExpandToggled(slotIndex_);
}

//==============================================================================
void EffectSlotWidget::layoutHeader(juce::Rectangle<int> area)
{
    // Up/Down buttons (fixed width at left)
    auto moveArea = area.removeFromLeft(buttonSize * 2 + 2);
    upButton_.setBounds(moveArea.removeFromLeft(buttonSize).reduced(0, 4));
    downButton_.setBounds(moveArea.removeFromLeft(buttonSize).reduced(0, 4));

    // Remove button (fixed width at right)
    removeButton_.setBounds(area.removeFromRight(removeW + 2).reduced(0, 6));

    // Bypass button (fixed width)
    bypassButton_.setBounds(area.removeFromLeft(bypassW + 2).reduced(0, 4));

    // Type label (fixed width)
    typeLabel_.setBounds(area.removeFromLeft(labelW).reduced(2, 4));

    // Remaining space → distribute among sliders with minimum widths
    const int remainingW = area.getWidth();
    const int sliderCount = 3;
    const int minTotal = sliderMinW * sliderCount;
    const int sliderW = std::max(minTotal, remainingW) / sliderCount;

    // Expand toggle (rightmost)
    expandButton_.setBounds(area.removeFromRight(22).reduced(1, 6));

    // HI (next from right)
    highCutSlider_.setBounds(area.removeFromRight(sliderW).reduced(1, 6));

    // LO (next from right)
    lowCutSlider_.setBounds(area.removeFromRight(sliderW).reduced(1, 6));

    // Mix (remaining, leftmost of sliders)
    mixSlider_.setBounds(area.reduced(1, 6));
}

//==============================================================================
void EffectSlotWidget::resized()
{
    auto area = getLocalBounds().reduced(1, 0);

    if (expanded_)
    {
        auto header = area.removeFromTop(slotHeight);
        layoutHeader(header);
        if (paramPanel_ != nullptr)
            paramPanel_->setBounds(area);
    }
    else
    {
        layoutHeader(area);
    }
}

//==============================================================================
void EffectSlotWidget::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Background — subtle row tint
    g.setColour(CyberpunkTheme::bg_.brighter((slotIndex_ % 2 == 0) ? 0.04f : 0.0f));
    g.fillRect(bounds);

    // Bottom separator
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.08f));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0, bounds.getWidth());

    // Slot index label at far left of type area (dim)
    if (isLast_) {
        g.setColour(CyberpunkTheme::yellow_.withAlpha(0.15f));
        g.fillRect(bounds);
    }

    // Bypass indicator glow when active (non-bypassed = active)
    const bool bypassed = bypassButton_.getToggleState();
    if (!bypassed) {
        g.setColour(CyberpunkTheme::cyan_.withAlpha(0.04f));
        g.fillRoundedRectangle(bounds.toFloat().reduced(1.0f), 2.0f);
    }
}

//==============================================================================
void EffectSlotWidget::syncFromProcessor()
{
    auto& chain = processor_.getEffectsChain();
    const int numEffects = chain.getNumEffects();

    if (slotIndex_ < 0 || slotIndex_ >= numEffects)
        return;

    auto& slot = chain.getEffect(slotIndex_);

    // Type label
    typeLabel_.setText(slot.name, juce::dontSendNotification);

    // Bypass
    bypassButton_.setToggleState(slot.bypassed, juce::dontSendNotification);

    // Mix
    {
        const double mixVal = static_cast<double>(slot.mix);
        if (std::abs(mixSlider_.getValue() - mixVal) > 0.001)
            mixSlider_.setValue(mixVal, juce::dontSendNotification);
    }

    // Low cut
    {
        const double lcVal = static_cast<double>(slot.wetLowCut);
        if (std::abs(lowCutSlider_.getValue() - lcVal) > 0.5)
            lowCutSlider_.setValue(lcVal, juce::dontSendNotification);
    }

    // High cut
    {
        const double hcVal = static_cast<double>(slot.wetHighCut);
        if (std::abs(highCutSlider_.getValue() - hcVal) > 0.5)
            highCutSlider_.setValue(hcVal, juce::dontSendNotification);
    }

    // Update up/down button enabled states
    upButton_.setEnabled(slotIndex_ > 0);
    downButton_.setEnabled(slotIndex_ < numEffects - 1);
}

//==============================================================================
// Undoable action for adding an effect slot
//==============================================================================
class EffectAddAction : public juce::UndoableAction
{
public:
    EffectAddAction(EffectsChain& chain, int slotIndex,
                    const juce::String& typeName)
        : chain_(chain), slotIndex_(slotIndex), typeName_(typeName) {}

    bool perform() override
    {
        if (!effectAdded_)
        {
            auto effect = EffectSlotWidget::createEffectByName(typeName_);
            if (effect)
            {
                chain_.addEffect(std::move(effect), typeName_);
                // Move to the original position if not at end
                const int lastIdx = chain_.getNumEffects() - 1;
                if (lastIdx > slotIndex_)
                    chain_.reorderEffects(lastIdx, slotIndex_);
            }
            effectAdded_ = true;
        }
        else
        {
            chain_.removeEffect(slotIndex_);
            effectAdded_ = false;
        }
        return true;
    }

    bool undo() override
    {
        if (!effectAdded_)
        {
            auto effect = EffectSlotWidget::createEffectByName(typeName_);
            if (effect)
            {
                chain_.addEffect(std::move(effect), typeName_);
                const int lastIdx = chain_.getNumEffects() - 1;
                if (lastIdx > slotIndex_)
                    chain_.reorderEffects(lastIdx, slotIndex_);
            }
            effectAdded_ = true;
        }
        else
        {
            chain_.removeEffect(slotIndex_);
            effectAdded_ = false;
        }
        return true;
    }

private:
    EffectsChain& chain_;
    int slotIndex_;
    juce::String typeName_;
    bool effectAdded_ = false;
};

//==============================================================================
// Undoable action for reordering effects
//==============================================================================
class EffectMoveAction : public juce::UndoableAction
{
public:
    EffectMoveAction(EffectsChain& chain, int fromIdx, int toIdx)
        : chain_(chain), fromIdx_(fromIdx), toIdx_(toIdx) {}

    bool perform() override
    {
        chain_.reorderEffects(fromIdx_, toIdx_);
        std::swap(fromIdx_, toIdx_);
        return true;
    }

    bool undo() override
    {
        chain_.reorderEffects(fromIdx_, toIdx_);
        std::swap(fromIdx_, toIdx_);
        return true;
    }

private:
    EffectsChain& chain_;
    int fromIdx_, toIdx_;
};

//==============================================================================
// Undoable action for removing an effect slot
//==============================================================================
class EffectRemoveAction : public juce::UndoableAction
{
public:
    EffectRemoveAction(EffectsChain& chain, int slotIndex)
        : chain_(chain), slotIndex_(slotIndex) {}

    bool perform() override
    {
        auto& slot = chain_.getEffect(slotIndex_);
        if (slot.effect)
        {
            effectState_ = slot.effect->getState();
            effectName_ = slot.name;
        }
        bypassed_     = slot.bypassed;
        mix_          = slot.mix;
        wetLowCut_    = slot.wetLowCut;
        wetHighCut_   = slot.wetHighCut;

        chain_.removeEffect(slotIndex_);
        return true;
    }

    bool undo() override
    {
        auto effect = EffectSlotWidget::createEffectByName(effectName_);
        if (effect && effectState_.isValid())
            effect->setState(effectState_);

        if (effect)
        {
            chain_.addEffect(std::move(effect), effectName_);
            const int lastIdx = chain_.getNumEffects() - 1;
            if (lastIdx > slotIndex_)
                chain_.reorderEffects(lastIdx, slotIndex_);
        }

        // Restore slot metadata
        auto& slot = chain_.getEffect(slotIndex_);
        slot.bypassed   = bypassed_;
        slot.mix        = mix_;
        slot.wetLowCut  = wetLowCut_;
        slot.wetHighCut = wetHighCut_;

        return true;
    }

private:
    EffectsChain& chain_;
    int slotIndex_;
    juce::ValueTree effectState_;
    juce::String effectName_;
    bool bypassed_     = false;
    float mix_          = 1.0f;
    float wetLowCut_    = 20.0f;
    float wetHighCut_   = 20000.0f;
};

//==============================================================================
// EffectRackComponent
//==============================================================================
EffectRackComponent::EffectRackComponent(AnaPlugAudioProcessor& processor)
    : processor_(processor)
{
    ANA_CRUMB("rack:enter");
    // Viewport settings
    viewport_.setScrollBarsShown(true, false);
    viewport_.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
        CyberpunkTheme::cyan_.withAlpha(0.4f));
    viewport_.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId,
        CyberpunkTheme::bg_.brighter(0.08f));
    viewport_.setViewedComponent(&contentPanel_, false);
    addAndMakeVisible(viewport_);

    // "+ Add Effect" button
    addButton_.setButtonText("+ ADD EFFECT");
    addButton_.setColour(juce::TextButton::buttonColourId, CyberpunkTheme::cyan_.withAlpha(0.15f));
    addButton_.setColour(juce::TextButton::textColourOffId, CyberpunkTheme::cyan_);
    addButton_.onClick = [this] { showAddEffectMenu(); };
    addAndMakeVisible(addButton_);

    // Initial build
    rebuildSlots();
    ANA_CRUMB("rack:exit");
}

//==============================================================================
void EffectRackComponent::resized()
{
    auto area = getLocalBounds();

    // Add button at bottom
    addButton_.setBounds(area.removeFromBottom(22).reduced(2, 2));

    // Viewport fills remaining space
    viewport_.setBounds(area);

    // Layout content panel (per-slot heights: 30 collapsed, 30 + params expanded)
    const int contentW = juce::jmax(1, viewport_.getWidth() - 6);
    std::vector<int> heights(slots_.size(), slotHeight);
    for (size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i]->isExpanded())
            heights[i] = slotHeight + slots_[i]->getParamPanelPreferredHeight(contentW);

    int contentH = 0;
    for (const int h : heights)
        contentH += h;
    contentPanel_.setSize(contentW, contentH);
    contentPanel_.setBounds(0, 0, contentW, contentH);

    int y = 0;
    for (size_t i = 0; i < slots_.size(); ++i)
    {
        slots_[i]->setBounds(0, y, contentW, heights[i]);
        slots_[i]->setSlotIndex(static_cast<int>(i));
        slots_[i]->syncFromProcessor();
        y += heights[i];
    }
}

//==============================================================================
void EffectRackComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Panel background fill
    g.setColour(CyberpunkTheme::bg_.withAlpha(0.5f));
    g.fillRect(bounds);

    // Border accent
    g.setColour(CyberpunkTheme::cyan_.withAlpha(0.12f));
    g.drawRect(bounds.toFloat(), 1.0f);
}

//==============================================================================
void EffectRackComponent::rebuildSlots()
{
    // Clear existing slot widgets
    slots_.clear();
    contentPanel_.removeAllChildren();

    auto& chain = processor_.getEffectsChain();
    const int numEffects = chain.getNumEffects();

    for (int i = 0; i < numEffects; ++i)
    {
        auto slot = std::make_unique<EffectSlotWidget>(processor_, i, i == numEffects - 1);

        slot->onRemove = [this](int idx) { onSlotRemove(idx); };
        slot->onMoveUp = [this](int idx) { onSlotMoveUp(idx); };
        slot->onMoveDown = [this](int idx) { onSlotMoveDown(idx); };
        slot->onExpandToggled = [this](int idx) { onSlotExpandToggled(idx); };

        contentPanel_.addAndMakeVisible(slot.get());
        slots_.push_back(std::move(slot));
    }

    expanded_.assign(static_cast<size_t>(numEffects), false);
    resized();
}

//==============================================================================
void EffectRackComponent::onSlotRemove(int slotIndex)
{
    auto& chain = processor_.getEffectsChain();
    if (slotIndex < 0 || slotIndex >= chain.getNumEffects())
        return;

    undoManager_.perform(new EffectRemoveAction(chain, slotIndex));
    rebuildSlots();
}

//==============================================================================
void EffectRackComponent::onSlotMoveUp(int slotIndex)
{
    if (slotIndex <= 0)
        return;

    auto& chain = processor_.getEffectsChain();
    if (slotIndex >= chain.getNumEffects())
        return;

    auto action = std::make_unique<EffectMoveAction>(chain, slotIndex, slotIndex - 1);
    undoManager_.perform(action.release());
    rebuildSlots();
}

//==============================================================================
void EffectRackComponent::onSlotMoveDown(int slotIndex)
{
    auto& chain = processor_.getEffectsChain();
    if (slotIndex < 0 || slotIndex >= chain.getNumEffects() - 1)
        return;

    auto action = std::make_unique<EffectMoveAction>(chain, slotIndex, slotIndex + 1);
    undoManager_.perform(action.release());
    rebuildSlots();
}

//==============================================================================
void EffectRackComponent::onSlotExpandToggled(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(expanded_.size()))
        return;
    expanded_[(size_t) slotIndex] = slots_[(size_t) slotIndex]->isExpanded();
    resized();
}

//==============================================================================
void EffectRackComponent::syncFromProcessor()
{
    for (auto& slot : slots_)
        slot->syncFromProcessor();
}

//==============================================================================
void EffectRackComponent::showAddEffectMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&CyberpunkTheme::getInstance());

    auto types = EffectSlotWidget::getAvailableEffectTypes();
    for (int i = 0; i < types.size(); ++i)
    {
        juce::PopupMenu::Item item;
        item.itemID = i + 1;
        item.text = types[i];
        item.isEnabled = true;
        item.isTicked = false;
        item.action = [this, typeName = types[i]]()
        {
            auto& chain = processor_.getEffectsChain();
            undoManager_.perform(new EffectAddAction(
                chain, chain.getNumEffects(), typeName));
            rebuildSlots();
        };
        menu.addItem(std::move(item));
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
        .withTargetComponent(&addButton_)
        .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards));
}

} // namespace ana
