#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

class BitcrusherEffect : public EffectBase {
public:
    BitcrusherEffect();
    ~BitcrusherEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setBitDepth(float depth);     // 1-16
    void setDownsample(float factor);  // 1-32
    void setMix(float wet);            // 0-1

    float getBitDepth() const noexcept { return bitDepth_; }
    float getDownsample() const noexcept { return downsample_; }
    float getMix() const noexcept { return mix_; }

    juce::ValueTree getState() const override
    {
        juce::ValueTree tree("BitcrusherEffect");
        tree.setProperty("bitDepth", bitDepth_, nullptr);
        tree.setProperty("downsample", downsample_, nullptr);
        tree.setProperty("mix", mix_, nullptr);
        return tree;
    }

    void setState(const juce::ValueTree& state) override
    {
        setBitDepth(state.getProperty("bitDepth", 8.0f));
        setDownsample(state.getProperty("downsample", 1.0f));
        setMix(state.getProperty("mix", 1.0f));
    }

private:
    void updateQuantizeFactor();

    float bitDepth_ = 8.0f;
    float downsample_ = 1.0f;
    float mix_ = 1.0f;

    // Precomputed quantize factor (not per-sample pow)
    float quantizeFactor_ = 256.0f;  // pow(2, 8)

    // Sample-and-hold state (per channel)
    std::vector<float> heldSample_;
    std::vector<int> sampleCounter_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitcrusherEffect)
};

} // namespace ana
