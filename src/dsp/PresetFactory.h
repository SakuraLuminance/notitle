#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <utility>

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

    // Returns all 100+ factory presets as (name, ValueTree) pairs
    static std::vector<std::pair<juce::String, juce::ValueTree>> createAllPresets();
};

} // namespace ana
