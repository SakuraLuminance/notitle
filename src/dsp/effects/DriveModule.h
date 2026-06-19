#pragma once
#include <atomic>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

//==============================================================================
/**
    Multi-mode drive/distortion module.

    Merges DistortionEffect, SaturationEffect, BitcrusherEffect, and
    RingModulatorEffect into a single consolidated class with seven modes.

    Modes:
        Soft  — tanh(preGain * input)          symmetric
        Tube  — tanh asymmetric                 (different gain pos/neg)
        Tape  — atan(preGain * input) / (pi/2)  with oversampling
        Hard  — hard clip at ±threshold
        Fold  — wavefolder
        Crush — quantise + downsample           with oversampling
        Ring  — input * sine carrier

    Shared parameter mappings:
        Drive  0…1  → pre-gain / threshold / fold / bit-depth / carrier-freq
        Tone   0…1  → post-process LP cutoff    (0=dark, 1=bright)
        Mix    0…1  → wet/dry blend
        WetHPF      → high-pass on wet signal    (Hz)
        WetLPF      → low-pass on wet signal     (Hz)
*/
enum class DriveMode
{
    Soft,   // 0
    Tube,   // 1
    Tape,   // 2
    Hard,   // 3
    Fold,   // 4
    Crush,  // 5
    Ring    // 6
};

class DriveModule : public EffectBase
{
public:
    DriveModule();
    ~DriveModule() override = default;

    //==============================================================================
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    /** @name Parameter Setters */
    void setMode(DriveMode m);
    void setDrive(float d01);          // 0…1
    void setTone(float t01);           // 0…1  (0 = dark, 1 = bright)
    void setMix(float m01);            // 0…1
    void setWetHPF(float hz);          // high-pass cutoff (Hz)
    void setWetLPF(float hz);          // low-pass cutoff  (Hz)
    void setBypass(bool b);
    void setGain(float g);             // output gain 0…4

    //==============================================================================
    int getLatencySamples() const noexcept { return latencySamples_; }

    //==============================================================================
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& tree) override;

private:
    //==============================================================================
    /** Process a block without oversampling (Soft, Tube, Hard, Fold, Ring). */
    void processNative(juce::dsp::AudioBlock<float>& block, DriveMode mode, float preGain);

    /** Process a block through oversampling (Tape, Crush). */
    void processOversampled(juce::dsp::AudioBlock<float>& block, DriveMode mode);

    //==============================================================================
    /** Per-sample waveshaping helpers (stateless). */
    static float softClip(float x, float g);
    static float tubeClip(float x, float g);
    static float tapeClip(float x, float g);
    static float hardClip(float x, float threshold);
    static float waveFold(float x, float foldFactor);

    /** Per-channel Ring processing (stateful). */
    void processRing(int channel, float* data, int numSamples, float freq);

    //==============================================================================
    /** Update filter coefficients. */
    void updateToneFilter();
    void updateWetFilters();

    //==============================================================================
    /** Parameter state (atomic mode for cross-thread safety). */
    std::atomic<int> modeAtomic_{ static_cast<int>(DriveMode::Soft) };
    float drive_     = 0.5f;
    float tone_      = 1.0f;              // 0…1
    float mix_       = 1.0f;
    float wetHPF_    = 20.0f;
    float wetLPF_    = 20000.0f;
    bool  bypassed_  = false;
    float gain_      = 1.0f;

    //==============================================================================
    /** Processing specs. */
    double sampleRate_    = 44100.0;
    int    latencySamples_ = 0;
    int    numChannels_   = 2;
    int    blockSize_     = 512;

    //==============================================================================
    /** Oversampling (4x, used for Tape and Crush modes). */
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling_;

    //==============================================================================
    /** Post-process tone filter — single-pole LP per channel. */
    struct OnePoleLP { float y1 = 0.0f; float a = 0.0f; };
    std::vector<OnePoleLP> toneFilters_;
    bool  toneFilterDirty_ = true;
    float toneCutoff_      = 20000.0f;

    //==============================================================================
    /** Wet-signal HPF / LPF — same IIR design as EffectsChain. */
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> wetHPFFilter_;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> wetLPFFilter_;
    bool wetFiltersDirty_ = true;

    //==============================================================================
    /** Ring-modulator state (per channel). */
    std::vector<float> phase_;        // [0, 1)  for phase accumulator
    std::vector<float> phasorCos_;    // recursive phasor cos
    std::vector<float> phasorSin_;    // recursive phasor sin
    float cosDelta_ = 1.0f;           // rotation coeffs (recomputed in setDrive)
    float sinDelta_ = 0.0f;
    float ringFreq_ = 100.0f;         // carrier frequency (set from drive)

    //==============================================================================
    /** Bitcrusher state (per channel). */
    std::vector<float> heldSample_;
    std::vector<int>   sampleCounter_;
    float quantizeFactor_ = 256.0f;

    //==============================================================================
    /** Dry-signal buffer for wet/dry blend. */
    juce::AudioBuffer<float> dryBuffer_;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriveModule)
};

} // namespace ana
