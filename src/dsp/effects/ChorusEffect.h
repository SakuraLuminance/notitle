#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ana {

class ChorusEffect {
public:
    ChorusEffect();
    ~ChorusEffect() = default;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();
    void setRate(float hz);
    void setDepth(float percent);
    void setCentreDelay(float ms);
    void setFeedback(float percent);
    void setMix(float percent);

    juce::ValueTree getState() const
    {
        juce::ValueTree tree("ChorusEffect");
        tree.setProperty("rate", rate, nullptr);
        tree.setProperty("depth", depth, nullptr);
        tree.setProperty("centreDelay", centreDelay, nullptr);
        tree.setProperty("feedback", feedback, nullptr);
        tree.setProperty("mix", mixVal, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& state)
    {
        setRate(state.getProperty("rate", 1.0f));
        setDepth(static_cast<float>(state.getProperty("depth", 0.5f)) * 100.0f);
        setCentreDelay(state.getProperty("centreDelay", 10.0f));
        setFeedback(static_cast<float>(state.getProperty("feedback", 0.3f)) * 100.0f);
        setMix(static_cast<float>(state.getProperty("mix", 0.3f)) * 100.0f);
    }

private:
    juce::dsp::Chorus<float> chorus;
    float rate = 1.0f, depth = 0.5f, centreDelay = 10.0f, feedback = 0.3f, mixVal = 0.3f;
    bool bypassed_ = false;
};

} // namespace ana
