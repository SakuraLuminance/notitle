#include "PartialModulator.h"
#include <cmath>

namespace ana {

//==============================================================================
void PartialModulator::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

//==============================================================================
void PartialModulator::process(PartialDataSIMD& partials, const Config& config)
{
    const double invSampleRate = 1.0 / sampleRate_;
    const float  twoPi         = 6.283185307179586f;
    const float  lfoIncrement  = static_cast<float>(config.lfoRate * invSampleRate);

    // Pre-compute envelope time constants (seconds -> per-sample increment)
    const float attackRate  = (config.attack  > 0.0f) ? (1.0f / (static_cast<float>(sampleRate_) * config.attack))  : 1.0f;
    const float decayRate   = (config.decay   > 0.0f) ? (1.0f / (static_cast<float>(sampleRate_) * config.decay))   : 1.0f;
    const float releaseRate = (config.release > 0.0f) ? (1.0f / (static_cast<float>(sampleRate_) * config.release)) : 1.0f;

    const float lfoDepth = config.lfoDepth;
    const float sustain  = config.sustain;

    // Shared-phase advancement: when !perPartialPhase we advance partial 0's
    // phase once and all partials read from it.  If no partials are active we
    // still need to keep the shared phase running so it doesn't snap later.
    if (!config.perPartialPhase && lfoDepth > 0.0f)
    {
        states_[0].lfoPhase += lfoIncrement;
        if (states_[0].lfoPhase >= 1.0f)
            states_[0].lfoPhase -= 1.0f;
    }

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (!partials.isActive(i))
        {
            // Inactive partial: advance envelope state into release if not already there
            if (states_[i].envState < 3)
                states_[i].envState = 3;
            continue;
        }

        auto& state = states_[i];

        // --- 1. Advance LFO phase (per-partial or shared) ---
        if (config.perPartialPhase)
        {
            state.lfoPhase += lfoIncrement;
            if (state.lfoPhase >= 1.0f)
                state.lfoPhase -= 1.0f;
        }

        // --- 2. Compute LFO value ---
        float lfoValue = 0.0f;
        if (lfoDepth > 0.0f)
        {
            const float phase = config.perPartialPhase ? state.lfoPhase : states_[0].lfoPhase;
            lfoValue = std::sin(twoPi * phase) * lfoDepth;
        }

        // --- 3. Update envelope state machine (ADSR) ---
        switch (state.envState)
        {
            case 0: // Attack
                state.envLevel += attackRate;
                if (state.envLevel >= 1.0f)
                {
                    state.envLevel = 1.0f;
                    state.envState = 1;
                }
                break;

            case 1: // Decay
                state.envLevel -= decayRate;
                if (state.envLevel <= sustain)
                {
                    state.envLevel = sustain;
                    state.envState = 2;
                }
                break;

            case 2: // Sustain — hold at sustain level
                break;

            case 3: // Release
                state.envLevel -= releaseRate;
                if (state.envLevel <= 0.0f)
                    state.envLevel = 0.0f;
                break;
        }

        // Safety clamp
        if (state.envLevel < 0.0f) state.envLevel = 0.0f;
        if (state.envLevel > 1.0f) state.envLevel = 1.0f;

        // --- 4. Apply modulation: amplitude *= (1 + LFO * env) ---
        partials.amplitude[i] *= (1.0f + lfoValue * state.envLevel);
    }
}

//==============================================================================
void PartialModulator::reset()
{
    for (auto& s : states_)
    {
        s.lfoPhase = 0.0f;
        s.envLevel = 0.0f;
        s.envState = 0; // start in attack
    }
}

} // namespace ana
