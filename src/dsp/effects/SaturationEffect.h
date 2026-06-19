#pragma once
#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

enum class SaturationMode { Soft, Tube, Tape };

class SaturationEffect : public EffectBase {
public:
    SaturationEffect();
    ~SaturationEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    void setDrive(float percent);   // 0-100
    void setTone(float freqHz);     // 20-20000
    void setMode(SaturationMode m);
    void setMix(float percent);     // 0-100
    void setBypass(bool b);
    void setGain(float g);

    int getLatencySamples() const { return latencySamples; }

    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& tree) override;

private:
    void updateToneFilter();

    static float driveToPreGain(float percent);  // 0-100 -> 0-10

    std::atomic<int> modeAtomic_{ static_cast<int>(SaturationMode::Soft) };
    float drive = 50.0f;
    float toneFreq = 20000.0f;
    float mixVal = 1.0f;
    bool bypassed = false;
    float gainVal = 1.0f;

    double sampleRate = 44100.0;
    int latencySamples = 0;
    int numChannels = 2;

    // Pre-gain cached for fast DSP (atomic for cross-thread safety)
    std::atomic<float> preGainAtomic_{ 0.5f };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    // WaveShaper function is set via the public .function member
    juce::dsp::WaveShaper<float> waveShaper;

    // Per-channel 1-pole LP tone filter
    struct OnePoleLP {
        float y1 = 0.0f;
        float a = 0.0f;  // coefficient
    };
    std::vector<OnePoleLP> toneFilters;
    bool toneFilterDirty = true;

    // Dry buffer for wet/dry mix
    juce::AudioBuffer<float> dryBuffer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturationEffect)
};

} // namespace ana
