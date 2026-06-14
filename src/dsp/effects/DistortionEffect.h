#pragma once
#include <juce_dsp/juce_dsp.h>

namespace ana {

enum class DistortionType { SoftClip, HardClip, Tube, WaveFolder, BitCrush };

class DistortionEffect {
public:
    DistortionEffect();
    ~DistortionEffect() = default;
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();
    void setType(DistortionType t);
    void setDrive(float percent);
    void setRange(float percent);
    void setBlend(float percent);
    void setVolume(float percent);
private:
    DistortionType type = DistortionType::SoftClip;
    float drive = 50.0f, range = 50.0f, blend = 100.0f, volume = 100.0f;
    float waveshape(float input) const;
};

} // namespace ana
