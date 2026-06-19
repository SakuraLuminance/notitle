#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ana {

class DelayEffect {
public:
    DelayEffect();
    ~DelayEffect() = default;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();
    void setDelayTime(float ms);
    void setDelayBeats(float beats);
    void setFeedback(float percent);
    void setMix(float percent);
    void setTempo(double bpm);
    void setPingPong(bool v);
    void setSyncMode(bool v);

    juce::ValueTree getState() const
    {
        juce::ValueTree tree("DelayEffect");
        tree.setProperty("delayMs", delayMs, nullptr);
        tree.setProperty("feedback", feedback, nullptr);
        tree.setProperty("mix", mixVal, nullptr);
        tree.setProperty("syncMode", syncMode, nullptr);
        tree.setProperty("pingPong", pingPong, nullptr);
        tree.setProperty("beats", beats, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& state)
    {
        setDelayTime(state.getProperty("delayMs", 250.0f));
        setFeedback(static_cast<float>(state.getProperty("feedback", 0.3f)) * 100.0f);
        setMix(static_cast<float>(state.getProperty("mix", 0.3f)) * 100.0f);
        setSyncMode(state.getProperty("syncMode", false));
        setPingPong(state.getProperty("pingPong", false));
        setDelayBeats(state.getProperty("beats", 0.25f));
    }

private:
    juce::dsp::DryWetMixer<float> mixer;
    float delayMs = 250.0f, feedback = 0.3f, mixVal = 0.3f;
    double sr = 44100.0, bpm = 120.0;
    bool syncMode = false, pingPong = false;
    float beats = 0.25f;
    std::vector<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>> lines;
    juce::AudioBuffer<float> dryBuffer_;
    bool bypassed_ = false;
};

} // namespace ana
