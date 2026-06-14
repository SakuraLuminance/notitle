#pragma once
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace ana {

enum class CombMode
{
    Feedforward,
    Feedback,
    Dual
};

class CombFilter
{
public:
    CombFilter();
    ~CombFilter();

    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();

    void setDelayTime(float ms);
    void setFeedback(float fb);    // -1.0 to 1.0
    void setDamping(float damp);   // 0.0 to 1.0
    void setMode(CombMode m);

    float getDelayTime() const { return delayTimeMs; }
    float getFeedback() const { return feedback; }
    float getDamping() const { return damping; }
    CombMode getMode() const { return mode; }

    void process(juce::dsp::AudioBlock<float>& block);

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationType::Linear> delayLine;
    double sampleRate = 44100.0;
    float delayTimeMs = 10.0f;
    float feedback = 0.0f;
    float damping = 0.5f;
    CombMode mode = CombMode::Feedback;

    // For damping low-pass filter
    float lastOutput[2] = {};
};

} // namespace ana
