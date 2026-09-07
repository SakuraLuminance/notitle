#pragma once

#include "../dsp/EffectsChain.h"
#include <juce_gui_basics/juce_gui_basics.h>
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

private:
    void rebuildKnobs();

    EffectBase* effect_ = nullptr;
    std::vector<juce::Slider> knobs_;
    std::vector<juce::Label>  labels_;
    static constexpr int knobW = 46;
    static constexpr int rowH = 58;
};

} // namespace ana
