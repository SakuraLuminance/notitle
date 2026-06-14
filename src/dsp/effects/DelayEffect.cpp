#include "DelayEffect.h"
#include <cmath>

namespace ana {

DelayEffect::DelayEffect() {}

void DelayEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sr = spec.sampleRate;
    lines.resize(spec.numChannels);
    for (auto& l : lines) l.prepare(spec);
}

void DelayEffect::reset() { for (auto& l : lines) l.reset(); }

void DelayEffect::process(juce::AudioBuffer<float>& buffer) {
    float delaySamples = syncMode ? (float)(60.0 / bpm * beats * sr) : (delayMs * sr / 1000.0f);
    int numSamples = buffer.getNumSamples();
    int numCh = buffer.getNumChannels();

    juce::AudioBuffer<float> dry(numCh, numSamples);
    dry.makeCopyOf(buffer);

    for (int s = 0; s < numSamples; ++s) {
        for (int ch = 0; ch < numCh; ++ch) {
            float input = buffer.getSample(ch, s);
            float delayed = lines[ch].popSample(ch, delaySamples);
            lines[ch].pushSample(ch, input + delayed * feedback);

            if (pingPong && numCh > 1) {
                float otherDelayed = lines[(ch + 1) % numCh].popSample(ch, delaySamples);
                buffer.setSample(ch, s, delayed + otherDelayed * feedback * 0.5f);
            } else {
                buffer.setSample(ch, s, delayed);
            }
        }
    }

    for (int ch = 0; ch < numCh; ++ch)
        for (int s = 0; s < numSamples; ++s)
            buffer.setSample(ch, s, dry.getSample(ch, s) * (1.0f - mixVal) + buffer.getSample(ch, s) * mixVal);
}

void DelayEffect::setDelayTime(float ms) { delayMs = std::max(1.0f, std::min(2000.0f, ms)); }
void DelayEffect::setDelayBeats(float b) { beats = b; }
void DelayEffect::setFeedback(float p) { feedback = std::max(0.0f, std::min(1.0f, p / 100.0f)); }
void DelayEffect::setMix(float p) { mixVal = std::max(0.0f, std::min(1.0f, p / 100.0f)); }
void DelayEffect::setTempo(double b) { bpm = b; }
void DelayEffect::setPingPong(bool v) { pingPong = v; }
void DelayEffect::setSyncMode(bool v) { syncMode = v; }

} // namespace ana
