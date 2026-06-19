#include "PhaserEffect.h"
#include "../SIMDSupport.h"
#include <cmath>
#include <algorithm>

namespace ana {

PhaserEffect::PhaserEffect() {
    for (int i = 0; i < maxStages; ++i)
        stageFreqMult[i] = 1.0f + i * 0.05f;
}

void PhaserEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate = spec.sampleRate;
    numChannels = static_cast<int>(spec.numChannels);

    // Allocate TDF2 state vectors: sv[ch * maxStages + stage]
    stateSV1.resize(numChannels * maxStages, 0.0f);
    stateSV2.resize(numChannels * maxStages, 0.0f);

    // Allocate shared coefficient storage (init to pass-through: y=x)
    coeffB0.resize(maxStages, 1.0f);
    coeffB1.resize(maxStages, 0.0f);
    coeffB2.resize(maxStages, 0.0f);
    coeffA1.resize(maxStages, 0.0f);
    coeffA2.resize(maxStages, 0.0f);

    // Init LFO phase per channel (right channel offset by stereoPhaseOffset)
    lfoPhase.resize(numChannels, 0.0f);
    for (int ch = 1; ch < numChannels; ++ch)
        lfoPhase[ch] = stereoPhaseOffset / 360.0f;

    fbOut.resize(numChannels, 0.0f);
    updateCounter = 0;

    // Pre-allocate wet buffer
    wetBuffer.setSize(numChannels, static_cast<int>(spec.maximumBlockSize));
}

void PhaserEffect::reset() {
    std::fill(stateSV1.begin(), stateSV1.end(), 0.0f);
    std::fill(stateSV2.begin(), stateSV2.end(), 0.0f);
    for (int ch = 0; ch < numChannels; ++ch) {
        lfoPhase[ch] = (ch > 0) ? stereoPhaseOffset / 360.0f : 0.0f;
        fbOut[ch] = 0.0f;
    }
    updateCounter = 0;
}

void PhaserEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    int numSamples = buffer.getNumSamples();
    int numCh = juce::jmin(numChannels, buffer.getNumChannels());

    // Ensure wet buffer is sized correctly
    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize(numCh, numSamples, false, true, false);
    wetBuffer.clear();

    const float rateNorm = rate / static_cast<float>(sampleRate);
    const float twoPi = 2.0f * juce::MathConstants<float>::pi;
    const float baseFreq = 800.0f;
    const float sr = static_cast<float>(sampleRate);
    const float nyquist = sr * 0.45f;
    const float qInv = 0.707f; // Q

    for (int ch = 0; ch < numCh; ++ch) {
        auto* wetData = wetBuffer.getWritePointer(ch);
        const auto* dryData = buffer.getReadPointer(ch);
        const int chBase = ch * maxStages;

        for (int s = 0; s < numSamples; ++s) {
            // --- Per-sample LFO update ---
            lfoPhase[ch] += rateNorm;
            if (lfoPhase[ch] >= 1.0f) lfoPhase[ch] -= 1.0f;
            float lfo = std::sin(twoPi * lfoPhase[ch]);
            float modAmount = depth * lfo;
            float halfSteps = modAmount * 2.0f;

            // --- Sub-sample coefficient update (every 8 samples) ---
            if (++updateCounter >= kUpdateInterval) {
                updateCounter = 0;

                float modulatedFreq = baseFreq * fast_exp2(halfSteps);
                modulatedFreq = juce::jlimit(20.0f, nyquist, modulatedFreq);

                for (int i = 0; i < stages; ++i) {
                    float stageFreq = modulatedFreq * stageFreqMult[i];
                    stageFreq = juce::jlimit(20.0f, nyquist, stageFreq);

                    float w0 = twoPi * stageFreq / sr;
                    float cosW0 = std::cos(w0);
                    float sinW0 = std::sin(w0);
                    float alpha = sinW0 * qInv;

                    float a0 = 1.0f + alpha;
                    float invA0 = 1.0f / a0;
                    float a1Num = -2.0f * cosW0;
                    float a2Num = 1.0f - alpha;

                    // All-pass TDF2 coefficients: b0=a2/a0, b1=a1/a0, b2=1
                    // a1_feedback = a1/a0, a2_feedback = a2/a0
                    coeffB0[i] = a2Num * invA0;
                    coeffB1[i] = a1Num * invA0;
                    coeffB2[i] = 1.0f;
                    coeffA1[i] = a1Num * invA0;
                    coeffA2[i] = a2Num * invA0;
                }
            }

            // --- Direct TDF2 all-pass chain (no AudioBlock overhead) ---
            float x = dryData[s] + fbOut[ch] * feedback;
            float* sv1 = stateSV1.data() + chBase;
            float* sv2 = stateSV2.data() + chBase;

            for (int i = 0; i < stages; ++i) {
                // TDF2 biquad:
                //   y   = b0 * x + sv1
                //   sv1 = b1 * x - a1 * y + sv2
                //   sv2 = b2 * x - a2 * y
                float y = coeffB0[i] * x + sv1[i];
                sv1[i] = coeffB1[i] * x - coeffA1[i] * y + sv2[i];
                sv2[i] = coeffB2[i] * x - coeffA2[i] * y;
                x = y;
            }

            fbOut[ch] = x + 1e-30f; // denormal flushing
            wetData[s] = x;
        }
    }

    // Dry/wet mix
    for (int ch = 0; ch < numCh; ++ch) {
        const auto* wetData = wetBuffer.getReadPointer(ch);
        auto* outData = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s) {
            outData[s] = juce::jlimit(-1.0f, 1.0f, outData[s] * (1.0f - mixVal) + wetData[s] * mixVal * gainVal);
        }
    }
}

void PhaserEffect::setRate(float hz) { rate = juce::jlimit(0.1f, 20.0f, hz); }
void PhaserEffect::setDepth(float d) { depth = juce::jlimit(0.0f, 1.0f, d); }
void PhaserEffect::setFeedback(float fb) { feedback = juce::jlimit(0.0f, 1.0f, fb); }
void PhaserEffect::setStages(int numStages) {
    stages = juce::jlimit(2, maxStages, numStages);
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
