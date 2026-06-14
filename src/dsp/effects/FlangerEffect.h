#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

class FlangerEffect : public EffectBase {
public:
    FlangerEffect();
    ~FlangerEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setRate(float hz);
    void setDepth(float depth);
    void setDelay(float ms);
    void setFeedback(float fb);
    void setMix(float wet);
    void setBypass(bool b);
    void setGain(float g);

private:
    float rate = 0.5f;
    float depth = 0.5f;
    float delayMs = 3.0f;
    float feedback = 0.3f;
    float mixVal = 0.5f;
    bool bypassed = false;
    float gainVal = 1.0f;

    double sampleRate = 44100.0;
    int numChannels = 2;

    // Delay lines per channel with linear interpolation
    std::vector<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>> delayLines;

    // LFO phase per channel (right channel gets inverted phase)
    std::vector<float> lfoPhase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlangerEffect)
};

} // namespace ana
