#include "StepSequencer.h"
#include <algorithm>

namespace ana {

//==============================================================================
StepSequencer::StepSequencer()
{
    // Default pattern: all 16 steps active, value ramps 0 → 1
    for (int i = 0; i < 16; ++i)
    {
        steps_[i].active = true;
        steps_[i].value  = static_cast<float>(i) / 15.0f;
    }
}

//==============================================================================
void StepSequencer::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void StepSequencer::reset()
{
    currentStep_   = -1;  // -1 = no step active; first advanceStep() plays step 0
    sampleCounter_ = 0.0;
    gateHigh_      = false;
    currentValue_  = 0.0f;
    direction_     = 1;
}

//==============================================================================
void StepSequencer::setBpm(double bpm)
{
    bpm_ = juce::jlimit(1.0, 999.0, bpm);
}

void StepSequencer::setRateBeats(float beats)
{
    rateBeats_ = std::max(0.03125f, beats); // minimum 1/32 note
}

//==============================================================================
void StepSequencer::externalTick()
{
    if (!enabled_)
        return;

    advanceStep();
}

void StepSequencer::trigger()
{
    currentStep_   = 0;
    sampleCounter_ = 0.0;
    direction_     = 1;
}

//==============================================================================
void StepSequencer::setStep(int index, bool active, float value)
{
    if (index >= 0 && index < 16)
    {
        steps_[index].active = active;
        steps_[index].value  = juce::jlimit(0.0f, 1.0f, value);
    }
}

const SeqStep& StepSequencer::getStep(int index) const
{
    static const SeqStep empty;
    if (index >= 0 && index < 16)
        return steps_[index];
    return empty;
}

//==============================================================================
void StepSequencer::process(int numSamples)
{
    if (!enabled_ || sampleRate_ <= 0.0)
    {
        gateHigh_     = false;
        currentValue_ = 0.0f;
        return;
    }

    if (clockSource_ == SeqClockSource::Internal)
    {
        const double stepDur = getStepDurationSamples();
        if (stepDur <= 0.0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            sampleCounter_ += 1.0;

            if (sampleCounter_ >= stepDur)
            {
                sampleCounter_ -= stepDur;
                advanceStep();
            }
        }
    }
    // In external clock mode, steps are advanced by externalTick() only.
    // The current value is simply maintained.
}

//==============================================================================
void StepSequencer::advanceStep()
{
    // Handle initial state (currentStep_ == -1) — always start at step 0
    if (currentStep_ < 0)
    {
        switch (playMode_)
        {
            case SeqPlayMode::Backward: currentStep_ = 15; break;
            case SeqPlayMode::PingPong: currentStep_ = 0;  direction_ = 1; break;
            case SeqPlayMode::Random:
            case SeqPlayMode::Forward:
            default:                   currentStep_ = 0;  break;
        }
    }
    else
    {
        switch (playMode_)
        {
            case SeqPlayMode::Forward:
            {
                currentStep_ = (currentStep_ + 1) % 16;
                break;
            }

            case SeqPlayMode::Backward:
            {
                currentStep_ = (currentStep_ - 1 + 16) % 16;
                break;
            }

            case SeqPlayMode::PingPong:
            {
                currentStep_ += direction_;
                if (currentStep_ >= 15)
                {
                    currentStep_ = 15;
                    direction_   = -1;
                }
                else if (currentStep_ <= 0)
                {
                    currentStep_ = 0;
                    direction_   = 1;
                }
                break;
            }

            case SeqPlayMode::Random:
            {
                std::uniform_int_distribution<int> dist(0, 15);
                currentStep_ = dist(rng_);
                break;
            }
        }
    }

    // Update output
    const auto& step = steps_[currentStep_];
    gateHigh_        = step.active;
    currentValue_    = step.active ? step.value : 0.0f;
}

//==============================================================================
double StepSequencer::getStepDurationSamples() const noexcept
{
    // step duration = (60 / bpm) * rateBeats * sampleRate
    return (60.0 / bpm_) * static_cast<double>(rateBeats_) * sampleRate_;
}

} // namespace ana
