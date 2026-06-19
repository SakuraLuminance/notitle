#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

class CompressorEffect : public EffectBase {
public:
    CompressorEffect();
    ~CompressorEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setThreshold(float db);
    void setRatio(float r);
    void setAttack(float ms);
    void setRelease(float ms);
    void setKnee(float db);
    void setMakeupGain(float db);
    void setMix(float wet);
    void setBypass(bool b);
    void setGain(float g);
    void setRMSMode(bool rms);
    void setAutoMakeup(bool autoOn);

    juce::ValueTree getState() const override
    {
        juce::ValueTree tree("CompressorEffect");
        tree.setProperty("threshold", thresholdDb, nullptr);
        tree.setProperty("ratio", ratio, nullptr);
        tree.setProperty("attack", attackMs, nullptr);
        tree.setProperty("release", releaseMs, nullptr);
        tree.setProperty("knee", kneeDb, nullptr);
        tree.setProperty("makeupGain", makeupGainDb, nullptr);
        tree.setProperty("mix", mixVal, nullptr);
        tree.setProperty("bypass", bypassed, nullptr);
        tree.setProperty("gain", gainVal, nullptr);
        tree.setProperty("rmsMode", rmsMode, nullptr);
        tree.setProperty("autoMakeup", autoMakeup, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& tree) override
    {
        setThreshold(tree.getProperty("threshold", -24.0f));
        setRatio(tree.getProperty("ratio", 4.0f));
        setAttack(tree.getProperty("attack", 10.0f));
        setRelease(tree.getProperty("release", 100.0f));
        setKnee(tree.getProperty("knee", 6.0f));
        setMakeupGain(tree.getProperty("makeupGain", 0.0f));
        setMix(tree.getProperty("mix", 1.0f));
        setBypass(tree.getProperty("bypass", false));
        setGain(tree.getProperty("gain", 1.0f));
        setRMSMode(tree.getProperty("rmsMode", true));
        setAutoMakeup(tree.getProperty("autoMakeup", false));
    }

private:
    float computeGainReduction(float levelDb);

    float thresholdDb = -24.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float kneeDb = 6.0f;
    float makeupGainDb = 0.0f;
    float mixVal = 1.0f;
    bool bypassed = false;
    float gainVal = 1.0f;
    bool rmsMode = true;
    bool autoMakeup = false;

    double sampleRate = 44100.0;

    // Envelope follower state (per channel)
    float envelope = 0.0f;

    // RMS running sum per channel
    float rmsSum = 0.0f;
    int rmsWindowSize = 0;
    int rmsIndex = 0;
    std::vector<float> rmsBuffer;

    // Attack/release coefficients
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;

    // Pre-allocated buffers
    juce::AudioBuffer<float> dryBuffer_;
    std::vector<float> gainReduction_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorEffect)
};

} // namespace ana
