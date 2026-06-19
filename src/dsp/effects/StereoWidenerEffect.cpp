#include "StereoWidenerEffect.h"
#include <algorithm>
#include <cmath>

namespace ana {

StereoWidenerEffect::StereoWidenerEffect() {}

void StereoWidenerEffect::prepare(const juce::dsp::ProcessSpec& spec) {
    sampleRate   = spec.sampleRate;
    numChannels  = static_cast<int>(spec.numChannels);
}

void StereoWidenerEffect::reset() {
    // No stateful memory — M/S processing is sample-by-sample
}

void StereoWidenerEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin(numChannels, buffer.getNumChannels());

    // Mono passthrough — width has no effect
    if (numCh < 2) return;

    // Passthrough in Pan mode — no widening applied
    if (mode == StereoWidenerMode::Pan) return;

    // Skip if mix is zero
    if (mix <= 0.0f) return;

    // Resolve width factor based on mode
    float widthFactor;
    switch (mode) {
        case StereoWidenerMode::Wide:
            widthFactor = 2.0f;          // Always maximum widening
            break;
        case StereoWidenerMode::Stereo:
        default:
            widthFactor = width * 2.0f;  // 0 → mono, 0.5 → original, 1.0 → 2x
            break;
    }

    // When widthFactor == 1.0f (100% in Stereo mode) and mix == 1.0f,
    // L'=L and R'=R — can skip processing entirely
    if (widthFactor == 1.0f && mix >= 1.0f) return;

    const float* ptrL = buffer.getReadPointer(0);
    const float* ptrR = buffer.getReadPointer(1);
    float*       outL = buffer.getWritePointer(0);
    float*       outR = buffer.getWritePointer(1);

    for (int s = 0; s < numSamples; ++s) {
        const float L = ptrL[s];
        const float R = ptrR[s];

        // Mid/Side encoding
        const float mid  = (L + R) * 0.5f;
        const float side = (L - R) * 0.5f;

        // Decode with width control
        const float wetL = mid + side * widthFactor;
        const float wetR = mid - side * widthFactor;

        // Dry/wet blend
        outL[s] = L * (1.0f - mix) + wetL * mix;
        outR[s] = R * (1.0f - mix) + wetR * mix;
    }
}

void StereoWidenerEffect::setWidth(float normalized) {
    width = std::max(0.0f, std::min(1.0f, normalized));
}

void StereoWidenerEffect::setMode(StereoWidenerMode m) {
    mode = m;
}

void StereoWidenerEffect::setMix(float wet) {
    mix = std::max(0.0f, std::min(1.0f, wet));
}

void StereoWidenerEffect::setBypass(bool b) {
    bypassed = b;
}

} // namespace ana
