#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

class RingModulatorEffect : public EffectBase {
public:
    RingModulatorEffect();
    ~RingModulatorEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setFrequency(float hz);
    void setWaveform(int wf);   // 0=Sine, 1=Triangle, 2=Square
    void setMix(float wet);
    void setBypass(bool b);
    void setGain(float g);

    juce::ValueTree getState() const override
    {
        juce::ValueTree tree("RingModulatorEffect");
        tree.setProperty("frequency", frequency, nullptr);
        tree.setProperty("waveform", waveform, nullptr);
        tree.setProperty("mix", mixVal, nullptr);
        tree.setProperty("bypass", bypassed, nullptr);
        tree.setProperty("gain", gainVal, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& tree) override
    {
        setFrequency(tree.getProperty("frequency", 100.0f));
        setWaveform(tree.getProperty("waveform", 0));
        setMix(tree.getProperty("mix", 0.5f));
        setBypass(tree.getProperty("bypass", false));
        setGain(tree.getProperty("gain", 1.0f));
    }

private:
    float frequency = 100.0f;
    int waveform = 0;     // 0=Sine, 1=Triangle, 2=Square
    float mixVal = 0.5f;
    bool bypassed = false;
    float gainVal = 1.0f;

    double sampleRate = 44100.0;
    int numChannels = 2;

    // Phase accumulator per channel [0, 1) for triangle/square
    std::vector<float> phase_;

    // Recursive phasor state per channel (cos, sin) for sine carrier
    std::vector<float> phasorCos_;
    std::vector<float> phasorSin_;

    // Rotation coefficients for recursive phasor (recomputed on frequency change)
    float cosDelta_ = 1.0f;
    float sinDelta_ = 0.0f;

    // Reusable dry buffer for wet/dry mix
    juce::AudioBuffer<float> dryBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RingModulatorEffect)
};

} // namespace ana
