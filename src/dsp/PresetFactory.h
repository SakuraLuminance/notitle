#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <utility>
#include "effects/VocalProcessor.h"

namespace ana {

class PresetFactory {
public:
    // Legacy single-preset factory methods (called by PresetManager::writeFactoryPreset)
    static juce::ValueTree createFactoryBass();
    static juce::ValueTree createFactoryLead();
    static juce::ValueTree createFactoryPad();
    static juce::ValueTree createFactoryPluck();
    static juce::ValueTree createFactoryFX();
    static juce::ValueTree createFactoryExperimental();

    // Category preset lists (20 each)
    static std::vector<std::pair<juce::String, juce::ValueTree>> createBassPresets();
    static std::vector<std::pair<juce::String, juce::ValueTree>> createLeadPresets();
    static std::vector<std::pair<juce::String, juce::ValueTree>> createPadPresets();
    static std::vector<std::pair<juce::String, juce::ValueTree>> createFXPresets();
    static std::vector<std::pair<juce::String, juce::ValueTree>> createExperimentalPresets();
    static std::vector<std::pair<juce::String, juce::ValueTree>> createVocalPresets();

    // Returns all 100+ factory presets as (name, ValueTree) pairs
    static std::vector<std::pair<juce::String, juce::ValueTree>> createAllPresets();

    //==============================================================================
    // Effect-specific factory presets (Delay, Reverb, Chorus, Distortion, etc.)
    //==============================================================================

    /** Returns the list of available factory preset names for a given effect type.
        Effect types: "Delay", "Reverb", "Chorus", "Distortion", "Saturation",
        "RingMod", "StereoWidener", "Bitcrusher", "Phaser", "Flanger",
        "Compressor", "Limiter", "AutoTune", "EQ"
    */
    static juce::StringArray getFactoryPresets(const juce::String& effectType);

    /** Returns the parameter ValueTree for a named factory preset.
        The tree's root tag matches the effect's getState() root name,
        and properties match the parameter names used in setState().
        Returns an invalid ValueTree if not found.
    */
    static juce::ValueTree getFactoryPreset(const juce::String& effectType, const juce::String& presetName);

    //==============================================================================
    // Consolidated module factory presets
    //==============================================================================

    /** Returns available factory preset names for a consolidated module type. */
    static juce::StringArray getModuleFactoryPresets(const juce::String& moduleType);

    /** Returns the parameter ValueTree for a named consolidated module factory preset. */
    static juce::ValueTree getModuleFactoryPreset(const juce::String& moduleType, const juce::String& presetName);

    //==============================================================================
    // Factory rack presets (complete effects chains)
    //==============================================================================

    /** Creates a "Clean Rack" preset: Delay → Reverb → EQ → Limiter. */
    static juce::ValueTree createCleanRackPreset();

    /** Creates a "Creative Rack" preset: Drive → Modulation → Space → Pitch. */
    static juce::ValueTree createCreativeRackPreset();
};

} // namespace ana
