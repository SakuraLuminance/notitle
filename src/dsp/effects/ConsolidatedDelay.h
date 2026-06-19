#pragma once
#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

//==============================================================================
/** Multi-mode delay effect following the SaturationEffect enum + switch pattern.

    Modes:
    - Mono:     Sums stereo to mono delay line, outputs to all channels
    - Stereo:   Independent delay lines per channel
    - PingPong: Delay bounces between left and right channels
    - Reverse:  Continuously records input, plays back backwards within a window
    - Tape:     Tape-echo emulation with wow/flutter modulation and tone control
    - Ducking:  Delay output attenuates when input exceeds threshold
*/
enum class DelayMode { Mono, Stereo, PingPong, Reverse, Tape, Ducking };

class ConsolidatedDelay : public EffectBase {
public:
    ConsolidatedDelay();
    ~ConsolidatedDelay() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    // Shared parameters (shown in all modes)
    //==============================================================================
    void setTime(float ms);         // 20-2000 ms
    void setFeedback(float val);    // 0-0.99 (linear 0-1)
    void setMix(float val);         // 0-1 (linear 0-1)
    void setWetHPF(float hz);       // 20-20000 Hz
    void setWetLPF(float hz);       // 20-20000 Hz
    void setMode(DelayMode m);

    //==============================================================================
    // Reverse-specific
    //==============================================================================
    void setWindowLength(float ms); // 50-1000 ms

    //==============================================================================
    // Tape-specific
    //==============================================================================
    void setWowFlutter(float val);  // 0-1 (amount of pitch instability)
    void setTone(float val);        // 0-1 (bright → dark feedback filtering)

    //==============================================================================
    // Ducking-specific
    //==============================================================================
    void setThreshold(float dB);    // -60-0 dB
    void setDuckRelease(float ms);  // 10-1000 ms

    //==============================================================================
    // Utility
    //==============================================================================
    void setBypass(bool b);
    bool isBypassed() const { return bypassed; }

    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& tree) override;

private:
    //==============================================================================
    // Per-mode DSP processors — dispatched by process() via switch(mode)
    //==============================================================================
    void processMono(juce::AudioBuffer<float>& buffer, float delaySamples);
    void processStereo(juce::AudioBuffer<float>& buffer, float delaySamples);
    void processPingPong(juce::AudioBuffer<float>& buffer, float delaySamples);
    void processReverse(juce::AudioBuffer<float>& buffer, float delaySamples);
    void processTape(juce::AudioBuffer<float>& buffer, float delaySamples);
    void processDucking(juce::AudioBuffer<float>& buffer, float delaySamples);

    //==============================================================================
    // Helpers
    //==============================================================================
    void updateWetFilters();
    float getCurrentTimeSamples() const noexcept;

    //==============================================================================
    // State — shared
    //==============================================================================
    std::atomic<int> modeAtomic_{ static_cast<int>(DelayMode::Stereo) };
    float timeMs = 250.0f;
    float feedback = 0.3f;
    float mixVal = 0.3f;
    float wetHPFHz = 20.0f;
    float wetLPFHz = 20000.0f;
    bool bypassed = false;

    double sampleRate = 44100.0;
    int numChannels = 2;

    //==============================================================================
    // Delay lines (one per channel)
    //==============================================================================
    std::vector<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>> delayLines_;

    //==============================================================================
    // Dry buffer for wet/dry mix
    //==============================================================================
    juce::AudioBuffer<float> dryBuffer_;

    //==============================================================================
    // Wet HPF/LPF per channel
    //==============================================================================
    struct WetFilterSlot {
        juce::dsp::IIR::Filter<float> hpf;
        juce::dsp::IIR::Filter<float> lpf;
        bool dirty = true;
    };
    std::vector<WetFilterSlot> wetFilters_;

    //==============================================================================
    // Reverse state
    //==============================================================================
    juce::AudioBuffer<float> reverseBuffer_;
    int reverseWritePos_ = 0;
    float windowLengthMs = 200.0f;

    //==============================================================================
    // Tape state
    //==============================================================================
    float wowFlutter = 0.0f;
    float toneVal = 0.5f;
    double tapePhase_ = 0.0;
    struct TapeFbLP {
        float y1 = 0.0f;
    };
    std::vector<TapeFbLP> tapeFbLP_;

    //==============================================================================
    // Ducking state
    //==============================================================================
    float thresholdDB = -24.0f;
    float duckReleaseMs = 200.0f;
    float duckEnv_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConsolidatedDelay)
};

} // namespace ana
