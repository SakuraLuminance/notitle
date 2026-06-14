#pragma once

#include <juce_core/juce_core.h>
#include "STFTConfig.h"
#include "SpectralMorpher.h"
#include "MultiFilter.h"
#include "MultiPointEnvelope.h"
#include "LFOSystem.h"
#include "UnisonEngine.h"
#include "GranularSynthesizer.h"
#include "VoiceManager.h"
#include "FilterModulation.h"

namespace ana {

//==============================================================================
/**
    Preset Management System for AnaPlug.

    Handles XML-based preset serialization using JUCE ValueTree. Presets capture
    the full synthesizer state including STFT configuration, filter parameters,
    envelope breakpoints, LFO settings, effect/voice parameters, and modulation
    routing.

    Factory presets ship with 5 categories: Bass, Lead, Pad, Pluck, FX.

    File format: .anaplug (XML/ValueTree)

    Storage path: %APPDATA%/AnaPlug/Presets/
*/
class PresetManager
{
public:
    //==============================================================================
    /** Available preset categories. */
    static constexpr const char* categories[] =
    {
        "Bass",
        "Lead",
        "Pad",
        "Pluck",
        "FX"
    };

    /** Number of built-in categories. */
    static constexpr int numCategories = 5;

    /** Factory preset names (one per category). */
    static constexpr const char* factoryPresetNames[] =
    {
        "Deep Sub Bass",
        "Resonant Lead",
        "Ambient Pad",
        "Percussive Pluck",
        "Warped FX"
    };

    //==============================================================================
    /** Creates the PresetManager and ensures the preset directory exists. */
    PresetManager();

    /** Destructor. */
    ~PresetManager() = default;

    //==============================================================================
    /** Creates factory presets if none exist in the preset directory. */
    void initialiseFactoryPresets();

    //==============================================================================
    /**
        Saves the current synthesizer state as a named preset in a category.
        @param name      Preset name (e.g. "My Bass")
        @param category  Category from PresetManager::categories
        @return true if the preset was saved successfully
    */
    bool savePreset(const juce::String& name, const juce::String& category);

    /**
        Loads a preset by name. Searches all categories for a matching name.
        @param name  The preset name to load
        @return true if the preset was found and loaded
    */
    bool loadPreset(const juce::String& name);

    /**
        Loads a preset from an external .anaplug file.
        @param file  The file to load
        @return true if the file was loaded successfully
    */
    bool loadPresetFromFile(const juce::File& file);

    /**
        Saves the current state to an external .anaplug file.
        @param file  The destination file path
        @return true if the file was saved successfully
    */
    bool savePresetToFile(const juce::File& file);

    //==============================================================================
    /** Returns a list of preset names in the given category. */
    juce::StringArray getPresetList(const juce::String& category) const;

    /** Returns all available category names. */
    juce::StringArray getCategories() const;

    /**
        Deletes a preset by name. Searches all categories.
        @param name  The preset name to delete
        @return true if the preset was found and deleted
    */
    bool deletePreset(const juce::String& name);

    /**
        Searches presets by name (case-insensitive substring match).
        @param query  Search string
        @return Matching preset names with their categories
    */
    juce::StringPairArray searchPresets(const juce::String& query) const;

    /** Returns the total number of presets across all categories. */
    int getPresetCount() const;

    /** Returns the name of the currently loaded preset, or empty if none. */
    juce::String getCurrentPresetName() const;

    //==============================================================================
    /**
        Returns all preset names across all categories, sorted alphabetically.
    */
    juce::StringArray getPresetNames() const;

    //==============================================================================
    /**
        Loads two presets, captures their partial data from the engine, and
        linearly morphs between them using SpectralMorpher::morphLinear.

        This function is intended to be called from the message thread when
        the user selects two presets for morphing.  The result is written to
        @p output.  As a side effect, the engine state ends at preset B; the
        caller should restore the current preset if needed.

        @param presetA  Name of the first morph source (t=0)
        @param presetB  Name of the second morph source (t=1)
        @param t        Interpolation factor [0, 1]
        @param output   Receives the morphed partial data
        @return true if both presets were found and morph succeeded
    */
    bool morphPresets(const juce::String& presetA,
                      const juce::String& presetB,
                      float t,
                      PartialDataSIMD& output);

    //==============================================================================
    /**
        Sets a pointer to the engine's current partial data in SIMD format.
        Used internally by morphPresets to capture partial data after loading
        each preset.  The caller (PluginProcessor) is responsible for keeping
        this ref synchronised with the engine's analysed partial data.
    */
    void setEnginePartialsRef(PartialDataSIMD* ref);

    //==============================================================================
    /**
        Returns the cached partial data for the last preset loaded via
        morphPresets as presetA.  Audio-thread safe (read-only, no alloc).
    */
    const PartialDataSIMD& getMorphCacheA() const { return morphCacheA_; }

    /** Returns the cached partial data for morphPresets' presetB. */
    const PartialDataSIMD& getMorphCacheB() const { return morphCacheB_; }

    //==============================================================================
    /**
        Sets the current synthesizer state to be saved with the preset.
        These references are read when saving and written when loading.
    */
    void setStateReferences(STFTConfig* stftConfig,
                             MultiFilter* multiFilter,
                             MultiPointEnvelope* envelope,
                             LFOSystem* lfo,
                             GranularSynthesizer* granular,
                             UnisonEngine* unison,
                             VoiceManager* voiceManager,
                             FilterModulationSystem* filterMod);

    //==============================================================================
    /**
        Serializes the current synthesizer state to a ValueTree.
        @return ValueTree representation of the full preset
    */
    juce::ValueTree serialiseState() const;

    /**
        Deserialises a ValueTree into the current synthesizer state.
        @param tree  The ValueTree to load from
        @return true if deserialisation succeeded
    */
    bool deserialiseState(const juce::ValueTree& tree);

    //==============================================================================
    /** Returns the default preset directory path. */
    static juce::File getPresetDirectory();

    /** File extension for AnaPlug presets. */
    static constexpr const char* presetExtension = ".anaplug";

    /** Current preset format version. */
    static constexpr const char* presetVersion = "1.0";

    /** XML root element name. */
    static constexpr const char* xmlRootTag = "AnaPlugPreset";

private:
    //==============================================================================
    /** Serialises STFT configuration. */
    juce::ValueTree serialiseSTFTConfig() const;

    /** Deserialises STFT configuration. */
    bool deserialiseSTFTConfig(const juce::ValueTree& tree);

    /** Serialises multi-filter state (all slots + routing). */
    juce::ValueTree serialiseFilters() const;

    /** Deserialises multi-filter state. */
    bool deserialiseFilters(const juce::ValueTree& tree);

    /** Serialises envelope breakpoints and settings. */
    juce::ValueTree serialiseEnvelope() const;

    /** Deserialises envelope breakpoints and settings. */
    bool deserialiseEnvelope(const juce::ValueTree& tree);

    /** Serialises LFO settings. */
    juce::ValueTree serialiseLFO() const;

    /** Deserialises LFO settings. */
    bool deserialiseLFO(const juce::ValueTree& tree);

    /** Serialises granular synthesiser parameters. */
    juce::ValueTree serialiseGranular() const;

    /** Deserialises granular synthesiser parameters. */
    bool deserialiseGranular(const juce::ValueTree& tree);

    /** Serialises unison engine parameters. */
    juce::ValueTree serialiseUnison() const;

    /** Deserialises unison engine parameters. */
    bool deserialiseUnison(const juce::ValueTree& tree);

    /** Serialises voice manager / ADSR defaults. */
    juce::ValueTree serialiseVoiceManager() const;

    /** Deserialises voice manager / ADSR defaults. */
    bool deserialiseVoiceManager(const juce::ValueTree& tree);

    /** Serialises filter modulation routing. */
    juce::ValueTree serialiseModulation() const;

    /** Deserialises filter modulation routing. */
    bool deserialiseModulation(const juce::ValueTree& tree);

    //==============================================================================
    /** Writes factory presets to disk. */
    void writeFactoryPreset(const juce::String& name, const juce::String& category);



    /** Scans the preset directory and rebuilds the internal cache. */
    void rebuildCache();

    //==============================================================================
    /** Validates that a ValueTree is a well-formed AnaPlug preset. */
    static bool validatePresetTree(const juce::ValueTree& tree);

    //==============================================================================
    // State references (point to the engine's actual components)
    STFTConfig*              stftConfigRef       = nullptr;
    MultiFilter*             multiFilterRef      = nullptr;
    MultiPointEnvelope*      envelopeRef         = nullptr;
    LFOSystem*               lfoRef              = nullptr;
    GranularSynthesizer*     granularRef         = nullptr;
    UnisonEngine*            unisonRef           = nullptr;
    VoiceManager*            voiceManagerRef     = nullptr;
    FilterModulationSystem*  filterModRef        = nullptr;

    //==============================================================================
    // Internal state
    juce::String             currentPresetName;
    juce::StringArray        cachedCategories;
    juce::StringPairArray    cachedPresets;  // name -> category mapping

    //==============================================================================
    // Morphing state (pre-loaded partial data for audio-thread-safe morph)
    PartialDataSIMD*         enginePartialsRef_ = nullptr;
    PartialDataSIMD          morphCacheA_;        // cached partials for preset A
    PartialDataSIMD          morphCacheB_;        // cached partials for preset B

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};

} // namespace ana
