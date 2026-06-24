#pragma once
#include <random>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ana {

//==============================================================================
/**
    Seed-based parameter randomizer with Mersenne Twister PRNG.

    Applies multiplicative randomization to parameter values:
        newValue = baseValue * (1.0f + uniform(-0.5f, 0.5f) * rangePercent / 100.0f)

    The seed is stored in plugin presets so the same randomization can be
    recalled deterministically.  Range is user-selectable from a small set
    of percentages (±5%, ±10%, ±25%, ±50%).

    Thread-safety: intended for message-thread use only (not audio-thread safe).
*/
class Randomizer
{
public:
    /** Creates a Randomizer with a random seed and default ±25% range. */
    Randomizer();

    //==============================================================================
    /** Sets the PRNG seed.  Calling this resets the generator to a deterministic
        state based on the given seed.  Pass 0 to use std::random_device.
    */
    void setSeed(unsigned int seed);

    /** Returns the current seed. */
    unsigned int getSeed() const noexcept { return seed_; }

    //==============================================================================
    /** Sets the range percentage (5, 10, 25, or 50). */
    void setRangePercent(float percent);

    /** Returns the current range percentage. */
    float getRangePercent() const noexcept { return rangePercent_; }

    //==============================================================================
    /** Applies randomization to a single parameter value.

        @param baseValue  The current (pre-randomization) parameter value.
        @param min        Minimum allowed value (clamp floor).
        @param max        Maximum allowed value (clamp ceiling).
        @return Randomized value clamped to [min, max].
    */
    float apply(float baseValue, float min, float max);

    //==============================================================================
    /** Generates a new random seed using std::random_device. */
    void reseed();

    //==============================================================================
    /** Serialises the seed to a ValueTree for preset storage. */
    juce::ValueTree getState() const;

    /** Restores the seed from a ValueTree. */
    void setState(const juce::ValueTree& tree);

private:
    //==============================================================================
    unsigned int seed_ = 0;
    float rangePercent_ = 25.0f;

    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_{-0.5f, 0.5f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Randomizer)
};

} // namespace ana
