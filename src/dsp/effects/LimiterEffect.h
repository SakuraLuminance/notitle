#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

class LimiterEffect : public EffectBase {
public:
    LimiterEffect();
    ~LimiterEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setThreshold(float db);
    void setAttack(float ms);
    void setRelease(float ms);
    void setLookahead(float ms);
    void setMix(float wet);
    void setBypass(bool b);
    void setGain(float g);
    void setOversampling(int factor); // 1, 2, or 4

    juce::ValueTree getState() const override
    {
        juce::ValueTree tree("LimiterEffect");
        tree.setProperty("threshold", thresholdDb, nullptr);
        tree.setProperty("attack", attackMs, nullptr);
        tree.setProperty("release", releaseMs, nullptr);
        tree.setProperty("lookahead", lookaheadMs, nullptr);
        tree.setProperty("mix", mixVal, nullptr);
        tree.setProperty("bypass", bypassed, nullptr);
        tree.setProperty("gain", gainVal, nullptr);
        tree.setProperty("oversampling", oversamplingFactor, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& tree) override
    {
        setThreshold(tree.getProperty("threshold", -6.0f));
        setAttack(tree.getProperty("attack", 0.1f));
        setRelease(tree.getProperty("release", 20.0f));
        setLookahead(tree.getProperty("lookahead", 2.0f));
        setMix(tree.getProperty("mix", 1.0f));
        setBypass(tree.getProperty("bypass", false));
        setGain(tree.getProperty("gain", 1.0f));
        setOversampling(tree.getProperty("oversampling", 1));
    }

private:
    void processLimiter(juce::AudioBuffer<float>& buffer);

    float thresholdDb = -6.0f;
    float attackMs = 0.1f;
    float releaseMs = 20.0f;
    float lookaheadMs = 2.0f;
    float mixVal = 1.0f;
    bool bypassed = false;
    float gainVal = 1.0f;
    int oversamplingFactor = 1;

    double sampleRate = 44100.0;
    int numChannels = 2;

    // Gain reduction envelope
    float envelope = 1.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;

    // Lookahead delay line
    std::vector<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>> lookaheadLines;

    // Oversampling buffers
    juce::AudioBuffer<float> osBuffer;

    // Pre-allocated dry/wet buffers
    juce::AudioBuffer<float> dryBuffer_;
    juce::AudioBuffer<float> wetBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimiterEffect)
};

} // namespace ana
