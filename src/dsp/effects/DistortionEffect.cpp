#include "DistortionEffect.h"
#include <cmath>

namespace ana {

DistortionEffect::DistortionEffect() {}
void DistortionEffect::prepare(const juce::dsp::ProcessSpec&) {}
void DistortionEffect::reset() {}

float DistortionEffect::waveshape(float input) const {
    float d = drive / 50.0f;
    switch (type) {
        case DistortionType::SoftClip:  return std::tanh(input * d);
        case DistortionType::HardClip: { float threshold = 1.0f / (d + 0.01f); return std::max(-threshold, std::min(threshold, input)); }
        case DistortionType::Tube:      return input * d / (1.0f + std::abs(input * d)) * 1.5f;
        case DistortionType::WaveFolder: { float fold = 1.0f + d * 2.0f; float v = input * fold; return v - std::round(v / 2.0f) * 2.0f; }
        case DistortionType::BitCrush: { float bits = 16.0f - d * 14.0f; float steps = std::pow(2.0f, bits); return std::round(input * steps) / steps; }
    }
    return input;
}

void DistortionEffect::process(juce::AudioBuffer<float>& buffer) {
    float vol = volume / 100.0f;
    float bl = blend / 100.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s) {
            float dry = buffer.getSample(ch, s);
            float wet = waveshape(dry);
            buffer.setSample(ch, s, dry * (1.0f - bl) + wet * bl * vol);
        }
}

void DistortionEffect::setType(DistortionType t) { type = t; }
void DistortionEffect::setDrive(float p) { drive = std::max(0.0f, std::min(100.0f, p)); }
void DistortionEffect::setRange(float p) { range = std::max(0.0f, std::min(100.0f, p)); }
void DistortionEffect::setBlend(float p) { blend = std::max(0.0f, std::min(100.0f, p)); }
void DistortionEffect::setVolume(float p) { volume = std::max(0.0f, std::min(200.0f, p)); }

} // namespace ana
