#pragma once
#include <juce_dsp/juce_dsp.h>

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
private:
    juce::dsp::Chorus<float> chorus;
    float rate = 1.0f, depth = 0.5f, centreDelay = 10.0f, feedback = 0.3f, mixVal = 0.3f;
};

} // namespace ana
