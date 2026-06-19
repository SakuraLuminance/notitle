#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>

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

    juce::ValueTree getState() const
    {
        juce::ValueTree tree("DistortionEffect");
        tree.setProperty("type", static_cast<int>(type), nullptr);
        tree.setProperty("drive", drive, nullptr);
        tree.setProperty("range", range, nullptr);
        tree.setProperty("blend", blend, nullptr);
        tree.setProperty("volume", volume, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& state)
    {
        setType(static_cast<DistortionType>(juce::jlimit(0, 4, static_cast<int>(state.getProperty("type", static_cast<int>(DistortionType::SoftClip))))));
        setDrive(state.getProperty("drive", 50.0f));
        setRange(state.getProperty("range", 50.0f));
        setBlend(state.getProperty("blend", 100.0f));
        setVolume(state.getProperty("volume", 100.0f));
    }

private:
    DistortionType type = DistortionType::SoftClip;
    float drive = 50.0f, range = 50.0f, blend = 100.0f, volume = 100.0f;
    bool bypassed = false;

};

} // namespace ana
