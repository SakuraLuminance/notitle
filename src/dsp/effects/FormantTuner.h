#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

//==============================================================================
/**
    Creative formant manipulation effect independent of pitch.

    Wraps LPC analysis (autocorrelation + Levinson-Durbin) for vocal tract
    estimation and applies frequency-domain warping to shift, spread, or
    gender-morph formants.  Optionally morphs the spectral envelope toward
    a target vowel using formant data from FormantFilterBank presets.

    Parameters:
        FormantShift   (-12 .. +12 semitones, default 0)
        FormantSpread  (0.5 .. 2.0, default 1.0)
        Gender         (0.0 = male  ..  1.0 = female, default 0.5)
        VowelTarget    (0 = None, 1 = A, 2 = E, 3 = I, 4 = O, 5 = U)
        Mix            (0.0 = dry, 1.0 = fully wet)
*/
class FormantTuner : public EffectBase
{
public:
    FormantTuner();
    ~FormantTuner() override = default;

    //==============================================================================
    // EffectBase
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    // Parameters
    void setFormantShift(float semitones);   // -12 .. +12
    void setFormantSpread(float factor);     // 0.5 .. 2.0
    void setGender(float amount);            // 0.0 .. 1.0  (male → female)
    void setVowelTarget(int idx);            // 0=None, 1=A, 2=E, 3=I, 4=O, 5=U
    void setMix(float wet);                  // 0.0 .. 1.0

    float getFormantShift()   const noexcept { return formantShift_; }
    float getFormantSpread()  const noexcept { return formantSpread_; }
    float getGender()         const noexcept { return gender_; }
    int   getVowelTarget()    const noexcept { return vowelTarget_; }
    float getMix()            const noexcept { return mix_; }

    //==============================================================================
    // State
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& state) override;

private:
    //==============================================================================
    // LPC analysis — autocorrelation + Levinson-Durbin
    void performLpcAnalysis(const float* x, int n, int order,
                            float* coeffs, float& gain);

    //==============================================================================
    // Frequency-domain LPC warping
    //     α = 2^(-shift/12) * spread * genderScale
    //   Warps the spectral envelope such that A_new(z) ≈ A(z^α).
    void warpLpcCoeffs(const float* srcCoeffs, int order, float alpha,
                       float* dstCoeffs);

    //==============================================================================
    // Build LPC coefficients from vowel formant data (A/E/I/O/U presets)
    void vowelFormantToLpc(int vowelIdx, int order, float* coeffs, float& gain);

    //==============================================================================
    // LPC inverse (analysis) filter   e[n] = x[n] - Σ a[k]·x[n-k]
    void inverseLpcFilter(const float* x, float* residual, int n,
                          const float* coeffs, int order);

    //==============================================================================
    // LPC synthesis (all-pole) filter y[n] = residual[n] + Σ a[k]·y[n-k]
    void synthesisLpcFilter(const float* residual, float* output, int n,
                            const float* coeffs, int order);

    //==============================================================================
    // Parameters
    float formantShift_   = 0.0f;
    float formantSpread_  = 1.0f;
    float gender_         = 0.5f;
    int   vowelTarget_    = 0;     // 0 = None
    float mix_            = 1.0f;

    //==============================================================================
    // DSP spec
    double sampleRate_ = 44100.0;
    int    numChannels_ = 2;

    static constexpr int kLpcOrder    = 20;
    static constexpr int kFftSize     = 1024;   // power of 2, must cover 2*kLpcOrder
    static constexpr int kFftOrder    = 10;      // log2(kFftSize)
    static constexpr int kNumVowels   = 5;

    //==============================================================================
    // Pre-allocated scratch buffers (no heap allocations in audio thread)
    std::vector<float> scratchLpcCoeffs_;        // [kLpcOrder+1]
    std::vector<float> scratchLpcWarped_;        // [kLpcOrder+1]
    std::vector<float> scratchLpcVowel_;         // [kLpcOrder+1]
    std::vector<float> scratchResidual_;         // [maxBlockSize]
    std::vector<float> scratchFftBuf_;           // [kFftSize]
    std::vector<double> scratchLpcR_;            // [kLpcOrder+1]
    std::vector<float> scratchAprev_;            // [kLpcOrder+1]
    std::vector<float> scratchAcurr_;            // [kLpcOrder+1]
    std::vector<double> scratchPowSpec_;         // [kFftSize/2+1]
    std::vector<double> scratchPowWarped_;       // [kFftSize/2+1]

    // Dry copy for wet/dry blend
    juce::AudioBuffer<float> dryBuffer_;

    // FFT object
    std::unique_ptr<juce::dsp::FFT> fft_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FormantTuner)
};

} // namespace ana
