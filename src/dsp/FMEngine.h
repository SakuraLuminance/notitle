#pragma once
#include "PartialDataSIMD.h"

namespace ana {

class FMEngine {
public:
    FMEngine() = default;

    void processFM(PartialDataSIMD& partials,
                   const PartialDataSIMD& modulator,
                   float modIndex);

    void processAM(PartialDataSIMD& partials,
                   const PartialDataSIMD& modulator,
                   float modDepth);

    void setFMIndex(float index);
    void setAMDepth(float depth);

private:
    float fmIndex_ = 0.0f;
    float amDepth_ = 0.0f;
};

} // namespace ana
