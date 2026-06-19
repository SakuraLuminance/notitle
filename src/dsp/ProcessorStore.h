#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace ana {

class EffectBase;
class UndoManager;

//==============================================================================
/**
    Static registry of effect type names → factory functions.

    Used for both the default rack preset and dynamic runtime effect
    creation.  Thread-safe for concurrent reads (SpinLock on the registry).
*/
class ProcessorStore
{
public:
    /** Factory function signature.
        @param um  Optional UndoManager for undo-capable effect construction.
    */
    using Factory = std::function<std::unique_ptr<EffectBase>(UndoManager*)>;

    //==============================================================================
    /** Register a factory for a named effect type.
        @param name     Unique effect type name (e.g. "Delay", "DriveModule")
        @param factory  Callable that returns a new EffectBase instance.
    */
    static void registerFactory(const juce::String& name, Factory factory);

    /** Create an effect by type name.
        @param name  Registered type name.
        @param um    Optional UndoManager.
        @return A new EffectBase instance, or nullptr if name is unknown.
    */
    static std::unique_ptr<EffectBase> create(const juce::String& name, UndoManager* um = nullptr);

    /** Returns a sorted list of all registered effect type names. */
    static juce::StringArray getAvailableTypes();

    /** Register all built-in effect types (consolidated modules + standalone). */
    static void registerAll();

    /** Remove all registered factories (useful for testing / plugin teardown). */
    static void clear();

private:
    //==============================================================================
    struct Entry
    {
        Factory     factory;
        juce::String type;
    };

    static juce::OwnedArray<Entry>& getRegistry();
    static juce::SpinLock&         getLock();
};

} // namespace ana
