#include "ChorusEffect.h"

namespace ana {

ChorusEffect::ChorusEffect() {}
void ChorusEffect::prepare(const juce::dsp::ProcessSpec& spec) { chorus.prepare(spec); }
void ChorusEffect::reset() { chorus.reset(); }

void ChorusEffect::process(juce::AudioBuffer<float>& buffer) {
    chorus.setRate(rate);
    chorus.setDepth(depth);
    chorus.setCentreDelay(centreDelay);
    chorus.setFeedback(feedback);
    chorus.setMix(mixVal);
    juce::dsp::AudioBlock<float> block(buffer);
    chorus.process(juce::dsp::ProcessContextReplacing<float>(block));
}

void ChorusEffect::setRate(float hz) { rate = hz; }
void ChorusEffect::setDepth(float p) { depth = p / 100.0f; }
void ChorusEffect::setCentreDelay(float ms) { centreDelay = ms; }
void ChorusEffect::setFeedback(float p) { feedback = p / 100.0f; }
void ChorusEffect::setMix(float p) { mixVal = p / 100.0f; }

} // namespace ana
