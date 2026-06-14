#pragma once
#include <juce_dsp/juce_dsp.h>

namespace ana {

enum class ReverbPreset { Hall, Room, Plate, Spring };

class ReverbEffect {
public:
    ReverbEffect();
    ~ReverbEffect() = default;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();
    void setRoomSize(float v);
    void setDamping(float v);
    void setWetLevel(float v);
    void setDryLevel(float v);
    void setWidth(float v);
    void setPreset(ReverbPreset p);
private:
    juce::dsp::Reverb reverb;
    float roomSize = 0.5f, damping = 0.5f, wetLevel = 0.3f, dryLevel = 0.7f, width = 0.5f;
};

} // namespace ana
