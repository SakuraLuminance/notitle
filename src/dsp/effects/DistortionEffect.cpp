#include "DistortionEffect.h"
#include <cmath>

namespace ana {

DistortionEffect::DistortionEffect() {}
void DistortionEffect::prepare(const juce::dsp::ProcessSpec&) {}
void DistortionEffect::reset() {}

void DistortionEffect::process(juce::AudioBuffer<float>& buffer) {
    if (bypassed) return;
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();
    const float vol = volume / 100.0f;
    const float bl  = blend  / 100.0f;
    const float d   = drive  / 50.0f;

    // Precompute BitCrush steps (pow is expensive — do it once, not per-sample)
    const float bitCrushSteps = (type == DistortionType::BitCrush)
        ? std::pow(2.0f, 16.0f - d * 14.0f) : 0.0f;

    // Resolve distortion type to a function pointer once — no switch in inner loop
    using ShapeFn = float (*)(float, float, float);
    ShapeFn shapeFn = nullptr;
    float p1 = 0.0f; // drive-like param
    float p2 = 0.0f; // threshold / fold / steps param

    switch (type) {
        case DistortionType::SoftClip:
            shapeFn = [](float x, float d, float)  { return std::tanh(x * d); };
            p1 = d;
            break;
        case DistortionType::HardClip: {
            const float threshold = 1.0f / (d + 0.01f);
            shapeFn = [](float x, float, float t)  { return std::max(-t, std::min(t, x)); };
            p2 = threshold;
            break;
        }
        case DistortionType::Tube:
            shapeFn = [](float x, float d, float)  { return x * d / (1.0f + std::abs(x * d)) * 1.5f; };
            p1 = d;
            break;
        case DistortionType::WaveFolder: {
            const float fold = 1.0f + d * 2.0f;
            shapeFn = [](float x, float, float f) { float v = x * f; return v - std::round(v / 2.0f) * 2.0f; };
            p2 = fold;
            break;
        }
        case DistortionType::BitCrush:
            shapeFn = [](float x, float, float s) { return std::round(x * s) / s; };
            p2 = bitCrushSteps;
            break;
    }

    // Channel-outer loop for cache locality, raw pointer access
    for (int ch = 0; ch < numChannels; ++ch) {
        const float* in  = buffer.getReadPointer(ch);
        float*       out = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s) {
            const float dry = in[s];
            const float wet = shapeFn(dry, p1, p2);
            out[s] = dry * (1.0f - bl) + wet * bl * vol;
        }
    }
}

void DistortionEffect::setType(DistortionType t) { type = t; }
void DistortionEffect::setDrive(float p) { drive = std::max(0.0f, std::min(100.0f, p)); }
void DistortionEffect::setRange(float p) { range = std::max(0.0f, std::min(100.0f, p)); }
void DistortionEffect::setBlend(float p) { blend = std::max(0.0f, std::min(100.0f, p)); }
void DistortionEffect::setVolume(float p) { volume = std::max(0.0f, std::min(200.0f, p)); }

} // namespace ana
