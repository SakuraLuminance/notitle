#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

enum class StereoWidenerMode {
    Stereo,  ///< Normal M/S processing with user-controlled width
    Wide,    ///< Fixed maximum width (acts as dedicated widener)
    Pan      ///< Passthrough — no widening applied
};

class StereoWidenerEffect : public EffectBase {
public:
    StereoWidenerEffect();
    ~StereoWidenerEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setWidth(float normalized);   ///< 0.0 = 0%, 0.5 = 100%, 1.0 = 200%
    void setMode(StereoWidenerMode m);
    void setMix(float wet);            ///< 0.0 = dry, 1.0 = fully wet
    void setBypass(bool b);

    float getWidth() const { return width; }
    StereoWidenerMode getMode() const { return mode; }
    float getMix() const { return mix; }
    bool isBypassed() const { return bypassed; }

    juce::ValueTree getState() const override
    {
        juce::ValueTree tree("StereoWidenerEffect");
        tree.setProperty("width", width, nullptr);
        tree.setProperty("mode", static_cast<int>(mode), nullptr);
        tree.setProperty("mix", mix, nullptr);
        tree.setProperty("bypass", bypassed, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& state) override
    {
        setWidth(state.getProperty("width", 0.5f));
        setMode(static_cast<StereoWidenerMode>(
                    juce::jlimit(0, 2, static_cast<int>(state.getProperty("mode", static_cast<int>(StereoWidenerMode::Stereo))))));
        setMix(state.getProperty("mix", 1.0f));
        setBypass(state.getProperty("bypass", false));
    }

private:
    float width = 0.5f;        ///< Normalized 0..1 → actual width factor = width * 2.0f
    StereoWidenerMode mode = StereoWidenerMode::Stereo;
    float mix = 1.0f;
    bool bypassed = false;

    double sampleRate = 44100.0;
    int numChannels = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoWidenerEffect)
};

} // namespace ana
