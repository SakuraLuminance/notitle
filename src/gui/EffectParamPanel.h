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

    /** Visit every knob together with its parameter index — used by the editor
        to register each knob for MIDI Learn. */
    void visitKnobs(const std::function<void(int, juce::Slider&)>& fn);

private:
    void rebuildKnobs();

    EffectBase* effect_ = nullptr;
    juce::OwnedArray<juce::Slider> knobs_;
    juce::OwnedArray<juce::Label>  labels_;
    static constexpr int knobW = 46;
    static constexpr int rowH = 58;
};

} // namespace ana
