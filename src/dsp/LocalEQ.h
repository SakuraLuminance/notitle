#pragma once
#include "PartialDataSIMD.h"
#include <vector>
#include <array>

namespace ana {

//==============================================================================
/**
    Local EQ applies a frequency response curve relative to the fundamental
    frequency. Because AnaPlug uses additive synthesis, we can implement Local EQ
    by simply scaling the amplitude of each harmonic index (partial index),
    rather than filtering absolute frequencies in Hz.
*/
class LocalEQ {
public:
    LocalEQ();
    ~LocalEQ() = default;

    /** 
        Set the gain for a specific harmonic (0 = fundamental, 1 = 2nd harmonic, etc.)
        Gain is linear (1.0 = no change, 0.0 = silence, >1.0 = boost).
    */
    void setHarmonicGain(int harmonicIndex, float linearGain);

    /**
        Get the gain for a specific harmonic.
    */
    float getHarmonicGain(int harmonicIndex) const;

    /**
        Set the overall mix of the Local EQ effect (0.0 to 1.0).
    */
    void setAmount(float amount);

    /**
        Reset all harmonic gains to 1.0 (flat response).
    */
    void reset();

    /**
        Apply the Local EQ to the partial data.
        Since PartialDataSIMD stores partials in harmonic order (i = harmonic index),
        this simply multiplies the amplitude array by the local EQ curve.
    */
    void process(PartialDataSIMD& data);

private:
    std::array<float, PartialDataSIMD::kMaxPartials> gains_;
    float amount_ = 1.0f;
};

} // namespace ana
