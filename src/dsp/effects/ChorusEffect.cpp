#include "ChorusEffect.h"

namespace ana {

ChorusEffect::ChorusEffect() {}
void ChorusEffect::prepare(const juce::dsp::ProcessSpec& spec) { chorus.prepare(spec); }
void ChorusEffect::reset() { chorus.reset(); }

void ChorusEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed_) return;
    chorus.setRate(rate);
    chorus.setDepth(depth);
    chorus.setCentreDelay(centreDelay);
    chorus.setFeedback(feedback);
    chorus.setMix(mixVal);
    juce::dsp::AudioBlock<float> block(buffer);
    chorus.process(juce::dsp::ProcessContextReplacing<float>(block));
}

void ChorusEffect::setRate(float hz) { rate = juce::jlimit(0.001f, 20.0f, hz); }
void ChorusEffect::setDepth(float p) { depth = juce::jlimit(0.0f, 1.0f, p / 100.0f); }
void ChorusEffect::setCentreDelay(float ms) { centreDelay = juce::jlimit(0.001f, 100.0f, ms); }
void ChorusEffect::setFeedback(float p) { feedback = juce::jlimit(0.0f, 0.99f, p / 100.0f); }
void ChorusEffect::setMix(float p) { mixVal = juce::jlimit(0.0f, 1.0f, p / 100.0f); }

} // namespace ana
