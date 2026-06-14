#pragma once
#include <vector>
#include "PartialData.h"
#include "STFTConfig.h"

namespace ana {

class ResynthesisEngine
{
public:
    ResynthesisEngine();
    ~ResynthesisEngine();

    std::vector<float> resynthesize(
        const PartialData& partialData,
        const STFTConfig& config);
};

} // namespace ana
