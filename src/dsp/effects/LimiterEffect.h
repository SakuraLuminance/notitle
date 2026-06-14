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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimiterEffect)
};

} // namespace ana
