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
    // We use a stack-local PartialDataSIMD to safely process through
    // SpectralFilter (which expects PartialDataSIMD layout with activeMask
    // and activeCount at SIMD-specific offsets). The frequency/amplitude/phase
    // data is copied in/out via memcpy; activeMask is built via SIMD kernel.

    constexpr size_t kArrayBytes = sizeof(PartialDataSIMD::frequency);

    // --- Process B ---
    {
        PartialDataSIMD pB;

        // Copy SoA arrays (safe memcpy — shared layout verified by static_asserts above)
        std::memcpy(pB.frequency, inputB_.frequency, kArrayBytes);
        std::memcpy(pB.amplitude, inputB_.amplitude, kArrayBytes);
        std::memcpy(pB.phase,     inputB_.phase,     kArrayBytes);
        pB.activeCount = inputB_.activeCount;

        // Build activeMask from amplitude array using SIMD compare
        for (int w = 0; w < 16; ++w)
        {
            const int base = w * 32;
            const int remaining = PartialDataSIMD::kMaxPartials - base;
            pB.activeMask[w] = SIMDKernels::vectorCompareMask(
                &pB.amplitude[base], 1e-6f, std::min(32, remaining));
        }

        filterB_.process(pB);

        // Copy results back
        std::memcpy(processedB_.frequency, pB.frequency, kArrayBytes);
        std::memcpy(processedB_.amplitude, pB.amplitude, kArrayBytes);
        std::memcpy(processedB_.phase,     pB.phase,     kArrayBytes);
        processedB_.activeCount = pB.activeCount;
    }

    // --- Process A ---
    {
        PartialDataSIMD pA;

        std::memcpy(pA.frequency, inputA_.frequency, kArrayBytes);
        std::memcpy(pA.amplitude, inputA_.amplitude, kArrayBytes);
        std::memcpy(pA.phase,     inputA_.phase,     kArrayBytes);
        pA.activeCount = inputA_.activeCount;

        for (int w = 0; w < 16; ++w)
        {
            const int base = w * 32;
            const int remaining = PartialDataSIMD::kMaxPartials - base;
            pA.activeMask[w] = SIMDKernels::vectorCompareMask(
                &pA.amplitude[base], 1e-6f, std::min(32, remaining));
        }

        filterA_.process(pA);

        std::memcpy(processedA_.frequency, pA.frequency, kArrayBytes);
        std::memcpy(processedA_.amplitude, pA.amplitude, kArrayBytes);
        std::memcpy(processedA_.phase,     pA.phase,     kArrayBytes);
        processedA_.activeCount = pA.activeCount;
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
