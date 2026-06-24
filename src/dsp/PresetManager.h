#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include "STFTConfig.h"
#include "SpectralMorpher.h"
#include "MultiFilter.h"
#include "MultiPointEnvelope.h"
#include "LFOSystem.h"
#include "UnisonEngine.h"
#include "GranularSynthesizer.h"
#include "VoiceManager.h"
#include "FilterModulation.h"
#include "EffectsChain.h"
#include "ModulationEngine.h"

namespace ana {

class Randomizer; // forward declaration
class MidiLearn;  // forward declaration

//==============================================================================
/**
    Preset Management System for AnaPlug.

    Handles XML-based preset serialization using JUCE ValueTree. Presets capture
    the full synthesizer state including STFT configuration, filter parameters,
    envelope breakpoints, LFO settings, effect/voice parameters, and modulation
    routing.

    Factory presets ship with 6 categories: Bass, Lead, Pad, Pluck, FX, Vocal.

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
        "FX",
        "Vocal"
    };

    /** Number of built-in categories. */
    static constexpr int numCategories = 6;

    /** Factory preset names (one per category). */
    static constexpr const char* factoryPresetNames[] =
    {
        "Deep Sub Bass",
        "Resonant Lead",
        "Ambient Pad",
        "Percussive Pluck",
        "Warped FX",
        "Pop Lead"
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
    /** Returns the current list of favorited preset names. */
    const juce::Array<juce::String>& getFavorites() const noexcept { return favoriteNames_; }

    /** Sets the entire favorites list. */
    void setFavorites(const juce::Array<juce::String>& names) { favoriteNames_ = names; }

    /** Adds a preset name to favorites (no-op if already present). */
    void addFavorite(const juce::String& name);

    /** Removes a preset name from favorites (no-op if not present). */
    void removeFavorite(const juce::String& name);

    /** Returns true if the preset name is in the favorites list. */
    bool isFavorite(const juce::String& name) const;

    /** Persists the current favorites list to a file in the preset directory. */
    void saveFavoritesToFile();

    /** Loads the favorites list from the file in the preset directory. */
    void loadFavoritesFromFile();

    //==============================================================================
    /** Returns a list of names of saved effect-only presets. */
    juce::StringArray getEffectPresetNames() const;

    /** Saves the current effects chain state as a named effect preset. */
    bool saveEffectPreset(const juce::String& name);

    /** Loads an effect preset by name, restoring only the effects chain. */
    bool loadEffectPreset(const juce::String& name);

    /** Deletes a saved effect preset by name. */
    bool deleteEffectPreset(const juce::String& name);

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

    /** Sets the EffectsChain reference for serialisation. */
    void setEffectsChain(EffectsChain* chain) { effectsChain_ = chain; }

    /** Sets the modulation slots array reference (16 slots from the processor). */
    void setModulationSlotsRef(std::array<ModulationSlot, 16>* slots) { modSlotsRef_ = slots; }

    /** Sets the LFO pool array reference (4 LFOs from the processor). */
    void setLfoPoolRef(std::array<LFOSystem, 4>* pool) { lfoPoolRef_ = pool; }

    /** Sets the envelope pool array reference (3 ENVs from the processor). */
    void setEnvPoolRef(std::array<MultiPointEnvelope, 3>* pool) { envPoolRef_ = pool; }

    /** Sets the Volume ADSR reference. */
    void setVolumeAdsrRef(MultiPointEnvelope* adsr) { volumeAdsrRef_ = adsr; }

    /** Sets the Randomizer reference for seed serialisation. */
    void setRandomizerRef(Randomizer* r) { randomizerRef_ = r; }

    /** Sets the MidiLearn reference for per-preset MIDI mapping persistence. */
    void setMidiLearnRef(MidiLearn* ref) { midiLearnRef_ = ref; }

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
    static constexpr const char* presetVersion = "1.2";

    /** XML root element name. */
    static constexpr const char* xmlRootTag = "AnaPlugPreset";

    #ifdef ANA_INCLUDE_TEST_ACCESSORS
friend class ::PresetManagerTestAccess;
#endif

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

    /** Serialises effects chain state. */
    juce::ValueTree serialiseEffects() const;

    /** Deserialises effects chain state. */
    bool deserialiseEffects(const juce::ValueTree& tree);

    /** Serialises per-parameter modulation routing (16 modulation slots). */
    juce::ValueTree serialiseModulationRouting() const;

    /** Deserialises per-parameter modulation routing. */
    bool deserialiseModulationRouting(const juce::ValueTree& tree);

    /** Serialises per-LFO configurations (4 LFOs). */
    juce::ValueTree serialiseLFOConfig() const;

    /** Deserialises per-LFO configurations. */
    bool deserialiseLFOConfig(const juce::ValueTree& tree);

    /** Serialises per-ENV configurations (3 ENVs). */
    juce::ValueTree serialiseENVConfig() const;

    /** Deserialises per-ENV configurations. */
    bool deserialiseENVConfig(const juce::ValueTree& tree);

    /** Serialises Volume ADSR parameters. */
    juce::ValueTree serialiseVolumeADSR() const;

    /** Deserialises Volume ADSR parameters. */
    bool deserialiseVolumeADSR(const juce::ValueTree& tree);

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
    EffectsChain*            effectsChain_       = nullptr;
    std::array<ModulationSlot, 16>* modSlotsRef_  = nullptr;
    std::array<LFOSystem, 4>*        lfoPoolRef_  = nullptr;
    std::array<MultiPointEnvelope, 3>* envPoolRef_ = nullptr;
    MultiPointEnvelope*       volumeAdsrRef_     = nullptr;
    Randomizer*               randomizerRef_     = nullptr;
    MidiLearn*                midiLearnRef_      = nullptr;

    //==============================================================================
    // Internal state
    juce::String             currentPresetName;
    juce::StringArray        cachedCategories;
    juce::StringPairArray    cachedPresets;  // name -> category mapping

    //==============================================================================
    // Global user preferences
    juce::Array<juce::String> favoriteNames_;

    //==============================================================================
    // Morphing state (pre-loaded partial data for audio-thread-safe morph)
    PartialDataSIMD*         enginePartialsRef_ = nullptr;
    PartialDataSIMD          morphCacheA_;        // cached partials for preset A
    PartialDataSIMD          morphCacheB_;        // cached partials for preset B

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};

} // namespace ana
