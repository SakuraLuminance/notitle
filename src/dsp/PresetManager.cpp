#include "PresetManager.h"

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

bool PresetManager::savePreset(const juce::String& name, const juce::String& category)
{
    if (name.trim().isEmpty())
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

    auto file = dir.getChildFile(name + presetExtension);

    // Create XML from ValueTree
    auto xml = presetTree.createXml();
    if (xml == nullptr)
        return false;

    juce::FileOutputStream stream(file);
    if (!stream.openedOk())
        return false;

    xml->writeTo(stream, juce::XmlElement::TextFormat().withHeaderLineComment(""));

    currentPresetName = name;
    rebuildCache();
    return true;
}

bool PresetManager::loadPreset(const juce::String& name)
{
    if (name.trim().isEmpty())
        return false;

    // Search all categories for matching preset file
    for (int i = 0; i < numCategories; ++i)
    {
        auto catDir = getPresetDirectory().getChildFile(categories[i]);
        if (!catDir.exists())
            continue;

        auto file = catDir.getChildFile(name + presetExtension);
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

    xml->writeTo(stream, juce::XmlElement::TextFormat().withHeaderLineComment(""));

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
    if (name.trim().isEmpty())
        return false;

    for (int i = 0; i < numCategories; ++i)
    {
        auto catDir = getPresetDirectory().getChildFile(categories[i]);
        if (!catDir.exists())
            continue;

        auto file = catDir.getChildFile(name + presetExtension);
        if (file.existsAsFile())
        {
            bool ok = file.deleteFile();
            if (ok)
            {
                if (currentPresetName == name)
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

    stftConfigRef->fftSize         = tree.getProperty("FFTSize", 2048);
    stftConfigRef->hopSize         = tree.getProperty("HopSize", 512);
    stftConfigRef->windowType      = stringToWindowType(tree.getProperty("WindowType", "Hann").toString());
    stftConfigRef->peakThresholdDB = tree.getProperty("Threshold", -60.0f);
    stftConfigRef->maxPartials     = tree.getProperty("MaxPartials", 512);

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
        params.cutoff        = slotTree.getProperty("Cutoff", 1000.0);
        params.resonance     = slotTree.getProperty("Resonance", 0.0f);
        params.drive         = slotTree.getProperty("Drive", 0.0f);
        params.mix           = slotTree.getProperty("Mix", 1.0f);
        params.crossoverLow  = slotTree.getProperty("CrossoverLow", 200.0);
        params.crossoverHigh = slotTree.getProperty("CrossoverHigh", 2000.0);
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

    unisonRef->setVoiceCount(tree.getProperty("VoiceCount", 1));
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
        tree.setProperty("Attack",         voiceManagerRef->getVoice(0).attackSeconds, nullptr);
        tree.setProperty("Decay",          voiceManagerRef->getVoice(0).decaySeconds, nullptr);
        tree.setProperty("Sustain",        voiceManagerRef->getVoice(0).sustainLevel, nullptr);
        tree.setProperty("Release",        voiceManagerRef->getVoice(0).releaseSeconds, nullptr);
        tree.setProperty("AllocationMode", allocModeToString(voiceManagerRef->getAllocationMode()), nullptr);
    }

    return tree;
}

bool PresetManager::deserialiseVoiceManager(const juce::ValueTree& tree)
{
    if (voiceManagerRef == nullptr || !tree.isValid())
        return false;

    float attack  = tree.getProperty("Attack", 0.01f);
    float decay   = tree.getProperty("Decay", 0.2f);
    float sustain = tree.getProperty("Sustain", 0.7f);
    float release = tree.getProperty("Release", 0.3f);

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

    int numFilters = tree.getProperty("NumFilters", 1);
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
// Factory presets
//==============================================================================

void PresetManager::writeFactoryPreset(const juce::String& name, const juce::String& category)
{
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

    if (params.isValid())
        presetTree.addChild(params, 0, nullptr);

    auto xml = presetTree.createXml();
    if (xml == nullptr)
        return;

    auto dir = getPresetDirectory().getChildFile(category);
    dir.createDirectory();

    auto file = dir.getChildFile(name + presetExtension);
    juce::FileOutputStream stream(file);
    if (stream.openedOk())
        xml->writeTo(stream, juce::XmlElement::TextFormat().withHeaderLineComment(""));
}


//==============================================================================
// Preset names
//==============================================================================

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (int i = 0; i < cachedPresets.size(); ++i)
        names.add(cachedPresets.getName(i));
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
