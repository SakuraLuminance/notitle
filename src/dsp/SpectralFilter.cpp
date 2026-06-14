#include "SpectralFilter.h"
#include <cmath>
#include <algorithm>

namespace ana {

void SpectralFilter::reset() noexcept
{
    type = Type::LowPass;
    cutoff = 1000.0f;
    resonance = 0.0f;
    harmonicProtection = 0;
}

void SpectralFilter::process(PartialDataSIMD& partials) const noexcept
{
    // Map resonance (0-1) to Q (0.707 to ~10.0)
    const float q = 0.707f + resonance * 9.293f; 

    // To avoid divide by zero:
    const float fc = std::max(20.0f, cutoff);

    for (int i = 0; i < PartialDataSIMD::kMaxPartials; ++i)
    {
        if (!partials.isActive(i))
            continue;

        // Harmonic protection
        if (i < harmonicProtection)
            continue; // Gain is 1.0, bypass filter

        const float f = partials.frequency[i];
        if (f <= 0.0f)
            continue;

        const float x = f / fc;
        const float x2 = x * x;
        const float denominator = std::sqrt((1.0f - x2) * (1.0f - x2) + (x / q) * (x / q));
        
        float gain = 1.0f;

        if (denominator > 0.0001f)
        {
            switch (type)
            {
                case Type::LowPass:
                    gain = 1.0f / denominator;
                    break;
                case Type::HighPass:
                    gain = x2 / denominator;
                    break;
                case Type::BandPass:
                    gain = (x / q) / denominator;
                    break;
            }
        }
        else
        {
            gain = 0.0f;
        }

        // Apply gain, clip to avoid massive peaks
        partials.amplitude[i] *= std::min(gain, 10.0f);
    }

    // Recompute mask since amplitudes might have dropped below threshold
    partials.updateActiveMask();
}

} // namespace ana
