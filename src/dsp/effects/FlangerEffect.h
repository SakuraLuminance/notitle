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

    juce::ValueTree getState() const override
    {
        juce::ValueTree tree("FlangerEffect");
        tree.setProperty("rate", rate, nullptr);
        tree.setProperty("depth", depth, nullptr);
        tree.setProperty("delay", delayMs, nullptr);
        tree.setProperty("feedback", feedback, nullptr);
        tree.setProperty("mix", mixVal, nullptr);
        tree.setProperty("bypass", bypassed, nullptr);
        tree.setProperty("gain", gainVal, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& tree) override
    {
        setRate(tree.getProperty("rate", 0.5f));
        setDepth(tree.getProperty("depth", 0.5f));
        setDelay(tree.getProperty("delay", 3.0f));
        setFeedback(tree.getProperty("feedback", 0.3f));
        setMix(tree.getProperty("mix", 0.5f));
        setBypass(tree.getProperty("bypass", false));
        setGain(tree.getProperty("gain", 1.0f));
    }

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

    // Pre-allocated dry buffer
    juce::AudioBuffer<float> dryBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlangerEffect)
};

} // namespace ana
