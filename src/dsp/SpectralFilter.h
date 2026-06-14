#pragma once

#include "PartialDataSIMD.h"

namespace ana {

class SpectralFilter
{
public:
    enum class Type
    {
        LowPass,
        HighPass,
        BandPass
    };

    SpectralFilter() = default;

    void setType(Type newType) noexcept { type = newType; }
    void setCutoff(float newCutoff) noexcept { cutoff = newCutoff; }
    void setResonance(float newResonance) noexcept { resonance = newResonance; }
    
    /**
     * Sets the number of lower partials that are exempt from filtering.
     * For example, passing 1 protects the fundamental. Passing 2 protects the fundamental and the 1st harmonic.
     */
    void setHarmonicProtection(int numPartials) noexcept { harmonicProtection = numPartials; }

    void process(PartialDataSIMD& partials) const noexcept;

private:
    Type type = Type::LowPass;
    float cutoff = 1000.0f;
    float resonance = 0.0f; // 0.0 to 1.0
    int harmonicProtection = 0;
};

} // namespace ana
