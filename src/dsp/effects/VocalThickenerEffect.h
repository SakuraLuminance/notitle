#pragma once
#include <array>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"
#include "../PitchCorrector.h"

namespace ana {

//==============================================================================
/**
    MicroShift-style vocal doubling effect.

    Creates a thick, wide vocal sound by generating N detuned copies of the
    input, applying Haas delay on alternating voices, and panning them across
    the stereo field.  Uses sinc-interpolation pitch shifting (Simple mode)
    for micro-detune with no audible comb filtering.

    Parameters:
        Detune     0-25 cents        (default 10)
        VoiceCount 2-8 voices        (default 2)
        Spread     0-100%            (default 50)
        HaasDelay  0-30 ms           (default 10)
        Mix        0-100%            (default 40)
*/
class VocalThickenerEffect : public EffectBase
{
public:
    VocalThickenerEffect();
    ~VocalThickenerEffect() override = default;

    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    // Parameter setters
    void setDetune(float cents);        ///< 0 – 25 cents
    void setVoiceCount(int voices);     ///< 2 – 8
    void setSpread(float percent);      ///< 0 – 100 %
    void setHaasDelay(float ms);        ///< 0 – 30 ms
    void setMix(float percent);         ///< 0 – 100 %
    void setBypass(bool b);

    //==============================================================================
    // Parameter getters
    float getDetune()      const noexcept { return detune; }
    int   getVoiceCount()  const noexcept { return voiceCount; }
    float getSpread()      const noexcept { return spread; }
    float getHaasDelay()   const noexcept { return haasDelayMs; }
    float getMix()         const noexcept { return mix; }
    bool  isBypassed()     const noexcept { return bypassed; }

    //==============================================================================
    // State persistence
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& state) override;

private:
    //==============================================================================
    // Simple ring-buffer delay line for Haas effect (feed-forward, no feedback)
    struct DelayLine
    {
        std::vector<float> data;
        int writePos = 0;
        int capacity = 0;

        void resize(int numSamples);
        void reset();

        /** Write one sample, return the sample delayed by @p delaySamples samples.
            If delaySamples == 0 the input is returned immediately (passthrough).
        */
        float process(float input, int delaySamples) noexcept;
    };

    //==============================================================================
    // Per-voice processing helper (inline in cpp)
    void processVoice(const float* monoInput,
                      float* outL, float* outR,
                      int numSamples,
                      float pitchShiftSemitones,
                      int haasDelaySamples,
                      float leftGain, float rightGain,
                      int voiceIndex);

    //==============================================================================
    // Parameters
    float detune      = 10.0f;   ///< cents
    int   voiceCount  = 2;
    float spread      = 0.5f;    ///< normalized 0..1
    float haasDelayMs = 10.0f;
    float mix         = 0.4f;    ///< normalized 0..1
    bool  bypassed    = false;

    //==============================================================================
    // DSP state
    double sampleRate = 44100.0;
    int    numChannels = 2;
    int    maxExpectedBlockSize = 1024;

    PitchCorrector pitchShifter;

    static constexpr int kMaxVoices = 8;
    std::array<DelayLine, kMaxVoices> delayLines;

    // Pre-allocated scratch buffers — no heap allocations in audio thread
    juce::AudioBuffer<float> voiceScratch;   ///< mono scratch for pitch shifting
    juce::AudioBuffer<float> monoInput;      ///< mono mixdown of input
    juce::AudioBuffer<float> dryBuffer;      ///< dry signal for wet/dry blend

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalThickenerEffect)
};

} // namespace ana
