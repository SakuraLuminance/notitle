#pragma once
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

namespace ana {

//==============================================================================
/**
    Modulation sources that can drive filter parameter changes.
*/
enum class ModulationSource
{
    LFO1,
    LFO2,
    Envelope1,
    Envelope2,
    Velocity,
    Modwheel,
    Aftertouch
};

//==============================================================================
/**
    Filter parameters that can be modulated.
*/
enum class ModulationTarget
{
    Cutoff,
    Resonance,
    Drive,
    Mix
};

//==============================================================================
/**
    Represents a single modulation connection between a source and a target.
    
    Each connection links one modulation source to one filter parameter target
    with an adjustable depth and bipolar mode. Bipolar modulation allows the
    source to both increase and decrease the target value, while unipolar
    modulation only increases it.
*/
struct FilterModulationConnection
{
    ModulationSource source;
    ModulationTarget target;
    int filterIndex = 0;    ///< Index of the filter in the chain (0-based)
    float depth = 0.0f;     ///< Modulation depth (0.0 to 1.0, representing 0-100%)
    bool bipolar = false;   ///< If true, source can modulate in both directions
    
    /** Unique identifier for this connection (auto-assigned). */
    int id = 0;
};

//==============================================================================
/**
    Filter Modulation System.
    
    Manages a set of modulation connections that link modulation sources
    (LFOs, envelopes, velocity, etc.) to filter parameters (cutoff, resonance,
    drive, mix). The system computes the net modulation for each target
    parameter given current source values.
    
    Usage:
        FilterModulationSystem modSys;
        modSys.connect(ModulationSource::LFO1, ModulationTarget::Cutoff, 0, 0.5f, true);
        modSys.setSourceValue(ModulationSource::LFO1, 0.7f);
        
        float modValue = modSys.getModulationValue(ModulationTarget::Cutoff, 0);
        // Apply modValue to the actual cutoff parameter
*/
class FilterModulationSystem
{
public:
    FilterModulationSystem();
    ~FilterModulationSystem() = default;

    //==============================================================================
    /** Creates a new modulation connection and returns its ID. */
    int connect(ModulationSource source, ModulationTarget target,
                int filterIndex, float depth, bool bipolar);

    /** Removes a modulation connection by ID. Returns true if found and removed. */
    bool disconnect(int connectionId);

    /** Removes all modulation connections for a given filter index. */
    void disconnectAll(int filterIndex);

    /** Removes all modulation connections. */
    void clearAll();

    //==============================================================================
    /** Sets the current value of a modulation source.
        @param source   The modulation source to set
        @param value    Source value: LFOs use -1..1, envelopes use 0..1,
                       velocity uses 0..1, modwheel uses 0..1, aftertouch uses 0..1
    */
    void setSourceValue(ModulationSource source, float value);

    /** Gets the current value of a modulation source. */
    float getSourceValue(ModulationSource source) const;

    //==============================================================================
    /**
        Computes the net modulation delta for a given target and filter index.
        @param target       The parameter to compute modulation for
        @param filterIndex  Which filter in the chain
        @return The modulation delta to apply (-1..1 range, scaled by depths)
    */
    float getModulationValue(ModulationTarget target, int filterIndex) const;

    /**
        Computes the modulated cutoff (Hz) given the base cutoff and modulation.
        @param baseCutoff   The unmodulated cutoff frequency in Hz
        @param filterIndex  Which filter in the chain
        @return The modulated cutoff frequency in Hz
    */
    float getModulatedCutoff(float baseCutoff, int filterIndex) const;

    /**
        Computes the modulated resonance value.
        @param baseResonance  The unmodulated resonance (0.0 to 1.0)
        @param filterIndex    Which filter in the chain
        @return The modulated resonance value
    */
    float getModulatedResonance(float baseResonance, int filterIndex) const;

    /**
        Computes the modulated drive value.
        @param baseDrive    The unmodulated drive (0.0 to 1.0)
        @param filterIndex  Which filter in the chain
        @return The modulated drive value
    */
    float getModulatedDrive(float baseDrive, int filterIndex) const;

    /**
        Computes the modulated mix (wet/dry) value.
        @param baseMix      The unmodulated mix (0.0 to 1.0)
        @param filterIndex  Which filter in the chain
        @return The modulated mix value
    */
    float getModulatedMix(float baseMix, int filterIndex) const;

    //==============================================================================
    /** Returns a list of all active modulation connections (read-only). */
    const std::vector<FilterModulationConnection>& getConnections() const;

    /** Returns the number of active connections. */
    int getNumConnections() const;

    /** Returns the number of filters currently configured in the chain. */
    int getNumFilters() const { return numFilters; }

    /** Sets the number of filters in the chain. Default is 1. */
    void setNumFilters(int numFiltersInChain);

private:
    std::vector<FilterModulationConnection> connections;
    float sourceValues[7] = {};  // One per ModulationSource enum value
    int nextConnectionId = 1;
    int numFilters = 1;

    /** Gets the current raw source value accounting for bipolar mode. */
    float getEffectiveSourceValue(const FilterModulationConnection& conn) const;

    /** Clamps a value to [0, 1]. */
    static float clamp01(float value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilterModulationSystem)
};

} // namespace ana
