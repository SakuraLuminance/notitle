#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

enum class SpaceMode { Room, Hall, Plate, Shimmer, Widener };

class SpaceModule : public EffectBase {
public:
    SpaceModule();
    ~SpaceModule() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setMode(SpaceMode m);

    // Reverb shared (Room/Hall/Plate/Shimmer)
    void setReverbSize(float v);
    void setReverbDamping(float v);
    void setReverbWidth(float v);

    // Shimmer-specific
    void setShimmerShift(float semitones);
    void setShimmerFeedback(float v);

    // Widener-specific
    void setWidenerWidth(float v);

    // Shared
    void setMix(float wet);
    void setWetHPF(float hz);
    void setWetLPF(float hz);
    void setBypass(bool b);

    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& tree) override;

private:
    void processReverbMode(juce::AudioBuffer<float>& buffer);
    void processShimmer(juce::AudioBuffer<float>& buffer);
    void processWidener(juce::AudioBuffer<float>& buffer);
    void applyWetFilters(juce::AudioBuffer<float>& buffer);
    void applyDryWetMix(juce::AudioBuffer<float>& buffer);
    void setReverbPreset(SpaceMode m);

    SpaceMode mode = SpaceMode::Room;

    // --- Reverb ---
    juce::dsp::Reverb reverb;
    float reverbSize = 0.5f;
    float reverbDamping = 0.5f;
    float reverbWidth = 0.5f;

    // --- Shimmer ---
    float shimmerShift = 12.0f;        // semitones up
    float shimmerFeedback = 0.4f;
    // Delay line for shimmer pitch shifting
    std::vector<float> shimmerBuf;
    int shimmerBufSize = 0;
    int shimmerWritePos = 0;
    float shimmerReadPos = 0.0f;
    float shimmerCrossfadePos = 0.0f;
    float shimmerCrossfadeLen = 0.0f;
    float shimmerPitchRatio = 2.0f;    // computed from shimmerShift
    // Two read heads for crossfade
    float shimmerReadPos2 = 0.0f;
    bool shimmerUseSecondHead = false;

    // --- Widener ---
    float widenerWidth = 0.5f;

    // --- Shared ---
    float mixVal = 1.0f;
    bool bypassed = false;
    double sampleRate = 44100.0;

    // Wet filters
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> wetHPF;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> wetLPF;

    // Pre-allocated dry/wet buffers
    juce::AudioBuffer<float> dryBuffer_;
    juce::AudioBuffer<float> wetBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpaceModule)
};

} // namespace ana
