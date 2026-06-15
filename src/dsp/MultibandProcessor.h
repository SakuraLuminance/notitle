#pragma once
#include <vector>
#include <memory>
#include "PartialDataSIMD.h"

namespace ana {

struct FrequencyBand {
    float lowHz = 0.0f;
    float highHz = 20000.0f;
    float gain = 1.0f;
    bool bypassed = false;
};

class MultibandProcessor {
public:
    MultibandProcessor() = default;
    
    void setNumBands(int numBands);
    void setBandRange(int bandIndex, float lowHz, float highHz);
    void setBandGain(int bandIndex, float gain);
    void bypassBand(int bandIndex, bool bypass);
    
    void process(PartialDataSIMD& partials);
    
    int getNumBands() const;
    const FrequencyBand& getBand(int index) const;
    
private:
    std::vector<FrequencyBand> bands_;
};

} // namespace ana
