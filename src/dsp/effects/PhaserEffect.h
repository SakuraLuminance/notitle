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

    juce::ValueTree getState() const override
    {
        juce::ValueTree tree("PhaserEffect");
        tree.setProperty("rate", rate, nullptr);
        tree.setProperty("depth", depth, nullptr);
        tree.setProperty("feedback", feedback, nullptr);
        tree.setProperty("stages", stages, nullptr);
        tree.setProperty("mix", mixVal, nullptr);
        tree.setProperty("bypass", bypassed, nullptr);
        tree.setProperty("gain", gainVal, nullptr);
        tree.setProperty("stereoPhaseOffset", stereoPhaseOffset, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& tree) override
    {
        setRate(tree.getProperty("rate", 1.0f));
        setDepth(tree.getProperty("depth", 0.5f));
        setFeedback(tree.getProperty("feedback", 0.3f));
        setStages(tree.getProperty("stages", 6));
        setMix(tree.getProperty("mix", 0.5f));
        setBypass(tree.getProperty("bypass", false));
        setGain(tree.getProperty("gain", 1.0f));
        setStereoPhaseOffset(tree.getProperty("stereoPhaseOffset", 0.0f));
    }

private:
    static constexpr int maxStages = 12;
    static constexpr int kUpdateInterval = 8;

    // Pre-computed per-stage frequency multipliers (1.0f + i * 0.05f)
    float stageFreqMult[maxStages] = {};

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

    // TDF2 state variables per (channel, stage): sv[ch * maxStages + stage]
    std::vector<float> stateSV1;
    std::vector<float> stateSV2;

    // Allpass coefficients (shared across channels, one set per stage)
    std::vector<float> coeffB0;
    std::vector<float> coeffB1;
    std::vector<float> coeffB2; // always 1.0f for all-pass
    std::vector<float> coeffA1;
    std::vector<float> coeffA2;

    // LFO phase accumulators per channel
    std::vector<float> lfoPhase;

    // Feedback buffers per channel
    std::vector<float> fbOut;

    // Internal processing buffer for wet signal
    juce::AudioBuffer<float> wetBuffer;

    // Sub-sample coefficient update counter
    int updateCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaserEffect)
};

} // namespace ana
