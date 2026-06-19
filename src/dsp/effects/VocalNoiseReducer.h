#pragma once
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "../EffectsChain.h"

namespace ana {

//==============================================================================
/**
    Real-time vocal noise reducer using spectral subtraction.

    Algorithm:
        STFT overlap-add + running-min noise estimation + per-bin spectral
        subtraction.  Pure spectral subtraction (no RNNoise/GPL code).

    FFT params:
        - 2048 pt FFT  (order = 11)
        - 75 % overlap (hop = 512)
        - Hann window

    Noise estimation:
        Running minimum per bin with separate attack / release time constants.
        When the current magnitude² drops below the estimate the attack rate
        applies; when it rises the (slower) release rate applies.

    Per-bin gain:
        G[k] = max(1 - alpha * |N[k]|^2 / |X[k]|^2, floor)
        where alpha = 2..4 depending on the Reduction parameter.

    Pre-allocates all buffers in prepare() — no heap activity in process().
*/
class VocalNoiseReducer : public EffectBase
{
public:
    VocalNoiseReducer();
    ~VocalNoiseReducer() override = default;

    //==============================================================================
    void prepare(const juce::dsp::ProcessSpec& spec) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    //==============================================================================
    /** @name Parameters */

    /** Reduction 0–100 % (default 50 %).  Maps to over-subtraction alpha = 2..4. */
    void setReduction(float percent);
    /** Minimum per-bin gain floor 0.01–0.1 (default 0.05). */
    void setFloor(float f);
    /** Noise estimation attack time 50–500 ms (default 50 ms). */
    void setAttack(float ms);
    /** Noise estimation release time 200–2000 ms (default 500 ms). */
    void setRelease(float ms);
    /** Bypass toggle. */
    void setBypass(bool b);

    //==============================================================================
    /** @name Queries */

    float getReduction() const noexcept      { return reduction_; }
    float getFloor() const noexcept          { return floor_; }
    float getAttack() const noexcept         { return attackMs_; }
    float getRelease() const noexcept        { return releaseMs_; }
    bool  isBypassed() const noexcept        { return bypassed_; }
    int   getLatencySamples() const noexcept { return latencySamples_; }

    //==============================================================================
    juce::ValueTree getState() const override;
    void setState(const juce::ValueTree& state) override;

private:
    //==============================================================================
    /** Process a single channel through the STFT spectral-subtraction pipeline. */
    void processChannel(int channel, const float* in, float* out, int numSamples,
                        float alpha, float floor, float attackCoeff, float releaseCoeff);

    /** Re-compute the over-subtraction factor alpha from the Reduction parameter. */
    void updateAlpha();

    /** Re-compute noise-estimation attack / release coefficients from the ms values. */
    void updateTimeConstants();

    //==============================================================================
    // FFT constants — all known at compile time.
    static constexpr int fftOrder_ = 11;             // 2^11 = 2048
    static constexpr int fftSize_  = 1 << fftOrder_; // 2048
    static constexpr int hopSize_  = 512;            // 75 % overlap
    static constexpr int numBins_  = fftSize_ / 2 + 1; // 1025 unique freq bins

    //==============================================================================
    // Parameters
    float reduction_ = 50.0f;   // 0 … 100 %
    float floor_      = 0.05f;  // 0.01 … 0.1
    float attackMs_   = 50.0f;  // 50 … 500 ms
    float releaseMs_  = 500.0f; // 200 … 2000 ms
    bool  bypassed_   = false;

    //==============================================================================
    // Derived coefficients
    float alpha_        = 3.0f;   // over-subtraction factor
    float attackCoeff_  = 0.0f;   // noise-decrease smoothing coeff
    float releaseCoeff_ = 0.0f;   // noise-increase smoothing coeff

    //==============================================================================
    // Processing state
    double sampleRate_    = 44100.0;
    int    numChannels_   = 2;
    int    maxBlockSize_  = 512;
    int    latencySamples_ = hopSize_; // ~12 ms at 44.1 kHz

    //==============================================================================
    // FFT engine
    std::unique_ptr<juce::dsp::FFT> fft_;

    // Pre-computed Hann window coefficients (size = fftSize_)
    std::vector<float> window_;

    // Per-channel input ring buffers (size = fftSize_ + maxBlockSize_)
    // Samples are appended at inputPos_[ch]; frames are consumed from index 0.
    std::vector<std::vector<float>> inputBuf_;
    std::vector<int>                inputPos_;

    // Per-channel overlap-add output buffers (size = fftSize_)
    std::vector<std::vector<float>> overlapBuf_;

    // Per-channel per-bin noise magnitude² estimates (size = numBins_)
    std::vector<std::vector<float>> noiseEstimate_;

    // Reusable FFT temp buffer (size = fftSize_, used during frame processing)
    std::vector<float> fftBuf_;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalNoiseReducer)
};

} // namespace ana
