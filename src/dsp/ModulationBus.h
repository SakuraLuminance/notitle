#pragma once
#include <vector>
#include <atomic>
#include <string>
#include <juce_core/juce_core.h>

namespace ana {

//==============================================================================
/**
    ModulationBus — routes modulation sources (LFO, Envelope, Macro, etc.)
    to target parameters via atomic pointers.

    Each Route connects one source to one parameter. The source value is
    read from a caller-supplied `float*` pointer, scaled by `depth`, and
    written to the target `std::atomic<float>*` during `processBlock()`.

    The bus does NOT own the sources — the caller is responsible for
    keeping the source value pointers alive for the lifetime of each route.

    Usage:
        ModulationBus bus;
        LFOSystem lfo;

        std::atomic<float> cutoff{ 1000.0f };

        bus.addRoute(ModulationBus::Source::LFO, 0, "cutoff",
                     &cutoff, &lfo.currentValueInternal(), 0.5f);

        // Inside audio callback:
        lfo.processBlock(numSamples);          // advance LFO
        bus.processBlock(numSamples);          // apply routes
*/
class ModulationBus
{
public:
    enum class Source
    {
        LFO,        /**< Low-frequency oscillator */
        Envelope,   /**< Envelope generator */
        Macro,      /**< Macro control */
        Audio,      /**< Audio-rate signal */
        Partial,    /**< Partial-domain modulation */
        Sequencer   /**< Step sequencer CV output */
    };

    //==============================================================================
    /** A single modulation route connecting one source to one parameter. */
    struct Route
    {
        Source source = Source::LFO;     /**< Type of modulation source. */
        int sourceIndex = 0;             /**< Which LFO/envelope/macro (0-based). */
        std::string targetParamId;       /**< Human-readable target parameter ID. */
        std::atomic<float>* targetParam = nullptr;  /**< Target parameter (atomic for thread safety). */
        const float* sourceValue = nullptr;         /**< Pointer to the source's current value.
                                                                     WARNING: Must outlive the ModulationBus route.
                                                                     Call removeRoute() before destroying the source. */
        float depth = 1.0f;              /**< Modulation depth scalar. */
    };

    //==============================================================================
    ModulationBus() = default;
    ~ModulationBus() = default;

    //==============================================================================
    /** Adds a modulation route.
        @param src         Source type (LFO, Envelope, etc.)
        @param srcIndex    Index identifying which source (e.g. which LFO)
        @param targetId    Human-readable target parameter identifier
        @param target      Atomic pointer to the target parameter value
        @param sourceValue Pointer to a float holding the current source value.
                           Must remain valid for the lifetime of the route.
        @param depth       Modulation depth scalar (0.0 = no modulation, 1.0 = full)
    */
    void addRoute(Source src, int srcIndex, const std::string& targetId,
                  std::atomic<float>* target, const float* sourceValue, float depth);

    /** Removes the route at the given index.
        @param index  Index of the route to remove (must be < numRoutes)
    */
    void removeRoute(int index);

    /** Removes all routes. */
    void clear();

    //==============================================================================
    /** Processes all routes for the given number of samples.
        For each route, samples the source value, scales by depth, and
        writes the result into the target atomic parameter.
        When numSamples > 1, the route is processed once per block (control rate);
        per-sample processing must be done externally via per-sample source polling.
        @param numSamples  Number of audio samples in the current block
    */
    void processBlock(int numSamples);

    //==============================================================================
    /** Returns the number of active routes. */
    int getNumRoutes() const noexcept { return static_cast<int>(routes_.size()); }

    /** Returns a const reference to the route at the given index. */
    const Route& getRoute(int index) const;

private:
    //==============================================================================
    std::vector<Route> routes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationBus)
};

} // namespace ana
