#include "PhaserEffect.h"
#include <cmath>

namespace ana {

PhaserEffect::PhaserEffect() {}

void PhaserEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);

    // Resize per-channel filter banks
    allPassFilters.resize(numChannels);
    for (int ch = 0; ch < numChannels; ++ch) {
        allPassFilters[ch].resize(stages);
        for (int i = 0; i < stages; ++i) {
            allPassFilters[ch][i].prepare(spec);
        }
    }

    // Init LFO phase per channel (right channel offset by stereoPhaseOffset)
    lfoPhase.resize(numChannels, 0.0f);
    for (int ch = 1; ch < numChannels; ++ch)
        lfoPhase[ch] = stereoPhaseOffset / 360.0f;

    fbOut.resize(numChannels, 0.0f);

    // Pre-allocate wet buffer
    wetBuffer.setSize(numChannels, static_cast<int>(spec.maximumBlockSize));
}

void PhaserEffect::reset() {
    for (int ch = 0; ch < numChannels; ++ch) {
        for (int i = 0; i < stages; ++i)
            allPassFilters[ch][i].reset();
        lfoPhase[ch] = (ch > 0) ? stereoPhaseOffset / 360.0f : 0.0f;
        fbOut[ch] = 0.0f;
    }
}

void PhaserEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    int numSamples = buffer.getNumSamples();
    int numCh = juce::jmin(numChannels, buffer.getNumChannels());

    // Ensure wet buffer is sized correctly
    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize(numCh, numSamples, false, true, false);
    wetBuffer.clear();

    for (int ch = 0; ch < numCh; ++ch) {
        auto* wetData = wetBuffer.getWritePointer(ch);
        const auto* dryData = buffer.getReadPointer(ch);

        for (int s = 0; s < numSamples; ++s) {
            // Update LFO
            lfoPhase[ch] += rate / static_cast<float>(sampleRate);
            if (lfoPhase[ch] >= 1.0f) lfoPhase[ch] -= 1.0f;
            float lfo = std::sin(2.0f * juce::MathConstants<float>::pi * lfoPhase[ch]);
            float modAmount = depth * lfo;
            float baseFreq = 800.0f;
            float modulatedFreq = baseFreq * std::pow(2.0f, modAmount * 2.0f);
            modulatedFreq = juce::jlimit(20.0f, sampleRate * 0.45f, modulatedFreq);

            // Update all-pass coefficients for this channel's stages
            updateAllPassCoeffs(ch, modulatedFreq);

            // Process through all-pass chain
            float x = dryData[s] + fbOut[ch] * feedback;
            for (int i = 0; i < stages; ++i) {
                auto& apf = allPassFilters[ch][i];
                juce::dsp::AudioBlock<float> block(wetBuffer, ch, s, 1);
                block.setSample(0, 0, x);
                apf.process(juce::dsp::ProcessContextReplacing<float>(block));
                x = block.getSample(0, 0);
            }
            fbOut[ch] = x;
            wetData[s] = x;
        }
    }

    // Dry/wet mix
    for (int ch = 0; ch < numCh; ++ch) {
        const auto* wetData = wetBuffer.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s) {
            outData[s] = outData[s] * (1.0f - mixVal) + wetData[s] * mixVal * gainVal;
        }
    }
}

void PhaserEffect::updateAllPassCoeffs(int channel, float frequency) {
    float sr = static_cast<float>(sampleRate);
    for (int i = 0; i < stages; ++i) {
        // Slightly stagger each stage's frequency for classic phaser sound
        float stageFreq = frequency * (1.0f + i * 0.05f);
        stageFreq = juce::jlimit(20.0f, sr * 0.45f, stageFreq);

        float w0 = 2.0f * juce::MathConstants<float>::pi * stageFreq / sr;
        float cosW0 = std::cos(w0);
        float alpha = std::sin(w0) * 0.707f; // Q = 0.707

        // All-pass biquad coefficients
        // H(z) = (a2 + a1*z^-1 + z^-2) / (1 + a1*z^-1 + a2*z^-2)
        // For all-pass: b0 = a2, b1 = a1, b2 = 1, a1 = -2*cos(w0)/(1+alpha), a2 = (1-alpha)/(1+alpha)
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosW0;
        float a2 = 1.0f - alpha;

        auto& coeffs = *allPassFilters[channel][i].coefficients;
        coeffs.b0 = a2 / a0;
        coeffs.b1 = a1 / a0;
        coeffs.b2 = 1.0f;
        coeffs.a1 = a1 / a0;
        coeffs.a2 = a2 / a0;
    }
}

void PhaserEffect::setRate(float hz) { rate = juce::jlimit(0.1f, 20.0f, hz); }
void PhaserEffect::setDepth(float d) { depth = juce::jlimit(0.0f, 1.0f, d); }
void PhaserEffect::setFeedback(float fb) { feedback = juce::jlimit(0.0f, 1.0f, fb); }
void PhaserEffect::setStages(int numStages) {
    stages = juce::jlimit(2, 12, numStages);
    // If already prepared, need to re-allocate filter chains
    if (!allPassFilters.empty()) {
        juce::dsp::ProcessSpec spec{ sampleRate, 512, static_cast<juce::uint32>(numChannels) };
        allPassFilters.clear();
        allPassFilters.resize(numChannels);
        for (int ch = 0; ch < numChannels; ++ch) {
            allPassFilters[ch].resize(stages);
            for (int i = 0; i < stages; ++i)
                allPassFilters[ch][i].prepare(spec);
        }
    }
}
void PhaserEffect::setMix(float wet) { mixVal = juce::jlimit(0.0f, 1.0f, wet); }
void PhaserEffect::setBypass(bool b) { bypassed = b; }
void PhaserEffect::setGain(float g) { gainVal = juce::jlimit(0.0f, 4.0f, g); }
void PhaserEffect::setStereoPhaseOffset(float deg) {
    stereoPhaseOffset = deg;
    if (lfoPhase.size() > 1)
        lfoPhase[1] = deg / 360.0f;
}

} // namespace ana
