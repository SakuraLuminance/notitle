#include "ReverbEffect.h"

namespace ana {

ReverbEffect::ReverbEffect() {}
void ReverbEffect::prepare(const juce::dsp::ProcessSpec&) { reverb.reset(); }
void ReverbEffect::reset() { reverb.reset(); }

void ReverbEffect::process(juce::AudioBuffer<float>& buffer) {
    reverb.setParameters({roomSize, damping, wetLevel, dryLevel, width});
    juce::dsp::AudioBlock<float> block(buffer);
    reverb.process(juce::dsp::ProcessContextReplacing<float>(block));
}

void ReverbEffect::setRoomSize(float v) { roomSize = v; }
void ReverbEffect::setDamping(float v) { damping = v; }
void ReverbEffect::setWetLevel(float v) { wetLevel = v; }
void ReverbEffect::setDryLevel(float v) { dryLevel = v; }
void ReverbEffect::setWidth(float v) { width = v; }

void ReverbEffect::setPreset(ReverbPreset p) {
    switch (p) {
        case ReverbPreset::Hall:   roomSize=0.9f; damping=0.3f; wetLevel=0.4f; dryLevel=0.6f; width=0.8f; break;
        case ReverbPreset::Room:   roomSize=0.5f; damping=0.5f; wetLevel=0.3f; dryLevel=0.7f; width=0.5f; break;
        case ReverbPreset::Plate:  roomSize=0.7f; damping=0.2f; wetLevel=0.4f; dryLevel=0.6f; width=0.9f; break;
        case ReverbPreset::Spring: roomSize=0.3f; damping=0.7f; wetLevel=0.5f; dryLevel=0.5f; width=0.3f; break;
    }
}

} // namespace ana
