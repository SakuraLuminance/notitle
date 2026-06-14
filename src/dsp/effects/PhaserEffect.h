#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

class PhaserEffect : public EffectBase {
public:
    PhaserEffect();
    ~PhaserEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setRate(float hz);
    void setDepth(float depth);
    void setFeedback(float fb);
    void setStages(int numStages);
    void setMix(float wet);
    void setBypass(bool b);
    void setGain(float g);
    void setStereoPhaseOffset(float degrees);

private:
    void updateAllPassCoeffs(int channel, float frequency);

    float rate = 1.0f;
    float depth = 0.5f;
    float feedback = 0.3f;
    int stages = 6;
    float mixVal = 0.5f;
    bool bypassed = false;
    float gainVal = 1.0f;
    float stereoPhaseOffset = 0.0f;

    double sampleRate = 44100.0;
    int numChannels = 2;

    // Per-channel all-pass filter chains
    std::vector<std::vector<juce::dsp::IIR::Filter<float>>> allPassFilters;

    // LFO state (phase accumulators per channel)
    std::vector<float> lfoPhase;

    // Feedback buffers per channel
    std::vector<float> fbOut;

    // Internal processing buffer for wet signal
    juce::AudioBuffer<float> wetBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaserEffect)
};

} // namespace ana
