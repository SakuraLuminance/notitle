#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

//==============================================================================
/**
    Breath-noise effect: white noise → bandpass 2-8kHz → amplitude shaped
    by vocal envelope follower → mixed with dry signal.

    Parameters:
        Breathiness (0-100%, default 30%) — overall noise level
        NoiseColor (0-100%, default 50%)  — BPF centering (2kHz dark → 8kHz bright)
        Mix        (0-20%,  default 10%)  — wet/dry ratio
*/
class BreathNoiseGenerator : public EffectBase {
public:
    BreathNoiseGenerator();
    ~BreathNoiseGenerator() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    // Parameters
    //==============================================================================
    void setBreathiness(float percent); // 0-100, default 30
    void setNoiseColor(float percent);  // 0-100, 0=dark (2kHz), 100=bright (8kHz)
    void setMix(float percent);         // 0-20,  default 10

    float getBreathiness() const noexcept { return breathinessVal_; }
    float getNoiseColor()  const noexcept { return noiseColorVal_; }
    float getMix()         const noexcept { return mixVal_; }

    //==============================================================================
    // State
    //==============================================================================
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& state) override;

private:
    void updateFilters();
    void updateEnvCoeffs();

    //==============================================================================
    // Parameters
    //==============================================================================
    float breathinessVal_ = 30.0f;  // 0-100
    float noiseColorVal_  = 50.0f;  // 0-100
    float mixVal_         = 0.1f;   // normalized 0-1 (10% default)

    double sampleRate_ = 44100.0;

    //==============================================================================
    // Per-channel DSP state
    //==============================================================================
    struct ChannelState {
        juce::dsp::IIR::Filter<float> bpf;  // bandpass filter
        float envelope = 0.0f;               // envelope follower state
    };
    std::vector<ChannelState> channels_;

    //==============================================================================
    // Envelope follower coefficients
    //==============================================================================
    float attackAlpha_  = 0.0f;  // ~5ms
    float releaseAlpha_ = 0.0f;  // ~50ms

    //==============================================================================
    // Noise generator (shared — accessed from single audio thread)
    //==============================================================================
    juce::Random noiseGen_;

    bool filtersDirty_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreathNoiseGenerator)
};

} // namespace ana
