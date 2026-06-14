#pragma once
#include "PartialDataSIMD.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ana {

class BlurEffect
{
public:
    BlurEffect();
    ~BlurEffect() = default;

    // Time blur parameters (horizontal - along time axis)
    void setAttackBlur(float ms);   // Leading blur time in ms (0-1000)
    void setDecayBlur(float ms);    // Trailing blur time in ms (0-1000)

    // Harmonic blur parameters (vertical - along frequency axis)
    void setHarmonicBlur(float amount);    // 0.0 to 1.0
    void setTopTension(float tension);     // 0.0 to 1.0 high freq tension
    void setBottomTension(float tension);  // 0.0 to 1.0 low freq tension

    // Mix
    void setMix(float mix);  // 0.0 to 1.0 wet/dry

    // Sample rate
    void setSampleRate(double sr);

    // Process a single frame
    void process(PartialDataSIMD& data);

    // Reset internal state
    void reset();

private:
    // Apply temporal blur (horizontal)
    void applyTemporalBlur(PartialDataSIMD& data);

    // Apply harmonic blur (vertical)
    void applyHarmonicBlur(PartialDataSIMD& data);

    float attackBlurMs_   = 0.0f;
    float decayBlurMs_    = 0.0f;
    float harmonicBlur_   = 0.0f;
    float topTension_     = 0.5f;
    float bottomTension_  = 0.5f;
    float mix_            = 1.0f;
    double sampleRate_    = 44100.0;

    // Temporal state (simple 1-pole IIR for decay)
    float prevAmplitudes_[PartialDataSIMD::kMaxPartials] = {0.0f};

    // Cached temporal-blur coefficient (recomputed when decayBlurMs_ changes)
    float cachedAlpha_       = 0.0f;
    float lastDecayBlurMs_   = -1.0f;

    // Pre-allocated scratch buffers
    float scratch_originalAmps_[PartialDataSIMD::kMaxPartials];
    float scratch_workingAmps_[PartialDataSIMD::kMaxPartials];
    float scratch_prefixSum_[PartialDataSIMD::kMaxPartials + 1];  // for O(n) running-sum blur

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlurEffect)
};

} // namespace ana
