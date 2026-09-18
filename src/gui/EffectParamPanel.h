#pragma once

#include "../dsp/EffectsChain.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace ana
{

class EffectParamPanel : public juce::Component
{
public:
    explicit EffectParamPanel(EffectBase* liveEffect);

    void paint(juce::Graphics&) override;
    void resized() override;

    int getPreferredHeight(int width) const;

    /** The effect this panel edits (never null for a live panel). */
    EffectBase* getEffect() const noexcept { return effect_; }

    /** Number of parameter knobs currently shown. */
    int getNumKnobs() const noexcept { return knobs_.size(); }

    /** Number of enumeration menus currently shown. */
    int getNumMenus() const noexcept { return menus_.size(); }

    /** Visit every knob together with its parameter index - used by the editor
        to register each knob for MIDI Learn. */
    void visitKnobs(const std::function<void(int, juce::Slider&)>& fn);

    /** Visit every choice menu together with its parameter index - the editor
        uses it to keep the menus in step when a preset or MIDI mapping moves
        an enumeration behind the UI's back. */
    void visitMenus(const std::function<void(int, juce::ComboBox&)>& fn);

private:
    void rebuildControls();

    EffectBase* effect_ = nullptr;
    juce::OwnedArray<juce::Slider>   knobs_;
    juce::OwnedArray<juce::Label>    labels_;
    juce::OwnedArray<juce::ComboBox> menus_;
    juce::OwnedArray<juce::Label>    menuLabels_;
    std::vector<int> knobParamIndex_;   // knob i edits this parameter index
    std::vector<int> menuParamIndex_;   // menu i edits this parameter index
    static constexpr int knobW = 46;
    static constexpr int rowH = 58;
    static constexpr int menuRowH = 20;
    static constexpr int menuLabelW = 72;
};

} // namespace ana
