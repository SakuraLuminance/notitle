#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>

#include "PartialData.h"
#include "STFTConfig.h"

namespace ana {

struct alignas(32) PartialDataSIMD
{
    static constexpr int kMaxPartials = 512;

    // SoA data (SIMD-friendly)
    alignas(32) float frequency[kMaxPartials];   // Hz
    alignas(32) float amplitude[kMaxPartials];   // 0.0 - 1.0
    alignas(32) float phase[kMaxPartials];       // radians

    // Active partial tracking (hybrid mode)
    uint32_t activeMask[(kMaxPartials + 31) / 32]; // 16 uint32_t bitmask
    int      activeCount = 0;                      // number of active partials

    // Metadata
    int    maxPartials = kMaxPartials;
    double sampleRate  = 44100.0;
    double hopSize     = 512.0;

    // --- Constructors -------------------------------------------------------

    PartialDataSIMD()
    {
        std::memset(frequency, 0, sizeof(frequency));
        std::memset(amplitude, 0, sizeof(amplitude));
        std::memset(phase, 0, sizeof(phase));
        std::memset(activeMask, 0, sizeof(activeMask));
        activeCount = 0;
    }

    // --- Conversion (legacy AoS -> SIMD SoA) --------------------------------

    static PartialDataSIMD fromPartialData(const PartialData& src)
    {
        PartialDataSIMD dst;

        dst.maxPartials = src.maxPartials;
        dst.sampleRate  = src.sampleRate;
        dst.hopSize     = src.hopSize;

        // Process the last frame's partials (most recent data)
        if (!src.frames.empty())
        {
            const auto& frame = src.frames.back();
            const int count = std::min(static_cast<int>(frame.partials.size()),
                                       dst.maxPartials);

            for (int i = 0; i < count; ++i)
            {
                const auto& p = frame.partials[i];
                dst.frequency[i] = p.frequency;
                dst.amplitude[i] = p.amplitude;
                dst.phase[i]     = p.phase;
            }
        }

        dst.updateActiveMask();
        return dst;
    }

    // --- Conversion (SIMD SoA -> legacy AoS) --------------------------------

    PartialData toPartialData() const
    {
        PartialData dst;
        dst.maxPartials = maxPartials;
        dst.sampleRate  = sampleRate;
        dst.hopSize     = hopSize;

        PartialFrame frame;
        frame.partials.reserve(static_cast<size_t>(maxPartials));

        for (int i = 0; i < maxPartials; ++i)
        {
            if (isActive(i))
            {
                Partial p;
                p.frequency = frequency[i];
                p.amplitude = amplitude[i];
                p.phase     = phase[i];
                frame.partials.push_back(p);
            }
        }

        dst.frames.push_back(std::move(frame));
        return dst;
    }

    // --- Mask operations ----------------------------------------------------

    void updateActiveMask()
    {
        static constexpr float kThreshold = 1e-6f;


        std::memset(activeMask, 0, sizeof(activeMask));
        activeCount = 0;

        for (int i = 0; i < kMaxPartials; ++i)
        {
            if (amplitude[i] > kThreshold)
            {
                const int word = i >> 5;       // i / 32
                const int bit  = i & 31;       // i % 32
                activeMask[word] |= (1u << bit);
                ++activeCount;
            }
        }
    }

    bool isActive(int index) const
    {
        if (index < 0 || index >= kMaxPartials)
            return false;

        const int word = index >> 5;
        const int bit  = index & 31;
        return (activeMask[word] & (1u << bit)) != 0;
    }

    int getNextActive(int startFrom) const
    {
        if (startFrom < 0 || startFrom >= kMaxPartials)
            return -1;

        for (int i = startFrom; i < kMaxPartials; ++i)
        {
            if (isActive(i))
                return i;
        }

        return -1; // no more active partials
    }
};

static_assert(sizeof(PartialDataSIMD::activeMask) == sizeof(uint32_t) * 16,
              "activeMask must be exactly 16 uint32_t words");

} // namespace ana
