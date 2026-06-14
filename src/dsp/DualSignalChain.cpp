#include "DualSignalChain.h"
#include "SIMDSupport.h"
#include <cstring>

namespace ana {

DualSignalChain::DualSignalChain()
{
}

void DualSignalChain::setInputA(const TimbrePart& timbreA)
{
    std::memcpy(&inputA_, &timbreA, sizeof(TimbrePart));
}

void DualSignalChain::setInputB(const TimbrePart& timbreB)
{
    std::memcpy(&inputB_, &timbreB, sizeof(TimbrePart));
}

void DualSignalChain::process(TimbrePart& output)
{
    // 1. Copy inputs to processing buffers
    // In a real SIMD architecture, we might cast TimbrePart to PartialDataSIMD 
    // since their memory layout is identical for the first 3 arrays.
    
    // We need PartialDataSIMD to pass into SpectralFilter.
    // They are binary compatible, but strictly speaking we should copy.
    PartialDataSIMD pA;
    std::memcpy(pA.frequency, inputA_.frequency, sizeof(pA.frequency));
    std::memcpy(pA.amplitude, inputA_.amplitude, sizeof(pA.amplitude));
    std::memcpy(pA.phase,     inputA_.phase,     sizeof(pA.phase));
    pA.activeCount = inputA_.activeCount;
    // Build bitmask
    for(int i=0; i<PartialDataSIMD::kMaxPartials; ++i) {
        if(pA.amplitude[i] > 1e-6f) pA.activeMask[i/32] |= (1u << (i%32));
    }

    PartialDataSIMD pB;
    std::memcpy(pB.frequency, inputB_.frequency, sizeof(pB.frequency));
    std::memcpy(pB.amplitude, inputB_.amplitude, sizeof(pB.amplitude));
    std::memcpy(pB.phase,     inputB_.phase,     sizeof(pB.phase));
    pB.activeCount = inputB_.activeCount;
    for(int i=0; i<PartialDataSIMD::kMaxPartials; ++i) {
        if(pB.amplitude[i] > 1e-6f) pB.activeMask[i/32] |= (1u << (i%32));
    }

    // 2. Process independent filter chains
    filterA_.process(pA);
    filterB_.process(pB);

    // 3. Copy back to TimbreParts
    std::memcpy(processedA_.frequency, pA.frequency, sizeof(processedA_.frequency));
    std::memcpy(processedA_.amplitude, pA.amplitude, sizeof(processedA_.amplitude));
    std::memcpy(processedA_.phase,     pA.phase,     sizeof(processedA_.phase));
    processedA_.activeCount = pA.activeCount;

    std::memcpy(processedB_.frequency, pB.frequency, sizeof(processedB_.frequency));
    std::memcpy(processedB_.amplitude, pB.amplitude, sizeof(processedB_.amplitude));
    std::memcpy(processedB_.phase,     pB.phase,     sizeof(processedB_.phase));
    processedB_.activeCount = pB.activeCount;

    // 4. Blend
    blender_.setTimbre1(processedA_);
    blender_.setTimbre2(processedB_);
    blender_.process(output);
}

void DualSignalChain::reset()
{
    inputA_.clear();
    inputB_.clear();
    processedA_.clear();
    processedB_.clear();
    filterA_.reset();
    filterB_.reset();
}

} // namespace ana
