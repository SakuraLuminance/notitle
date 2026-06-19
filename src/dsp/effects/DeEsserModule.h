#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

//==============================================================================
/**
    Frequency-selective de-esser for sibilance control.

    Uses a split-band approach:
    1. HPF isolates the sibilance range (4–10 kHz)
    2. RMS envelope detector drives a compressor on that band
    3. Compressed band is reconstructed with the dry low-frequency content
    4. Listen mode solos the processed band for tuning

    Parameters:
        Threshold  — level above which reduction kicks in (-40..0 dB, default -30)
        Frequency  — HPF cutoff for sibilance band (4..10 kHz, default 6 kHz)
        Reduction  — max gain reduction applied to the sibilance band (-30..0 dB, default -10)
        Listen     — solo the processed band for monitoring (default false)
*/
class DeEsserModule : public EffectBase
{
public:
    DeEsserModule();
    ~DeEsserModule() override = default;

    //==============================================================================
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    /** @name Parameter Setters */
    void setThreshold(float db);    // -40..0   default -30
    void setFrequency(float hz);    // 4000..10000  default 6000
    void setReduction(float db);    // -30..0   default -10
    void setListen(bool listen);    // solo processed band
    void setBypass(bool b);

    //==============================================================================
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& tree) override;

private:
    //==============================================================================
    /** Re-compute HPF coefficients from current frequency_ parameter. */
    void updateFilters();

    //==============================================================================
    /** Parameter state. */
    float threshold_  = -30.0f;      // dB
    float frequency_  = 6000.0f;     // Hz
    float reduction_  = -10.0f;      // max reduction (dB)
    bool  listen_     = false;
    bool  bypassed_   = false;

    //==============================================================================
    /** Processing state. */
    double sampleRate_      = 44100.0;
    int    blockSize_       = 512;
    float  envelope_        = 0.0f;  // smoothed envelope follower
    float  attackCoeff_     = 0.0f;
    float  releaseCoeff_    = 0.0f;

    //==============================================================================
    /** HPF isolating the sibilance band. */
    juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>> hpf_;
    bool filtersDirty_ = true;

    //==============================================================================
    /** Pre-allocated processing buffers. */
    juce::AudioBuffer<float> highBand_;   // HPF output (sibilance band)
    juce::AudioBuffer<float> dryBuffer_;  // copy of input for reconstruction

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeEsserModule)
};

} // namespace ana
