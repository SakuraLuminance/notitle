#include "LocalEQ.h"
#include "SIMDSupport.h"
#include <algorithm>

namespace ana {

LocalEQ::LocalEQ()
{
    reset();
}

void LocalEQ::setHarmonicGain(int harmonicIndex, float linearGain)
{
    if (harmonicIndex >= 0 && harmonicIndex < PartialDataSIMD::kMaxPartials)
    {
        gains_[static_cast<size_t>(harmonicIndex)] = std::max(0.0f, linearGain);
    }
}

float LocalEQ::getHarmonicGain(int harmonicIndex) const
{
    if (harmonicIndex >= 0 && harmonicIndex < PartialDataSIMD::kMaxPartials)
    {
        return gains_[static_cast<size_t>(harmonicIndex)];
    }
    return 1.0f;
}

void LocalEQ::setAmount(float amount)
{
    amount_ = std::clamp(amount, 0.0f, 1.0f);
}

void LocalEQ::reset()
{
    std::fill(gains_.begin(), gains_.end(), 1.0f);
}

void LocalEQ::process(PartialDataSIMD& data)
{
    if (amount_ <= 0.0f)
        return;
        
    if (amount_ >= 0.99f)
    {
        // 100% amount: Direct SIMD multiplication
        SIMDKernels::vectorMul(data.amplitude, data.amplitude, gains_.data(), PartialDataSIMD::kMaxPartials);
    }
    else
    {
        // Interpolated amount
        // effective_gain[i] = 1.0 + (gains_[i] - 1.0) * amount_
        // amplitude[i] *= effective_gain[i]
        
        // Let's do this per active partial to save CPU, or vectorize a small loop
        for (int i = data.getNextActive(-1); i != -1; i = data.getNextActive(i))
        {
            float targetGain = gains_[static_cast<size_t>(i)];
            float effectiveGain = 1.0f + (targetGain - 1.0f) * amount_;
            data.amplitude[i] *= effectiveGain;
        }
    }
}

} // namespace ana
