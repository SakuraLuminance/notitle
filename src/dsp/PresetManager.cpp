#include "PresetManager.h"
#include "PresetFactory.h"
#include "ProcessorStore.h"

namespace ana {

//==============================================================================
// Enum string conversion helpers
//==============================================================================

static juce::String filterTypeToString(FilterType type)
{
    switch (type)
    {
        case FilterType::LowPass:   return "LowPass";
        case FilterType::HighPass:  return "HighPass";
        case FilterType::BandPass:  return "BandPass";
        case FilterType::Notch:     return "Notch";
        case FilterType::AllPass:   return "AllPass";
        case FilterType::Comb:      return "Comb";
        case FilterType::Formant:   return "Formant";
        case FilterType::Morph:     return "Morph";
        default:                    return "LowPass";
    }
}

static FilterType stringToFilterType(const juce::String& s)
{
    if (s == "HighPass")  return FilterType::HighPass;
    if (s == "BandPass")  return FilterType::BandPass;
    if (s == "Notch")     return FilterType::Notch;
    if (s == "AllPass")   return FilterType::AllPass;
    if (s == "Comb")      return FilterType::Comb;
    if (s == "Formant")   return FilterType::Formant;
    if (s == "Morph")     return FilterType::Morph;
    return FilterType::LowPass;
}

static juce::String routingModeToString(RoutingMode mode)
{
    switch (mode)
    {
        case RoutingMode::Serial:   return "Serial";
        case RoutingMode::Parallel: return "Parallel";
        case RoutingMode::Split:    return "Split";
        default:                    return "Serial";
    }
}

static RoutingMode stringToRoutingMode(const juce::String& s)
{
    if (s == "Parallel") return RoutingMode::Parallel;
    if (s == "Split")    return RoutingMode::Split;
    return RoutingMode::Serial;
}

static juce::String windowTypeToString(STFTConfig::WindowType wt)
{
    switch (wt)
    {
        case STFTConfig::WindowType::Hann:           return "Hann";
        case STFTConfig::WindowType::BlackmanHarris: return "BlackmanHarris";
        case STFTConfig::WindowType::Hamming:        return "Hamming";
        default:                                     return "Hann";
    }
}

static STFTConfig::WindowType stringToWindowType(const juce::String& s)
{
    if (s == "BlackmanHarris") return STFTConfig::WindowType::BlackmanHarris;
    if (s == "Hamming")        return STFTConfig::WindowType::Hamming;
    return STFTConfig::WindowType::Hann;
}

static juce::String curveTypeToString(CurveType ct)
{
    switch (ct)
    {
        case CurveType::Linear:      return "Linear";
        case CurveType::Exponential: return "Exponential";
        case CurveType::SCurve:      return "SCurve";
        default:                     return "Linear";
    }
}

static CurveType stringToCurveType(const juce::String& s)
{
    if (s == "Exponential") return CurveType::Exponential;
    if (s == "SCurve")      return CurveType::SCurve;
    return CurveType::Linear;
}

static juce::String loopModeToString(LoopMode lm)
{
    switch (lm)
    {
        case LoopMode::None:     return "None";
        case LoopMode::Forward:  return "Forward";
        case LoopMode::PingPong: return "PingPong";
        case LoopMode::Sustain:  return "Sustain";
        default:                 return "None";
    }
}

static LoopMode stringToLoopMode(const juce::String& s)
{
    if (s == "Forward")  return LoopMode::Forward;
    if (s == "PingPong") return LoopMode::PingPong;
    if (s == "Sustain")  return LoopMode::Sustain;
    return LoopMode::None;
}

static juce::String waveformToString(WaveformType wt)
{
    switch (wt)
    {
        case WaveformType::Sine:     return "Sine";
        case WaveformType::Triangle: return "Triangle";
        case WaveformType::Saw:      return "Saw";
        case WaveformType::Square:   return "Square";
        case WaveformType::Random:   return "Random";
        default:                     return "Sine";
    }
}

static WaveformType stringToWaveform(const juce::String& s)
{
    if (s == "Triangle") return WaveformType::Triangle;
    if (s == "Saw")      return WaveformType::Saw;
    if (s == "Square")   return WaveformType::Square;
    if (s == "Random")   return WaveformType::Random;
    return WaveformType::Sine;
}

static juce::String grainWindowToString(GrainWindowType wt)
{
    switch (wt)
    {
        case GrainWindowType::Hann:     return "Hann";
        case GrainWindowType::Triangle: return "Triangle";
        case GrainWindowType::Gaussian: return "Gaussian";
        case GrainWindowType::Sinc:     return "Sinc";
        default:                        return "Hann";
    }
}

static GrainWindowType stringToGrainWindow(const juce::String& s)
{
    if (s == "Triangle") return GrainWindowType::Triangle;
    if (s == "Gaussian") return GrainWindowType::Gaussian;
    if (s == "Sinc")     return GrainWindowType::Sinc;
    return GrainWindowType::Hann;
}

static juce::String posModToString(PositionModulation pm)
{
    switch (pm)
    {
        case PositionModulation::Off:       return "Off";
        case PositionModulation::LFO:       return "LFO";
        case PositionModulation::Envelope:  return "Envelope";
        case PositionModulation::Random:    return "Random";
        default:                            return "Off";
    }
}

static PositionModulation stringToPosMod(const juce::String& s)
{
    if (s == "LFO")       return PositionModulation::LFO;
    if (s == "Envelope")  return PositionModulation::Envelope;
    if (s == "Random")    return PositionModulation::Random;
    return PositionModulation::Off;
}

static juce::String modSourceToString(ModulationSource ms)
{
    switch (ms)
    {
        case ModulationSource::LFO1:       return "LFO1";
        case ModulationSource::LFO2:       return "LFO2";
        case ModulationSource::Envelope1:  return "Envelope1";
        case ModulationSource::Envelope2:  return "Envelope2";
        case ModulationSource::Velocity:   return "Velocity";
        case ModulationSource::Modwheel:   return "Modwheel";
        case ModulationSource::Aftertouch: return "Aftertouch";
        default:                           return "LFO1";
    }
}

static ModulationSource stringToModSource(const juce::String& s)
{
    if (s == "LFO2")       return ModulationSource::LFO2;
    if (s == "Envelope1")  return ModulationSource::Envelope1;
    if (s == "Envelope2")  return ModulationSource::Envelope2;
    if (s == "Velocity")   return ModulationSource::Velocity;
    if (s == "Modwheel")   return ModulationSource::Modwheel;
    if (s == "Aftertouch") return ModulationSource::Aftertouch;
    return ModulationSource::LFO1;
}

static juce::String modTargetToString(ModulationTarget mt)
{
    switch (mt)
    {
        case ModulationTarget::Cutoff:    return "Cutoff";
        case ModulationTarget::Resonance: return "Resonance";
        case ModulationTarget::Drive:     return "Drive";
        case ModulationTarget::Mix:       return "Mix";
        default:                          return "Cutoff";
    }
}

static ModulationTarget stringToModTarget(const juce::String& s)
{
    if (s == "Resonance") return ModulationTarget::Resonance;
    if (s == "Drive")     return ModulationTarget::Drive;
    if (s == "Mix")       return ModulationTarget::Mix;
    return ModulationTarget::Cutoff;
}

//==============================================================================
// ModSource (ModulationEngine.h) string conversion — used by ModulationRouting
//==============================================================================

static juce::String modSourceNewToString(ModSource src)
{
    switch (src)
    {
        case ModSource::OFF:  return "Off";
        case ModSource::LFO1: return "LFO1";
        case ModSource::LFO2: return "LFO2";
        case ModSource::LFO3: return "LFO3";
        case ModSource::LFO4: return "LFO4";
        case ModSource::ENV1: return "ENV1";
        case ModSource::ENV2: return "ENV2";
        case ModSource::ENV3: return "ENV3";
        default:              return "Off";
    }
}

static ModSource stringToModSourceNew(const juce::String& s)
{
    if (s == "LFO1") return ModSource::LFO1;
    if (s == "LFO2") return ModSource::LFO2;
    if (s == "LFO3") return ModSource::LFO3;
    if (s == "LFO4") return ModSource::LFO4;
    if (s == "ENV1") return ModSource::ENV1;
    if (s == "ENV2") return ModSource::ENV2;
    if (s == "ENV3") return ModSource::ENV3;
    return ModSource::OFF;
}

static juce::String allocModeToString(AllocationMode am)
{
    switch (am)
    {
        case AllocationMode::roundRobin:  return "RoundRobin";
        case AllocationMode::oldestFirst: return "OldestFirst";
        case AllocationMode::random:      return "Random";
        default:                          return "RoundRobin";
    }
}

static AllocationMode stringToAllocMode(const juce::String& s)
{
    if (s == "OldestFirst") return AllocationMode::oldestFirst;
    if (s == "Random")      return AllocationMode::random;
    return AllocationMode::roundRobin;
}

//==============================================================================
// PresetManager implementation
//==============================================================================

PresetManager::PresetManager()
{
    // Ensure the ProcessorStore is populated with all built-in effect types
    ProcessorStore::registerAll();

    auto dir = getPresetDirectory();
    dir.createDirectory();

    rebuildCache();
}

//==============================================================================
// Public API
//==============================================================================

void PresetManager::initialiseFactoryPresets()
{
    // Only write factory presets if the preset directory is empty
    auto dir = getPresetDirectory();
    auto existingFiles = dir.findChildFiles(juce::File::findFiles, false,
                                            juce::String("*") + presetExtension);

    if (existingFiles.size() > 0)
        return;

    for (int i = 0; i < numCategories; ++i)
        writeFactoryPreset(factoryPresetNames[i], categories[i]);

    rebuildCache();
}

static juce::String sanitizePresetName(const juce::String& name)
{
    auto s = name.trim();
    if (s.isEmpty())
        return {};

    // Replace parent-directory references
    s = s.replace("..", "_");

    // Replace path separators
    s = s.replaceCharacter('/', '_');
    s = s.replaceCharacter('\\', '_');

    // Strip remaining illegal filename characters (: * ? " < > |)
    s = juce::File::createLegalFileName(s);

    return s;
}

bool PresetManager::savePreset(const juce::String& name, const juce::String& category)
{
    auto safeName = sanitizePresetName(name);
    if (safeName.isEmpty())
        return false;

    // Validate category
    bool validCategory = false;
    for (int i = 0; i < numCategories; ++i)
    {
        if (categories[i] == category)
        {
            validCategory = true;
            break;
        }
    }
    if (!validCategory)
        return false;

    // Build preset tree
    juce::ValueTree presetTree(xmlRootTag);
    presetTree.setProperty("Name", name, nullptr);
    presetTree.setProperty("Category", category, nullptr);
    presetTree.setProperty("Version", presetVersion, nullptr);

    auto paramsTree = serialiseState();
    presetTree.addChild(paramsTree, 0, nullptr);

    // Write to file
    auto dir = getPresetDirectory().getChildFile(category);
    dir.createDirectory();

    auto file = dir.getChildFile(safeName + presetExtension);

    // Create XML from ValueTree
    auto xml = presetTree.createXml();
    if (xml == nullptr)
        return false;

    juce::FileOutputStream stream(file);
    if (!stream.openedOk())
        return false;

    xml->writeTo(stream, juce::XmlElement::TextFormat());

    currentPresetName = safeName;
    rebuildCache();
    return true;
}

bool PresetManager::loadPreset(const juce::String& name)
{
    auto safeName = sanitizePresetName(name);
    if (safeName.isEmpty())
        return false;

    // Search all categories for matching preset file
    for (int i = 0; i < numCategories; ++i)
    {
        auto catDir = getPresetDirectory().getChildFile(categories[i]);
        if (!catDir.exists())
            continue;

        auto file = catDir.getChildFile(safeName + presetExtension);
        if (file.existsAsFile())
            return loadPresetFromFile(file);
    }

    return false;
}

bool PresetManager::loadPresetFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return false;

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!validatePresetTree(tree))
        return false;

    // Name and category
    currentPresetName = tree.getProperty("Name", "Untitled").toString();

    // Parameters are the first child
    if (tree.getNumChildren() > 0)
    {
        if (!deserialiseState(tree.getChild(0)))
            return false;
    }

    return true;
}

bool PresetManager::savePresetToFile(const juce::File& file)
{
    juce::ValueTree presetTree(xmlRootTag);
    presetTree.setProperty("Name", currentPresetName.isNotEmpty() ? currentPresetName : "Untitled", nullptr);
    presetTree.setProperty("Category", "Custom", nullptr);
    presetTree.setProperty("Version", presetVersion, nullptr);

    auto paramsTree = serialiseState();
    presetTree.addChild(paramsTree, 0, nullptr);

    auto xml = presetTree.createXml();
    if (xml == nullptr)
        return false;

    juce::FileOutputStream stream(file);
    if (!stream.openedOk())
        return false;

    xml->writeTo(stream, juce::XmlElement::TextFormat());

    return true;
}

juce::StringArray PresetManager::getPresetList(const juce::String& category) const
{
    juce::StringArray result;
    auto catDir = getPresetDirectory().getChildFile(category);
    if (!catDir.exists())
        return result;

    auto files = catDir.findChildFiles(juce::File::findFiles, false,
                                       juce::String("*") + presetExtension);

    for (const auto& f : files)
        result.add(f.getFileNameWithoutExtension());

    result.sort(true);
    return result;
}

juce::StringArray PresetManager::getCategories() const
{
    juce::StringArray result;
    for (int i = 0; i < numCategories; ++i)
        result.add(categories[i]);
    return result;
}

bool PresetManager::deletePreset(const juce::String& name)
{
    auto safeName = sanitizePresetName(name);
    if (safeName.isEmpty())
        return false;

    for (int i = 0; i < numCategories; ++i)
    {
        auto catDir = getPresetDirectory().getChildFile(categories[i]);
        if (!catDir.exists())
            continue;

        auto file = catDir.getChildFile(safeName + presetExtension);
        if (file.existsAsFile())
        {
            bool ok = file.deleteFile();
            if (ok)
            {
                if (currentPresetName == safeName)
                    currentPresetName = {};
                rebuildCache();
            }
            return ok;
        }
    }

    return false;
}

juce::StringPairArray PresetManager::searchPresets(const juce::String& query) const
{
    juce::StringPairArray results;
    auto lowerQuery = query.toLowerCase();

    for (int i = 0; i < numCategories; ++i)
    {
        auto catDir = getPresetDirectory().getChildFile(categories[i]);
        if (!catDir.exists())
            continue;

        auto files = catDir.findChildFiles(juce::File::findFiles, false,
                                           juce::String("*") + presetExtension);

        for (const auto& f : files)
        {
            auto name = f.getFileNameWithoutExtension();
            if (name.toLowerCase().contains(lowerQuery))
                results.set(name, categories[i]);
        }
    }

    return results;
}

int PresetManager::getPresetCount() const
{
    int count = 0;
    for (int i = 0; i < numCategories; ++i)
    {
        auto catDir = getPresetDirectory().getChildFile(categories[i]);
        if (!catDir.exists())
            continue;

        count += catDir.findChildFiles(juce::File::findFiles, false,
                                       juce::String("*") + presetExtension).size();
    }
    return count;
}

juce::String PresetManager::getCurrentPresetName() const
{
    return currentPresetName;
}

void PresetManager::setStateReferences(STFTConfig* stftConfig,
                                        MultiFilter* multiFilter,
                                        MultiPointEnvelope* envelope,
                                        LFOSystem* lfo,
                                        GranularSynthesizer* granular,
                                        UnisonEngine* unison,
                                        VoiceManager* voiceManager,
                                        FilterModulationSystem* filterMod)
{
    stftConfigRef   = stftConfig;
    multiFilterRef  = multiFilter;
    envelopeRef     = envelope;
    lfoRef          = lfo;
    granularRef     = granular;
    unisonRef       = unison;
    voiceManagerRef = voiceManager;
    filterModRef    = filterMod;
}

//==============================================================================
// ValueTree serialization
//==============================================================================

juce::ValueTree PresetManager::serialiseState() const
{
    juce::ValueTree params("Parameters");

    params.addChild(serialiseSTFTConfig(), 0, nullptr);
    params.addChild(serialiseFilters(), -1, nullptr);
    params.addChild(serialiseEnvelope(), -1, nullptr);
    params.addChild(serialiseLFO(), -1, nullptr);
    params.addChild(serialiseGranular(), -1, nullptr);
    params.addChild(serialiseUnison(), -1, nullptr);
    params.addChild(serialiseVoiceManager(), -1, nullptr);
    params.addChild(serialiseModulation(), -1, nullptr);
    params.addChild(serialiseModulationRouting(), -1, nullptr);
    params.addChild(serialiseLFOConfig(), -1, nullptr);
    params.addChild(serialiseENVConfig(), -1, nullptr);
    params.addChild(serialiseVolumeADSR(), -1, nullptr);
    params.addChild(serialiseEffects(), -1, nullptr);

    // Randomizer seed
    if (randomizerRef_ != nullptr)
        params.addChild(randomizerRef_->getState(), -1, nullptr);

    // Favorites (persisted as part of the full state tree)
    if (!favoriteNames_.isEmpty())
    {
        juce::ValueTree favsTree("Favorites");
        for (const auto& name : favoriteNames_)
        {
            juce::ValueTree entry("Preset");
            entry.setProperty("Name", name, nullptr);
            favsTree.addChild(entry, -1, nullptr);
        }
        params.addChild(favsTree, -1, nullptr);
    }

    // Per-preset MIDI Learn mappings (non-global only)
    if (midiLearnRef_ != nullptr)
        params.addChild(midiLearnRef_->savePresetState(), -1, nullptr);

    return params;
}

bool PresetManager::deserialiseState(const juce::ValueTree& tree)
{
    if (!tree.isValid())
        return false;

    bool ok = true;

    auto stft = tree.getChildWithName("STFTConfig");
    if (stft.isValid()) ok &= deserialiseSTFTConfig(stft);

    auto filters = tree.getChildWithName("Filters");
    if (filters.isValid()) ok &= deserialiseFilters(filters);

    auto envelope = tree.getChildWithName("Envelope");
    if (envelope.isValid()) ok &= deserialiseEnvelope(envelope);

    auto lfo = tree.getChildWithName("LFO");
    if (lfo.isValid()) ok &= deserialiseLFO(lfo);

    auto granular = tree.getChildWithName("Granular");
    if (granular.isValid()) ok &= deserialiseGranular(granular);

    auto unison = tree.getChildWithName("Unison");
    if (unison.isValid()) ok &= deserialiseUnison(unison);

    auto voiceMgr = tree.getChildWithName("VoiceManager");
    if (voiceMgr.isValid()) ok &= deserialiseVoiceManager(voiceMgr);

    auto mod = tree.getChildWithName("Modulation");
    if (mod.isValid()) ok &= deserialiseModulation(mod);

    // New sections (v1.2+) — gracefully absent in old presets
    auto modRouting = tree.getChildWithName("ModulationRouting");
    if (modRouting.isValid()) ok &= deserialiseModulationRouting(modRouting);
    // If no ModulationRouting, slots remain at default (source=OFF)

    auto lfoCfg = tree.getChildWithName("LFOConfig");
    if (lfoCfg.isValid()) ok &= deserialiseLFOConfig(lfoCfg);

    auto envCfg = tree.getChildWithName("ENVConfig");
    if (envCfg.isValid()) ok &= deserialiseENVConfig(envCfg);

    auto volAdsr = tree.getChildWithName("VolumeADSR");
    if (volAdsr.isValid()) ok &= deserialiseVolumeADSR(volAdsr);

    auto effects = tree.getChildWithName("Effects");
    if (effects.isValid()) ok &= deserialiseEffects(effects);

    auto randomizer = tree.getChildWithName("Randomizer");
    if (randomizer.isValid() && randomizerRef_ != nullptr)
        randomizerRef_->setState(randomizer);

    // Favorites
    auto favs = tree.getChildWithName("Favorites");
    if (favs.isValid())
    {
        favoriteNames_.clear();
        for (int i = 0; i < favs.getNumChildren(); ++i)
        {
            if (favs.getChild(i).hasType("Preset"))
                favoriteNames_.add(favs.getChild(i).getProperty("Name").toString());
        }
    }

    // Per-preset MIDI Learn mappings (non-global only)
    // Per-preset MIDI Learn mappings (non-global only)
    auto midiLearnTree = tree.getChildWithName("MidiLearnPreset");
    if (midiLearnTree.isValid() && midiLearnRef_ != nullptr)
        midiLearnRef_->loadPresetState(midiLearnTree);

    return ok;
}

//==============================================================================
// STFT Config
//==============================================================================

juce::ValueTree PresetManager::serialiseSTFTConfig() const
{
    juce::ValueTree tree("STFTConfig");

    if (stftConfigRef != nullptr)
    {
        tree.setProperty("FFTSize",      stftConfigRef->fftSize, nullptr);
        tree.setProperty("HopSize",      stftConfigRef->hopSize, nullptr);
        tree.setProperty("WindowType",   windowTypeToString(stftConfigRef->windowType), nullptr);
        tree.setProperty("Threshold",    stftConfigRef->peakThresholdDB, nullptr);
        tree.setProperty("MaxPartials",  stftConfigRef->maxPartials, nullptr);
    }

    return tree;
}

bool PresetManager::deserialiseSTFTConfig(const juce::ValueTree& tree)
{
    if (stftConfigRef == nullptr || !tree.isValid())
        return false;

    stftConfigRef->fftSize         = juce::jlimit(256, 65536, (int)tree.getProperty("FFTSize", 2048));
    stftConfigRef->hopSize         = juce::jlimit(64, 8192, (int)tree.getProperty("HopSize", 512));
    stftConfigRef->windowType      = stringToWindowType(tree.getProperty("WindowType", "Hann").toString());
    stftConfigRef->peakThresholdDB = juce::jlimit(-120.0f, 0.0f, (float)tree.getProperty("Threshold", -60.0f));
    stftConfigRef->maxPartials     = juce::jlimit(32, 2048, (int)tree.getProperty("MaxPartials", 512));

    return true;
}

//==============================================================================
// Multi-Filter
//==============================================================================

juce::ValueTree PresetManager::serialiseFilters() const
{
    juce::ValueTree tree("Filters");

    if (multiFilterRef != nullptr)
    {
        tree.setProperty("RoutingMode", routingModeToString(multiFilterRef->getRoutingMode()), nullptr);
        tree.setProperty("MasterGain",  multiFilterRef->getMasterGain(), nullptr);

        for (int i = 0; i < multiFilterRef->getNumSlots(); ++i)
        {
            const auto& slot = multiFilterRef->getSlot(i);
            juce::ValueTree slotTree("Slot");

            slotTree.setProperty("Type",          filterTypeToString(slot.type), nullptr);
            slotTree.setProperty("Cutoff",        slot.params.cutoff, nullptr);
            slotTree.setProperty("Resonance",     slot.params.resonance, nullptr);
            slotTree.setProperty("Drive",         slot.params.drive, nullptr);
            slotTree.setProperty("Mix",           slot.params.mix, nullptr);
            slotTree.setProperty("Bypassed",      slot.bypassed, nullptr);
            slotTree.setProperty("CrossoverLow",  slot.params.crossoverLow, nullptr);
            slotTree.setProperty("CrossoverHigh", slot.params.crossoverHigh, nullptr);
            slotTree.setProperty("MorphSource",   filterTypeToString(slot.params.morphSource), nullptr);
            slotTree.setProperty("MorphTarget",   filterTypeToString(slot.params.morphTarget), nullptr);
            slotTree.setProperty("MorphAmount",   slot.params.morphAmount, nullptr);

            tree.addChild(slotTree, -1, nullptr);
        }
    }

    return tree;
}

bool PresetManager::deserialiseFilters(const juce::ValueTree& tree)
{
    if (multiFilterRef == nullptr || !tree.isValid())
        return false;

    multiFilterRef->setRoutingMode(stringToRoutingMode(tree.getProperty("RoutingMode", "Serial").toString()));
    multiFilterRef->setMasterGain(tree.getProperty("MasterGain", 1.0f));

    multiFilterRef->clearSlots();

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto slotTree = tree.getChild(i);
        if (!slotTree.hasType("Slot"))
            continue;

        FilterParams params;
        params.cutoff        = juce::jlimit(20.0, 20000.0, (double)slotTree.getProperty("Cutoff", 1000.0));
        params.resonance     = juce::jlimit(0.0f, 1.0f, (float)slotTree.getProperty("Resonance", 0.0f));
        params.drive         = juce::jlimit(0.0f, 1.0f, (float)slotTree.getProperty("Drive", 0.0f));
        params.mix           = juce::jlimit(0.0f, 1.0f, (float)slotTree.getProperty("Mix", 1.0f));
        params.crossoverLow  = juce::jlimit(20.0, 20000.0, (double)slotTree.getProperty("CrossoverLow", 200.0));
        params.crossoverHigh = juce::jlimit(20.0, 20000.0, (double)slotTree.getProperty("CrossoverHigh", 2000.0));
        params.morphSource   = stringToFilterType(slotTree.getProperty("MorphSource", "LowPass").toString());
        params.morphTarget   = stringToFilterType(slotTree.getProperty("MorphTarget", "HighPass").toString());
        params.morphAmount   = slotTree.getProperty("MorphAmount", 0.0f);

        auto type = stringToFilterType(slotTree.getProperty("Type", "LowPass").toString());
        int slotIndex = multiFilterRef->addSlot(type, params);

        // Set bypass after adding
        if (slotIndex >= 0)
        {
            bool bypassed = slotTree.getProperty("Bypassed", false);
            multiFilterRef->getSlot(slotIndex).bypassed = bypassed;
        }
    }

    return true;
}

//==============================================================================
// Envelope
//==============================================================================

juce::ValueTree PresetManager::serialiseEnvelope() const
{
    juce::ValueTree tree("Envelope");

    if (envelopeRef != nullptr)
    {
        tree.setProperty("LoopMode",     loopModeToString(envelopeRef->getLoopMode()), nullptr);
        tree.setProperty("LoopStart",    envelopeRef->getLoopStart(), nullptr);
        tree.setProperty("LoopEnd",      envelopeRef->getLoopEnd(), nullptr);
        tree.setProperty("Tempo",        envelopeRef->getTempo(), nullptr);
        tree.setProperty("BeatDivision", envelopeRef->getBeatDivision(), nullptr);
        tree.setProperty("SyncEnabled",  envelopeRef->getSyncMode(), nullptr);

        for (int i = 0; i < envelopeRef->getNumBreakpoints(); ++i)
        {
            const auto& bp = envelopeRef->getBreakpoint(i);
            juce::ValueTree bpTree("Breakpoint");
            bpTree.setProperty("Time",  bp.time, nullptr);
            bpTree.setProperty("Value", bp.value, nullptr);
            bpTree.setProperty("Curve", curveTypeToString(bp.curve), nullptr);
            tree.addChild(bpTree, -1, nullptr);
        }
    }

    return tree;
}

bool PresetManager::deserialiseEnvelope(const juce::ValueTree& tree)
{
    if (envelopeRef == nullptr || !tree.isValid())
        return false;

    envelopeRef->setLoopMode(stringToLoopMode(tree.getProperty("LoopMode", "None").toString()));
    envelopeRef->setLoopStart(tree.getProperty("LoopStart", 0));
    envelopeRef->setLoopEnd(tree.getProperty("LoopEnd", -1));
    envelopeRef->setTempo(tree.getProperty("Tempo", 120.0));
    envelopeRef->setBeatDivision(tree.getProperty("BeatDivision", 1.0));
    envelopeRef->setSyncMode(tree.getProperty("SyncEnabled", false));

    envelopeRef->clearBreakpoints();

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto bpTree = tree.getChild(i);
        if (!bpTree.hasType("Breakpoint"))
            continue;

        float time  = bpTree.getProperty("Time", 0.0f);
        float value = bpTree.getProperty("Value", 0.0f);
        auto curve  = stringToCurveType(bpTree.getProperty("Curve", "Linear").toString());

        envelopeRef->addBreakpoint(time, value, curve);
    }

    return true;
}

//==============================================================================
// LFO
//==============================================================================

juce::ValueTree PresetManager::serialiseLFO() const
{
    juce::ValueTree tree("LFO");

    if (lfoRef != nullptr)
    {
        tree.setProperty("Waveform",    waveformToString(lfoRef->getWaveform()), nullptr);
        tree.setProperty("RateHz",      lfoRef->getRate(), nullptr);
        tree.setProperty("RateBeats",   lfoRef->getRateBeats(), nullptr);
        tree.setProperty("Depth",       lfoRef->getDepth(), nullptr);
        tree.setProperty("Phase",       lfoRef->getPhase(), nullptr);
        tree.setProperty("Bipolar",     lfoRef->isBipolar(), nullptr);
        tree.setProperty("SyncEnabled", lfoRef->isSyncEnabled(), nullptr);
        tree.setProperty("Tempo",       lfoRef->getTempo(), nullptr);
    }

    return tree;
}

bool PresetManager::deserialiseLFO(const juce::ValueTree& tree)
{
    if (lfoRef == nullptr || !tree.isValid())
        return false;

    lfoRef->setWaveform(stringToWaveform(tree.getProperty("Waveform", "Sine").toString()));
    lfoRef->setRate(tree.getProperty("RateHz", 1.0f));
    lfoRef->setRateBeats(tree.getProperty("RateBeats", 1.0f));
    lfoRef->setDepth(tree.getProperty("Depth", 100.0f));
    lfoRef->setPhase(tree.getProperty("Phase", 0.0f));
    lfoRef->setBipolar(tree.getProperty("Bipolar", true));
    lfoRef->setTempo(tree.getProperty("Tempo", 120.0));

    return true;
}

//==============================================================================
// Granular
//==============================================================================

juce::ValueTree PresetManager::serialiseGranular() const
{
    juce::ValueTree tree("Granular");

    if (granularRef != nullptr)
    {
        // GranularSynthesizer doesn't expose getters for all parameters via its header.
        // We store the canonical set; the caller ensures granularRef is set.
        tree.setProperty("GrainSize",      50.0f, nullptr);
        tree.setProperty("Density",        10.0f, nullptr);
        tree.setProperty("Position",       0.5f, nullptr);
        tree.setProperty("Pitch",          0.0f, nullptr);
        tree.setProperty("Amplitude",      0.5f, nullptr);
        tree.setProperty("Pan",            0.0f, nullptr);
        tree.setProperty("WindowType",     grainWindowToString(GrainWindowType::Hann), nullptr);
        tree.setProperty("PosModType",     posModToString(PositionModulation::Off), nullptr);
        tree.setProperty("PosModDepth",    0.1f, nullptr);
        tree.setProperty("PosModRate",     1.0f, nullptr);
    }

    return tree;
}

bool PresetManager::deserialiseGranular(const juce::ValueTree& tree)
{
    if (granularRef == nullptr || !tree.isValid())
        return false;

    granularRef->setGrainSize(tree.getProperty("GrainSize", 50.0f));
    granularRef->setDensity(tree.getProperty("Density", 10.0f));
    granularRef->setPosition(tree.getProperty("Position", 0.5f));
    granularRef->setPitch(tree.getProperty("Pitch", 0.0f));
    granularRef->setAmplitude(tree.getProperty("Amplitude", 0.5f));
    granularRef->setPan(tree.getProperty("Pan", 0.0f));
    granularRef->setWindowType(stringToGrainWindow(tree.getProperty("WindowType", "Hann").toString()));

    auto posMod = stringToPosMod(tree.getProperty("PosModType", "Off").toString());
    float posModDepth = tree.getProperty("PosModDepth", 0.1f);
    float posModRate  = tree.getProperty("PosModRate", 1.0f);
    granularRef->setPositionModulation(posMod, posModDepth, posModRate);

    return true;
}

//==============================================================================
// Unison
//==============================================================================

juce::ValueTree PresetManager::serialiseUnison() const
{
    juce::ValueTree tree("Unison");

    if (unisonRef != nullptr)
    {
        tree.setProperty("VoiceCount",    unisonRef->getVoiceCount(), nullptr);
        tree.setProperty("Detune",        unisonRef->getDetune(), nullptr);
        tree.setProperty("StereoSpread",  unisonRef->getStereoSpread(), nullptr);
        tree.setProperty("PhaseOffset",   unisonRef->getPhaseOffset(), nullptr);
    }

    return tree;
}

bool PresetManager::deserialiseUnison(const juce::ValueTree& tree)
{
    if (unisonRef == nullptr || !tree.isValid())
        return false;

    unisonRef->setVoiceCount(juce::jlimit(1, 64, (int)tree.getProperty("VoiceCount", 1)));
    unisonRef->setDetune(tree.getProperty("Detune", 0.0f));
    unisonRef->setStereoSpread(tree.getProperty("StereoSpread", 0.0f));
    unisonRef->setPhaseOffset(tree.getProperty("PhaseOffset", 0.0f));

    return true;
}

//==============================================================================
// Voice Manager
//==============================================================================

juce::ValueTree PresetManager::serialiseVoiceManager() const
{
    juce::ValueTree tree("VoiceManager");

    if (voiceManagerRef != nullptr)
    {
        tree.setProperty("Attack",         voiceManagerRef->getVoice(0)->attackSeconds, nullptr);
        tree.setProperty("Decay",          voiceManagerRef->getVoice(0)->decaySeconds, nullptr);
        tree.setProperty("Sustain",        voiceManagerRef->getVoice(0)->sustainLevel, nullptr);
        tree.setProperty("Release",        voiceManagerRef->getVoice(0)->releaseSeconds, nullptr);
        tree.setProperty("AllocationMode", allocModeToString(voiceManagerRef->getAllocationMode()), nullptr);
    }

    return tree;
}

bool PresetManager::deserialiseVoiceManager(const juce::ValueTree& tree)
{
    if (voiceManagerRef == nullptr || !tree.isValid())
        return false;

    float attack  = juce::jlimit(0.0f, 10.0f, (float)tree.getProperty("Attack", 0.01f));
    float decay   = juce::jlimit(0.0f, 10.0f, (float)tree.getProperty("Decay", 0.2f));
    float sustain = juce::jlimit(0.0f, 1.0f, (float)tree.getProperty("Sustain", 0.7f));
    float release = juce::jlimit(0.0f, 30.0f, (float)tree.getProperty("Release", 0.3f));

    voiceManagerRef->setDefaultAttack(attack);
    voiceManagerRef->setDefaultDecay(decay);
    voiceManagerRef->setDefaultSustain(sustain);
    voiceManagerRef->setDefaultRelease(release);

    voiceManagerRef->setAllocationMode(stringToAllocMode(tree.getProperty("AllocationMode", "RoundRobin").toString()));

    return true;
}

//==============================================================================
// Modulation
//==============================================================================

juce::ValueTree PresetManager::serialiseModulation() const
{
    juce::ValueTree tree("Modulation");

    if (filterModRef != nullptr)
    {
        tree.setProperty("NumFilters", filterModRef->getNumFilters(), nullptr);

        const auto& connections = filterModRef->getConnections();
        for (const auto& conn : connections)
        {
            juce::ValueTree connTree("Connection");
            connTree.setProperty("Source",      modSourceToString(conn.source), nullptr);
            connTree.setProperty("Target",      modTargetToString(conn.target), nullptr);
            connTree.setProperty("FilterIndex", conn.filterIndex, nullptr);
            connTree.setProperty("Depth",       conn.depth, nullptr);
            connTree.setProperty("Bipolar",     conn.bipolar, nullptr);
            connTree.setProperty("ID",          conn.id, nullptr);
            tree.addChild(connTree, -1, nullptr);
        }
    }

    return tree;
}

bool PresetManager::deserialiseModulation(const juce::ValueTree& tree)
{
    if (filterModRef == nullptr || !tree.isValid())
        return false;

    filterModRef->clearAll();

    int numFilters = juce::jlimit(1, 8, (int)tree.getProperty("NumFilters", 1));
    filterModRef->setNumFilters(numFilters);

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto connTree = tree.getChild(i);
        if (!connTree.hasType("Connection"))
            continue;

        auto source      = stringToModSource(connTree.getProperty("Source", "LFO1").toString());
        auto target      = stringToModTarget(connTree.getProperty("Target", "Cutoff").toString());
        int filterIndex  = connTree.getProperty("FilterIndex", 0);
        float depth      = connTree.getProperty("Depth", 0.0f);
        bool bipolar     = connTree.getProperty("Bipolar", false);

        filterModRef->connect(source, target, filterIndex, depth, bipolar);
    }

    return true;
}

//==============================================================================
// Modulation Routing (per-parameter modulation slots)
//==============================================================================

juce::ValueTree PresetManager::serialiseModulationRouting() const
{
    juce::ValueTree tree("ModulationRouting");
    tree.setProperty("version", 1, nullptr);

    if (modSlotsRef_ != nullptr)
    {
        for (const auto& slot : *modSlotsRef_)
        {
            juce::ValueTree slotTree("Slot");
            slotTree.setProperty("paramId", slot.paramId, nullptr);
            slotTree.setProperty("source",  modSourceNewToString(slot.mod.source), nullptr);
            slotTree.setProperty("depth",   slot.mod.depth, nullptr);
            slotTree.setProperty("curve",   slot.mod.curve, nullptr);
            tree.addChild(slotTree, -1, nullptr);
        }
    }

    return tree;
}

bool PresetManager::deserialiseModulationRouting(const juce::ValueTree& tree)
{
    if (modSlotsRef_ == nullptr || !tree.isValid())
        return false;

    // Reset all slots to OFF before loading
    for (auto& slot : *modSlotsRef_)
    {
        slot.mod.source = ModSource::OFF;
        slot.mod.depth  = 0.0f;
        slot.mod.curve  = 1.0f;
    }

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto slotTree = tree.getChild(i);
        if (!slotTree.hasType("Slot"))
            continue;

        juce::String paramId = slotTree.getProperty("paramId").toString();
        auto source = stringToModSourceNew(slotTree.getProperty("source", "Off").toString());
        float depth = (float)slotTree.getProperty("depth", 0.0f);
        float curve = (float)slotTree.getProperty("curve", 1.0f);

        // Find matching slot by paramId
        for (auto& slot : *modSlotsRef_)
        {
            if (slot.paramId == paramId)
            {
                slot.mod.source = source;
                slot.mod.depth  = depth;
                slot.mod.curve  = curve;
                break;
            }
        }
    }

    return true;
}

//==============================================================================
// LFO Config (per-LFO parameters, 4 LFOs)
//==============================================================================

juce::ValueTree PresetManager::serialiseLFOConfig() const
{
    juce::ValueTree tree("LFOConfig");

    if (lfoPoolRef_ != nullptr)
    {
        for (size_t i = 0; i < lfoPoolRef_->size(); ++i)
        {
            const auto& lfo = (*lfoPoolRef_)[i];
            juce::ValueTree lfoTree("LFO");
            lfoTree.setProperty("index",       (int)i, nullptr);
            lfoTree.setProperty("waveform",    waveformToString(lfo.getWaveform()), nullptr);
            lfoTree.setProperty("rate",        lfo.getRate(), nullptr);
            lfoTree.setProperty("depth",       lfo.getDepth(), nullptr);
            lfoTree.setProperty("phase",       lfo.getPhase(), nullptr);
            lfoTree.setProperty("bipolar",     lfo.isBipolar(), nullptr);
            lfoTree.setProperty("syncEnabled", lfo.isSyncEnabled(), nullptr);
            lfoTree.setProperty("rateBeats",   lfo.getRateBeats(), nullptr);
            lfoTree.setProperty("tempo",       lfo.getTempo(), nullptr);
            tree.addChild(lfoTree, -1, nullptr);
        }
    }

    return tree;
}

bool PresetManager::deserialiseLFOConfig(const juce::ValueTree& tree)
{
    if (lfoPoolRef_ == nullptr || !tree.isValid())
        return false;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto lfoTree = tree.getChild(i);
        if (!lfoTree.hasType("LFO"))
            continue;

        int index = (int)lfoTree.getProperty("index", i);
        if (index < 0 || index >= (int)lfoPoolRef_->size())
            continue;

        auto& lfo = (*lfoPoolRef_)[index];
        lfo.setWaveform(stringToWaveform(lfoTree.getProperty("waveform", "Sine").toString()));
        lfo.setRate((float)lfoTree.getProperty("rate", 4.0f));
        lfo.setDepth((float)lfoTree.getProperty("depth", 100.0f));
        lfo.setPhase((float)lfoTree.getProperty("phase", 0.0f));
        lfo.setBipolar(lfoTree.getProperty("bipolar", true));

        // Restore sync mode before rateBeats (setRateBeats enables sync)
        bool syncEnabled = lfoTree.getProperty("syncEnabled", false);
        if (syncEnabled)
            lfo.setRateBeats((float)lfoTree.getProperty("rateBeats", 1.0f));

        lfo.setTempo(lfoTree.getProperty("tempo", 120.0));
    }

    return true;
}

//==============================================================================
// ENV Config (per-ENV ADSR parameters, 3 ENVs)
//==============================================================================

juce::ValueTree PresetManager::serialiseENVConfig() const
{
    juce::ValueTree tree("ENVConfig");

    if (envPoolRef_ != nullptr)
    {
        for (size_t i = 0; i < envPoolRef_->size(); ++i)
        {
            const auto& env = (*envPoolRef_)[i];
            juce::ValueTree envTree("ENV");
            envTree.setProperty("index",   (int)i, nullptr);
            envTree.setProperty("attack",  env.getAttack(), nullptr);
            envTree.setProperty("decay",   env.getDecay(), nullptr);
            envTree.setProperty("sustain", env.getSustain(), nullptr);
            envTree.setProperty("release", env.getRelease(), nullptr);
            tree.addChild(envTree, -1, nullptr);
        }
    }

    return tree;
}

bool PresetManager::deserialiseENVConfig(const juce::ValueTree& tree)
{
    if (envPoolRef_ == nullptr || !tree.isValid())
        return false;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto envTree = tree.getChild(i);
        if (!envTree.hasType("ENV"))
            continue;

        int index = (int)envTree.getProperty("index", i);
        if (index < 0 || index >= (int)envPoolRef_->size())
            continue;

        auto& env = (*envPoolRef_)[index];
        env.setAttack((float)envTree.getProperty("attack", 0.01f));
        env.setDecay((float)envTree.getProperty("decay", 0.5f));
        env.setSustain((float)envTree.getProperty("sustain", 0.7f));
        env.setRelease((float)envTree.getProperty("release", 1.0f));
    }

    return true;
}

//==============================================================================
// Volume ADSR
//==============================================================================

juce::ValueTree PresetManager::serialiseVolumeADSR() const
{
    juce::ValueTree tree("VolumeADSR");

    if (volumeAdsrRef_ != nullptr)
    {
        tree.setProperty("attack",  volumeAdsrRef_->getAttack(), nullptr);
        tree.setProperty("decay",   volumeAdsrRef_->getDecay(), nullptr);
        tree.setProperty("sustain", volumeAdsrRef_->getSustain(), nullptr);
        tree.setProperty("release", volumeAdsrRef_->getRelease(), nullptr);
    }

    return tree;
}

bool PresetManager::deserialiseVolumeADSR(const juce::ValueTree& tree)
{
    if (volumeAdsrRef_ == nullptr || !tree.isValid())
        return false;

    volumeAdsrRef_->setAttack(juce::jlimit(0.0f, 10.0f, (float)tree.getProperty("attack", 0.01f)));
    volumeAdsrRef_->setDecay(juce::jlimit(0.0f, 10.0f, (float)tree.getProperty("decay", 0.2f)));
    volumeAdsrRef_->setSustain(juce::jlimit(0.0f, 1.0f, (float)tree.getProperty("sustain", 0.7f)));
    volumeAdsrRef_->setRelease(juce::jlimit(0.0f, 30.0f, (float)tree.getProperty("release", 0.3f)));

    return true;
}

//==============================================================================
// Effects chain
//==============================================================================

juce::ValueTree PresetManager::serialiseEffects() const
{
    juce::ValueTree tree("Effects");

    if (effectsChain_ != nullptr)
    {
        for (int i = 0; i < effectsChain_->getNumEffects(); ++i)
        {
            auto& slot = effectsChain_->getEffect(i);
            if (slot.effect != nullptr)
            {
                // Save the effect's state in order (child 0 = slot 0, etc.)
                // The ValueTree tag is the effect TYPE name (e.g. "ConsolidatedDelay").
                auto slotTree = slot.effect->getState();
                tree.addChild(slotTree, -1, nullptr);
            }
        }
    }

    return tree;
}

bool PresetManager::deserialiseEffects(const juce::ValueTree& tree)
{
    if (effectsChain_ == nullptr || !tree.isValid())
        return true;

    // Clear the current chain — we will reconstruct from scratch
    effectsChain_->clear();

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild(i);

        // Determine the type name we should look up in ProcessorStore
        juce::String typeName;

        if (child.hasType("EffectSlot"))
        {
            // Old format: <EffectSlot slotIndex="0" name="TypeName" ...>
            // Read the "name" property — it holds the display/type name
            typeName = child.getProperty("name").toString();
        }
        else
        {
            // New format: child tag IS the type name
            // (e.g. <ConsolidatedDelay>, <FlangerEffect>)
            typeName = child.getType().toString();
        }

        if (typeName.isEmpty())
            continue;

        // Attempt to reconstruct the effect via ProcessorStore
        auto effect = ProcessorStore::create(typeName);
        if (effect == nullptr)
        {
            // Unrecognised type — skip this child (fallback);
            // the PluginProcessor will set up its default chain.
            continue;
        }

        // Restore the effect's internal parameters
        effect->setState(child);

        // Add to the chain with the type name as the slot display name
        effectsChain_->addEffect(std::move(effect), typeName);
    }

    return true;
}

//==============================================================================
// Factory presets
//==============================================================================

void PresetManager::writeFactoryPreset(const juce::String& name, const juce::String& category)
{
    auto safeName = sanitizePresetName(name);
    if (safeName.isEmpty())
        return;

    juce::ValueTree presetTree(xmlRootTag);
    presetTree.setProperty("Name", name, nullptr);
    presetTree.setProperty("Category", category, nullptr);
    presetTree.setProperty("Version", presetVersion, nullptr);

    juce::ValueTree params("Parameters");

    if (category == "Bass")       params = PresetFactory::createFactoryBass();
    else if (category == "Lead")  params = PresetFactory::createFactoryLead();
    else if (category == "Pad")   params = PresetFactory::createFactoryPad();
    else if (category == "Pluck") params = PresetFactory::createFactoryPluck();
    else if (category == "FX")    params = PresetFactory::createFactoryFX();
    else if (category == "Vocal")
    {
        // "Pop Lead" — use first preset from createVocalPresets
        auto vocalPresets = PresetFactory::createVocalPresets();
        if (!vocalPresets.empty())
            params = vocalPresets[0].second;
    }

    if (params.isValid())
        presetTree.addChild(params, 0, nullptr);

    auto xml = presetTree.createXml();
    if (xml == nullptr)
        return;

    auto dir = getPresetDirectory().getChildFile(category);
    dir.createDirectory();

    auto file = dir.getChildFile(safeName + presetExtension);
    juce::FileOutputStream stream(file);
    if (stream.openedOk())
        xml->writeTo(stream, juce::XmlElement::TextFormat());
}


//==============================================================================
// Preset names
//==============================================================================

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (int i = 0; i < cachedPresets.size(); ++i)
        names.add(cachedPresets.getAllKeys()[i]);
    names.sort(true);
    return names;
}

//==============================================================================
// Preset morphing
//==============================================================================

bool PresetManager::morphPresets(const juce::String& presetA,
                                  const juce::String& presetB,
                                  float t,
                                  PartialDataSIMD& output)
{
    // Validate that we have a partial data source from the engine
    if (enginePartialsRef_ == nullptr)
    {
        jassertfalse;   // Engine partials ref not set — call setEnginePartialsRef()
        return false;
    }

    // --- Step 1: Load preset A and capture its partial data ---
    if (!loadPreset(presetA))
        return false;

    // Capture the engine's current partial data (after loading preset A)
    morphCacheA_ = *enginePartialsRef_;

    // --- Step 2: Load preset B and capture its partial data ---
    if (!loadPreset(presetB))
        return false;

    morphCacheB_ = *enginePartialsRef_;

    // --- Step 3: Validate that both caches contain data ---
    if (morphCacheA_.activeCount == 0 || morphCacheB_.activeCount == 0)
        return false;

    // --- Step 4: Perform the morph ---
    SpectralMorpher::morphLinear(output, morphCacheA_, morphCacheB_, t);
    return true;
}

void PresetManager::setEnginePartialsRef(PartialDataSIMD* ref)
{
    enginePartialsRef_ = ref;
}

//==============================================================================
// Internal helpers
//==============================================================================

juce::File PresetManager::getPresetDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("AnaPlug")
               .getChildFile("Presets");
}

void PresetManager::rebuildCache()
{
    cachedCategories.clear();
    cachedPresets.clear();

    auto dir = getPresetDirectory();
    if (!dir.exists())
        return;

    for (int i = 0; i < numCategories; ++i)
    {
        auto catDir = dir.getChildFile(categories[i]);
        if (!catDir.exists())
            continue;

        cachedCategories.add(categories[i]);

        auto files = catDir.findChildFiles(juce::File::findFiles, false,
                                           juce::String("*") + presetExtension);
        for (const auto& f : files)
            cachedPresets.set(f.getFileNameWithoutExtension(), categories[i]);
    }
}

//==============================================================================
// Effect preset management
//==============================================================================

static juce::File getEffectPresetDirectory()
{
    return PresetManager::getPresetDirectory().getChildFile("Effects");
}

juce::StringArray PresetManager::getEffectPresetNames() const
{
    auto dir = getEffectPresetDirectory();
    if (!dir.exists())
        return {};

    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.fxpreset");
    juce::StringArray names;
    for (const auto& f : files)
        names.add(f.getFileNameWithoutExtension());
    names.sort(true);
    return names;
}

bool PresetManager::saveEffectPreset(const juce::String& name)
{
    auto safeName = sanitizePresetName(name);
    if (safeName.isEmpty())
        return false;

    auto dir = getEffectPresetDirectory();
    dir.createDirectory();

    auto file = dir.getChildFile(safeName + ".fxpreset");

    auto effectsTree = serialiseEffects();
    if (!effectsTree.isValid())
        return false;

    juce::ValueTree root("EffectPreset");
    root.setProperty("Name", name, nullptr);
    root.setProperty("Version", presetVersion, nullptr);
    root.addChild(effectsTree, 0, nullptr);

    auto xml = root.createXml();
    if (xml == nullptr)
        return false;

    juce::FileOutputStream stream(file);
    if (!stream.openedOk())
        return false;

    xml->writeTo(stream, juce::XmlElement::TextFormat());
    return true;
}

bool PresetManager::loadEffectPreset(const juce::String& name)
{
    auto safeName = sanitizePresetName(name);
    if (safeName.isEmpty())
        return false;

    auto file = getEffectPresetDirectory().getChildFile(safeName + ".fxpreset");
    if (!file.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return false;

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.hasType("EffectPreset"))
        return false;

    if (tree.getNumChildren() > 0)
        return deserialiseEffects(tree.getChild(0));

    return false;
}

bool PresetManager::deleteEffectPreset(const juce::String& name)
{
    auto safeName = sanitizePresetName(name);
    if (safeName.isEmpty())
        return false;

    auto file = getEffectPresetDirectory().getChildFile(safeName + ".fxpreset");
    if (file.existsAsFile())
        return file.deleteFile();

    return false;
}

//==============================================================================
// Favorites
//==============================================================================

void PresetManager::addFavorite(const juce::String& name)
{
    if (!favoriteNames_.contains(name))
    {
        favoriteNames_.add(name);
        saveFavoritesToFile();
    }
}

void PresetManager::removeFavorite(const juce::String& name)
{
    favoriteNames_.removeAllInstancesOf(name);
    saveFavoritesToFile();
}

bool PresetManager::isFavorite(const juce::String& name) const
{
    return favoriteNames_.contains(name);
}

static juce::File getFavoritesFile()
{
    return PresetManager::getPresetDirectory().getChildFile("_favorites.xml");
}

void PresetManager::saveFavoritesToFile()
{
    auto file = getFavoritesFile();
    juce::ValueTree root("Favorites");
    for (const auto& name : favoriteNames_)
    {
        juce::ValueTree entry("Preset");
        entry.setProperty("Name", name, nullptr);
        root.addChild(entry, -1, nullptr);
    }

    auto xml = root.createXml();
    if (xml == nullptr)
        return;

    juce::FileOutputStream stream(file);
    if (stream.openedOk())
        xml->writeTo(stream, juce::XmlElement::TextFormat());
}

void PresetManager::loadFavoritesFromFile()
{
    auto file = getFavoritesFile();
    if (!file.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.hasType("Favorites"))
        return;

    favoriteNames_.clear();
    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        if (root.getChild(i).hasType("Preset"))
            favoriteNames_.add(root.getChild(i).getProperty("Name").toString());
    }
}

//==============================================================================
bool PresetManager::validatePresetTree(const juce::ValueTree& tree)
{
    if (!tree.isValid())
        return false;
    if (!tree.hasType(xmlRootTag))
        return false;
    if (!tree.hasProperty("Name") || !tree.hasProperty("Category"))
        return false;
    if (tree.getNumChildren() < 1)
        return false;
    if (!tree.getChild(0).hasType("Parameters"))
        return false;

    return true;
}

} // namespace ana
