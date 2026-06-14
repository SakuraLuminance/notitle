#include "DualSignalChain.h"
#include "SIMDSupport.h"
#include <cstring>

namespace ana {

// Verify that TimbrePart and PartialDataSIMD share identical layout for
// the first 3 arrays, enabling safe reinterpret_cast between them.
static_assert(offsetof(TimbrePart, frequency) == offsetof(PartialDataSIMD, frequency),
    "TimbrePart and PartialDataSIMD must have identical frequency offset");
static_assert(offsetof(TimbrePart, amplitude) == offsetof(PartialDataSIMD, amplitude),
    "TimbrePart and PartialDataSIMD must have identical amplitude offset");
static_assert(offsetof(TimbrePart, phase) == offsetof(PartialDataSIMD, phase),
    "TimbrePart and PartialDataSIMD must have identical phase offset");
static_assert(sizeof(TimbrePart::frequency) == sizeof(PartialDataSIMD::frequency),
    "TimbrePart and PartialDataSIMD frequency arrays must have same size");
static_assert(sizeof(TimbrePart::amplitude) == sizeof(PartialDataSIMD::amplitude),
    "TimbrePart and PartialDataSIMD amplitude arrays must have same size");
static_assert(sizeof(TimbrePart::phase) == sizeof(PartialDataSIMD::phase),
    "TimbrePart and PartialDataSIMD phase arrays must have same size");

DualSignalChain::DualSignalChain()
{
}

void DualSignalChain::setInputA(const TimbrePart& timbreA)
{
    inputA_ = timbreA;
}

void DualSignalChain::setInputB(const TimbrePart& timbreB)
{
    inputB_ = timbreB;
}

void DualSignalChain::process(TimbrePart& output)
{
    // Use reinterpret_cast to view TimbrePart inputs as PartialDataSIMD.
    // The first 3 arrays (frequency, amplitude, phase) have identical offsets
    // and sizes in both structs, so we process them in-place without any memcpy.
    // 
    // SpectralFilter::process() only touches these 3 arrays + calls updateActiveMask(),
    // which writes to activeMask/activeCount at PartialDataSIMD's offsets.
    // Since activeCount has different offsets in the two structs, we fix it up
    // after processing. The activeMask overlap into adjacent members is harmless
    // because we always process B first (so A's trailing overlap into B doesn't matter)
    // and B's trailing overlap goes into processedA_/filter state which get overwritten.

    // --- Process B first (A's updateActiveMask overflow into B is irrelevant) ---
    {
        auto& pB = reinterpret_cast<PartialDataSIMD&>(inputB_);
        // Build activeMask from amplitude array using SIMD compare
        for (int w = 0; w < 16; ++w)
        {
            const int base = w * 32;
            const int remaining = PartialDataSIMD::kMaxPartials - base;
            pB.activeMask[w] = SIMDKernels::vectorCompareMask(
                &inputB_.amplitude[base], 1e-6f, std::min(32, remaining));
        }
        pB.activeCount = inputB_.activeCount;
        filterB_.process(pB);
        // Copy result: inputB_ is now filtered, copy to processedB_
        processedB_ = inputB_;
    }

    // --- Process A ---
    {
        auto& pA = reinterpret_cast<PartialDataSIMD&>(inputA_);
        for (int w = 0; w < 16; ++w)
        {
            const int base = w * 32;
            const int remaining = PartialDataSIMD::kMaxPartials - base;
            pA.activeMask[w] = SIMDKernels::vectorCompareMask(
                &inputA_.amplitude[base], 1e-6f, std::min(32, remaining));
        }
        pA.activeCount = inputA_.activeCount;
        filterA_.process(pA);
        processedA_ = inputA_;
    }

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
