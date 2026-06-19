#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_undo/juce_undo.h>
#include "../PluginProcessor.h"
#include "CyberpunkTheme.h"

namespace ana {

//==============================================================================
/**
    A single effect slot widget in the dynamic effect rack.
    Displays controls for type, bypass, mix, LO/HI filters, and removal.

    Layout (32px height):
    [▲▼] [Type Label] [BYP 🔘] [Mix ████] [LO ███] [HI ███] [X ❌]

    Uses CyberpunkTheme colours and fonts consistently.
*/
class EffectSlotWidget : public juce::Component
{
public:
    EffectSlotWidget(AnaPlugAudioProcessor& processor, int slotIndex, bool isLast);
    ~EffectSlotWidget() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setSlotIndex(int index);
    int getSlotIndex() const noexcept { return slotIndex_; }

    /** Sync UI controls from the EffectsChain slot state. */
    void syncFromProcessor();

    std::function<void(int)> onRemove;
    std::function<void(int)> onMoveUp;
    std::function<void(int)> onMoveDown;

private:
    AnaPlugAudioProcessor& processor_;
    int slotIndex_;
    bool isLast_;

    juce::TextButton upButton_;
    juce::TextButton downButton_;
    juce::Label      typeLabel_;
    juce::TextButton bypassButton_;
    juce::Slider     mixSlider_;
    juce::Slider     lowCutSlider_;
    juce::Slider     highCutSlider_;
    juce::Slider     modSlider_;
    juce::TextButton removeButton_;

    //==============================================================================
    /** Available effect type names for the "Add Effect" popup menu. */
public:
    static juce::StringArray getAvailableEffectTypes();

    /** Create a concrete EffectBase by type name. */
    static std::unique_ptr<EffectBase> createEffectByName(const juce::String& typeName);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectSlotWidget)
};

//==============================================================================
/**
    Scrollable dynamic effect rack (Phase Plant / Bitwig style).

    Contains a vertical list of EffectSlotWidgets inside a Viewport with
    an "+ Add Effect" button at the bottom. Slots can be reordered via
    up/down arrow buttons and removed via X button with UndoableAction.

    Integrates with the processor's EffectsChain directly so any change
    made to the rack is reflected in the DSP pipeline.
*/
class EffectRackComponent : public juce::Component
{
public:
    EffectRackComponent(AnaPlugAudioProcessor& processor);
    ~EffectRackComponent() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    /** Full rebuild of slot widgets from the EffectsChain state. */
    void rebuildSlots();

    /** Sync all slot values from the EffectsChain state (no structural changes). */
    void syncFromProcessor();

private:
    AnaPlugAudioProcessor& processor_;
    juce::Viewport viewport_;
    juce::Component contentPanel_;
    juce::TextButton addButton_;
    juce::UndoManager undoManager_;
    std::vector<std::unique_ptr<EffectSlotWidget>> slots_;

    void onSlotRemove(int slotIndex);
    void onSlotMoveUp(int slotIndex);
    void onSlotMoveDown(int slotIndex);
    void showAddEffectMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectRackComponent)
};

} // namespace ana
