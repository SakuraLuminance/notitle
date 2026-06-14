#pragma once
#include "DualTimbre.h"
#include "SpectralFilter.h"
#include <memory>

namespace ana {

//==============================================================================
/**
    DualSignalChain implements the "A/B Independent Signal Chains" architecture.
    
    It holds two independent TimbrePart sources (A and B). Each TimbrePart
    is processed by its own independent SpectralFilter (representing an independent
    insert effect/filter chain) *before* they are blended together by the DualTimbre
    blender.

    Architecture:
    Timbre A -> SpectralFilter A \
                                  +-> DualTimbre Blender -> Output Timbre
    Timbre B -> SpectralFilter B /
*/
class DualSignalChain {
public:
    DualSignalChain();
    ~DualSignalChain() = default;

    /** Set the raw input for Timbre A */
    void setInputA(const TimbrePart& timbreA);
    
    /** Set the raw input for Timbre B */
    void setInputB(const TimbrePart& timbreB);

    /** Get the filter for Timbre A to configure it */
    SpectralFilter& getFilterA() { return filterA_; }
    
    /** Get the filter for Timbre B to configure it */
    SpectralFilter& getFilterB() { return filterB_; }

    /** Get the DualTimbre blender to configure mix/mode */
    DualTimbre& getBlender() { return blender_; }

    /** 
        Process the independent chains and blend them.
        @param output The resulting blended timbre
    */
    void process(TimbrePart& output);

    /** Reset all internal states */
    void reset();

private:
    TimbrePart inputA_;
    TimbrePart inputB_;

    // Processed intermediates
    TimbrePart processedA_;
    TimbrePart processedB_;

    SpectralFilter filterA_;
    SpectralFilter filterB_;

    DualTimbre blender_;
};

} // namespace ana
