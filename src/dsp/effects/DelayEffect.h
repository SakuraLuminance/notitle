#pragma once
#include <juce_dsp/juce_dsp.h>

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
private:
    juce::dsp::DryWetMixer<float> mixer;
    float delayMs = 250.0f, feedback = 0.3f, mixVal = 0.3f;
    double sr = 44100.0, bpm = 120.0;
    bool syncMode = false, pingPong = false;
    float beats = 0.25f;
    std::vector<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationType::Linear>> lines;
};

} // namespace ana
