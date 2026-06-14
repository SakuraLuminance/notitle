#pragma once

#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "SIMDSupport.h"

namespace ana {

//==============================================================================
/** Available filter types for the multi-filter system. */
enum class FilterType
{
    LowPass,
    HighPass,
    BandPass,
    Notch,
    AllPass,
    Comb,
    Formant,
    Morph
};

//==============================================================================
/** Routing modes for connecting multiple filters. */
enum class RoutingMode
{
    Serial,   /**< Filters applied in sequence, output feeds next input. */
    Parallel, /**< Each filter processes the same input, results blended. */
    Split     /**< Frequency split: low band → first filters, high band → rest. */
};

//==============================================================================
/** Per-filter parameters. */
struct FilterParams
{
    double cutoff = 1000.0;      /**< Cutoff frequency in Hz (20-20000). */
    float resonance = 0.0f;      /**< Resonance/Q (0-1 normalized). */
    float drive = 0.0f;          /**< Pre-filter drive (0-1, maps to 0-24dB). */
    float mix = 1.0f;            /**< Wet/dry blend (0-1). */

    // Split routing crossover frequencies (Hz)
    double crossoverLow = 200.0;
    double crossoverHigh = 2000.0;

    // Morph filter source and target types
    FilterType morphSource = FilterType::LowPass;
    FilterType morphTarget = FilterType::HighPass;
    float morphAmount = 0.0f;    /**< Morph interpolation (0-1). */
};

// Comparison operator for change detection
inline bool operator==(const FilterParams& a, const FilterParams& b) noexcept
{
    return a.cutoff == b.cutoff
        && a.resonance == b.resonance
        && a.drive == b.drive
        && a.mix == b.mix
        && a.crossoverLow == b.crossoverLow
        && a.crossoverHigh == b.crossoverHigh
        && a.morphSource == b.morphSource
        && a.morphTarget == b.morphTarget
        && a.morphAmount == b.morphAmount;
}

inline bool operator!=(const FilterParams& a, const FilterParams& b) noexcept
{
    return ! (a == b);
}

//==============================================================================
/** A single filter slot holding its type, parameters, and DSP objects. */
struct FilterSlot
{
    FilterType type = FilterType::LowPass;
    FilterParams params;
    bool bypassed = false;

    // Used by LP, HP, BP, Notch, AllPass, Morph
    juce::dsp::IIR::Filter<float> iirFilter;

    // Used by Comb
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;

    // Used by Formant (4 peaking filters)
    std::vector<juce::dsp::IIR::Filter<float>> formantFilters;

    // Used by Morph — target coefficient set
    juce::dsp::IIR::Coefficients<float>::Ptr morphCoeffs;

    // Comb filter damping state (per-slot, not global - thread-safety)
    float lastDelayed[2] = {};

    //==============================================================================
    // Coefficient caching support
    bool coefficientsDirty = true;
    FilterParams lastParams;
};

//==============================================================================
/**
    Multi-Filter system with 8 filter types and serial/parallel/split routing.

    Models Harmor's flexible filter architecture with gain staging, per-filter
    drive and mix, frequency response calculation, and filter morphing.
*/
class MultiFilter
{
public:
    MultiFilter();
    ~MultiFilter();

    //==============================================================================
    /** Initialises all filters with the given process spec. */
    void prepare(const juce::dsp::ProcessSpec& spec);

    /** Resets all filter states to zero. */
    void reset();

    //==============================================================================
    /** Adds a new filter slot and returns its index. */
    int addSlot(FilterType type, const FilterParams& params = FilterParams{});

    /** Removes the filter slot at the given index. */
    void removeSlot(int index);

    /** Removes all filter slots. */
    void clearSlots();

    /** Returns a reference to the filter slot at the given index. */
    FilterSlot& getSlot(int index);

    /** Returns the number of active filter slots. */
    int getNumSlots() const noexcept;

    //==============================================================================
    void setRoutingMode(RoutingMode mode) noexcept;
    RoutingMode getRoutingMode() const noexcept;

    //==============================================================================
    /** Processes the audio buffer through the multi-filter chain. */
    void process(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    /**
        Computes the magnitude response (linear) at each given frequency.
        Useful for visualisation / analyser integration.
    */
    std::vector<float> getFrequencyResponse(const std::vector<float>& frequencies) const;

    //==============================================================================
    void setMasterGain(float gain) noexcept;
    float getMasterGain() const noexcept;

    /** Marks all filter slots' coefficients as dirty, forcing recomputation
        on the next process() call.  Call this after modifying any slot's params
        directly through getSlot(). */
    void markCoefficientsDirty() noexcept;

private:
    // Coefficient management
    void updateCoefficients(FilterSlot& slot);
    void updateFormantCoefficients(FilterSlot& slot);
    void updateMorphCoefficients(FilterSlot& slot);

    // Routing logic
    void processSerial(juce::dsp::AudioBlock<float>& block);
    void processParallel(juce::AudioBuffer<float>& buffer);
    void processSplit(juce::dsp::AudioBlock<float>& block);

    // Slot-type-specific processors
    void processCombSlot(FilterSlot& slot, juce::dsp::AudioBlock<float>& block);
    void processFormantSlot(FilterSlot& slot, juce::dsp::AudioBlock<float>& block);

    // Frequency response helpers
    static float evalBiquadResponse(float frequency, double sampleRate,
                                     const juce::dsp::IIR::Coefficients<float>& coeffs);
    float evalSlotResponse(const FilterSlot& slot, float frequency) const;

    //==============================================================================
    juce::dsp::ProcessSpec spec;
    std::vector<FilterSlot> slots;
    RoutingMode currentRouting = RoutingMode::Serial;
    float masterGainValue = 1.0f;
    double currentSampleRate = 44100.0;

    // Scratch buffers for parallel and split processing
    juce::AudioBuffer<float> parallelScratch;
    juce::AudioBuffer<float> dryScratch;

    // Scratch buffers for formant filter processing (avoids per-call allocation)
    juce::AudioBuffer<float> formantScratch;
    juce::AudioBuffer<float> formantAccum;

    // Frequency response cache
    void invalidateFreqResponseCache() const noexcept;
    mutable std::vector<float> freqResponseCache_;
    mutable bool freqResponseCacheValid_ = false;
    mutable std::vector<float> freqResponseFrequencies_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiFilter)
};

} // namespace ana
