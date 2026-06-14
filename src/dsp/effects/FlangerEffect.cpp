#include "FlangerEffect.h"
#include <cmath>

namespace ana {

FlangerEffect::FlangerEffect() {}

void FlangerEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);

    delayLines.resize(numChannels);
    for (auto& dl : delayLines)
        dl.prepare(spec);

    // LFO phases: left = 0, right = 0.5 (inverted)
    lfoPhase.resize(numChannels, 0.0f);
    for (int ch = 1; ch < numChannels; ++ch)
        lfoPhase[ch] = 0.5f;
}

void FlangerEffect::reset() {
    for (auto& dl : delayLines)
        dl.reset();
    lfoPhase[0] = 0.0f;
    for (int ch = 1; ch < numChannels; ++ch)
        lfoPhase[ch] = 0.5f;
}

void FlangerEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    int numSamples = buffer.getNumSamples();
    int numCh = juce::jmin(numChannels, buffer.getNumChannels());

    // Dry buffer for mixing
    juce::AudioBuffer<float> dry(numCh, numSamples);
    dry.makeCopyOf(buffer);

    for (int ch = 0; ch < numCh; ++ch) {
        auto* data = buffer.getWritePointer(ch);
        float& phase = lfoPhase[ch];

        for (int s = 0; s < numSamples; ++s) {
            // Advance LFO: for right channel (ch=1), phase starts at 0.5 for inversion
            phase += rate / static_cast<float>(sampleRate);
            if (phase >= 1.0f) phase -= 1.0f;

            // LFO value: [-1, 1]
            float lfo = std::sin(2.0f * juce::MathConstants<float>::pi * phase);

            // Modulated delay in samples: base delay + depth * lfo * maxModDelay
            float maxModDelaySamples = 10.0f * sampleRate / 1000.0f; // 10ms max modulation
            float baseDelaySamples = delayMs * sampleRate / 1000.0f;
            float modDelay = baseDelaySamples + lfo * depth * maxModDelaySamples;
            modDelay = std::max(1.0f, modDelay); // minimum 1 sample delay

            // Process comb filter
            float input = data[s];
            float delayed = delayLines[ch].popSample(ch, modDelay);
            delayLines[ch].pushSample(ch, input + delayed * feedback);
            data[s] = delayed;
        }
    }

    // Dry/wet mix with gain
    for (int ch = 0; ch < numCh; ++ch) {
        const auto* dryData = dry.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s) {
            outData[s] = dryData[s] * (1.0f - mixVal) + outData[s] * mixVal * gainVal;
        }
    }
}

void FlangerEffect::setRate(float hz) { rate = juce::jlimit(0.1f, 10.0f, hz); }
void FlangerEffect::setDepth(float d) { depth = juce::jlimit(0.0f, 1.0f, d); }
void FlangerEffect::setDelay(float ms) { delayMs = juce::jlimit(0.1f, 10.0f, ms); }
void FlangerEffect::setFeedback(float fb) { feedback = juce::jlimit(0.0f, 1.0f, fb); }
void FlangerEffect::setMix(float wet) { mixVal = juce::jlimit(0.0f, 1.0f, wet); }
void FlangerEffect::setBypass(bool b) { bypassed = b; }
void FlangerEffect::setGain(float g) { gainVal = juce::jlimit(0.0f, 4.0f, g); }

} // namespace ana
