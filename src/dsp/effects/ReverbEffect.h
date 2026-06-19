#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_data_structures/juce_data_structures.h>

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

    juce::ValueTree getState() const
    {
        juce::ValueTree tree("ReverbEffect");
        tree.setProperty("roomSize", roomSize, nullptr);
        tree.setProperty("damping", damping, nullptr);
        tree.setProperty("wetLevel", wetLevel, nullptr);
        tree.setProperty("dryLevel", dryLevel, nullptr);
        tree.setProperty("width", width, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& state)
    {
        setRoomSize(state.getProperty("roomSize", 0.5f));
        setDamping(state.getProperty("damping", 0.5f));
        setWetLevel(state.getProperty("wetLevel", 0.3f));
        setDryLevel(state.getProperty("dryLevel", 0.7f));
        setWidth(state.getProperty("width", 0.5f));
    }

private:
    juce::dsp::Reverb reverb;
    float roomSize = 0.5f, damping = 0.5f, wetLevel = 0.3f, dryLevel = 0.7f, width = 0.5f;
    bool bypassed = false;
};

} // namespace ana
