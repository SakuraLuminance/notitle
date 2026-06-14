#pragma once
#include "PartialData.h"
#include "STFTConfig.h"

namespace ana {

class PhasePropagation
{
public:
    PhasePropagation();
    ~PhasePropagation();

    void propagatePhases(
        PartialData& partialData,
        const STFTConfig& config);
};

} // namespace ana
