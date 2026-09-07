#include "ProcessorStore.h"
#include "EffectsChain.h"
#include "EffectParamRegistry.h"
#include "UndoManager.h"

// Consolidated modules (all EffectBase)
#include "effects/DriveModule.h"
#include "effects/SpaceModule.h"
#include "effects/ModulationModule.h"
#include "effects/DynamicsModule.h"
#include "effects/DeEsserModule.h"
#include "effects/ConsolidatedDelay.h"
#include "effects/ModulationEffects.h"

// Standalone effects (some EffectBase, some need adapters)
#include "effects/VocalThickenerEffect.h"
#include "effects/StereoWidenerEffect.h"
#include "effects/FlangerEffect.h"
#include "effects/SaturationEffect.h"
#include "effects/LimiterEffect.h"
#include "effects/RingModulatorEffect.h"
#include "effects/CompressorEffect.h"
#include "effects/BitcrusherEffect.h"
#include "effects/BreathNoiseGenerator.h"
#include "effects/PhaserEffect.h"

// Standalone effects that need adapter wrappers (not EffectBase)
#include "EffectAdapters.h"
#include "effects/EQModule.h"
#include "effects/PitchModule.h"

// FormantTuner — standalone EffectBase
#include "effects/FormantTuner.h"

// VocalNoiseReducer — standalone EffectBase (spectral subtraction)
#include "effects/VocalNoiseReducer.h"

// Composite preset chains
#include "effects/SoloistVocalChain.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace ana {

//==============================================================================
// Internal adapter classes for non-EffectBase standalone effects
// These bridge the concrete effect API to the EffectBase interface
// so they can be created uniformly through ProcessorStore::create().
//==============================================================================
namespace {

//------------------------------------------------------------------------------
// EQModule adapter (consolidated, NOT EffectBase)
//------------------------------------------------------------------------------
class EQModuleAdapter : public EffectBase
{
    EQModule module;

public:
    EQModule* getModule() { return &module; }

    void prepare(const juce::dsp::ProcessSpec& spec) override { module.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { module.process(buffer); }
    void reset() override                                      { module.reset(); }
    juce::ValueTree getState() const override                 { return module.getState(); }
    void setState(const juce::ValueTree& s) override          { module.setState(s); }
};

//------------------------------------------------------------------------------
// PitchModule adapter (consolidated, NOT EffectBase)
//------------------------------------------------------------------------------
class PitchModuleAdapter : public EffectBase
{
    PitchModule module;

public:
    PitchModule* getModule() { return &module; }

    void prepare(const juce::dsp::ProcessSpec& spec) override { module.prepare(spec); }
    void process(juce::AudioBuffer<float>& buffer) override   { module.process(buffer); }
    void reset() override                                      { module.reset(); }
    juce::ValueTree getState() const override                 { return module.getState(); }
    void setState(const juce::ValueTree& s) override          { module.setState(s); }
};

} // namespace

//==============================================================================
// ProcessorStore implementation
//==============================================================================

juce::OwnedArray<ProcessorStore::Entry>& ProcessorStore::getRegistry()
{
    static juce::OwnedArray<Entry> registry;
    return registry;
}

juce::SpinLock& ProcessorStore::getLock()
{
    static juce::SpinLock lock;
    return lock;
}

void ProcessorStore::registerFactory(const juce::String& name, Factory factory)
{
    const juce::SpinLock::ScopedLockType lock(getLock());
    auto& reg = getRegistry();

    // Replace existing entry with same name, or append
    for (auto* entry : reg)
    {
        if (entry->type == name)
        {
            entry->factory = std::move(factory);
            return;
        }
    }

    auto* entry = new Entry();
    entry->type    = name;
    entry->factory = std::move(factory);
    reg.add(entry);
}

std::unique_ptr<EffectBase> ProcessorStore::create(const juce::String& name, UndoManager* um)
{
    const juce::SpinLock::ScopedTryLockType lock(getLock());
    if (!lock.isLocked())
        return nullptr;

    for (auto* entry : getRegistry())
    {
        if (entry->type == name && entry->factory)
            return entry->factory(um);
    }
    return nullptr;
}

juce::StringArray ProcessorStore::getAvailableTypes()
{
    const juce::SpinLock::ScopedLockType lock(getLock());
    juce::StringArray types;
    for (auto* entry : getRegistry())
        types.add(entry->type);
    types.sort(true);
    return types;
}

void ProcessorStore::clear()
{
    const juce::SpinLock::ScopedLockType lock(getLock());
    getRegistry().clear();
}

//==============================================================================
// registerAll() — registers every built-in effect type
//
// Consolidated modules (all EffectBase):
//   DriveModule, SpaceModule, ModulationModule, DynamicsModule,
//   ConsolidatedDelay, EQModule (wrapped), PitchModule (wrapped)
//
// Standalone effects:
//   StereoWidener, Delay, Flanger, Reverb, EQ, Chorus, Distortion,
//   Saturation, Bitcrusher, Compressor, AutoTune, Phaser,
//   RingModulator, Limiter
//==============================================================================
void ProcessorStore::registerAll()
{
    // --- Consolidated modules (EffectBase) ---
    registerFactory("DriveModule", [](UndoManager*) {
        return std::make_unique<DriveModule>();
    });

    registerFactory("SpaceModule", [](UndoManager*) {
        return std::make_unique<SpaceModule>();
    });

    registerFactory("ModulationModule", [](UndoManager*) {
        return std::make_unique<ModulationModule>();
    });

    registerFactory("DynamicsModule", [](UndoManager*) {
        // NOTE: DynamicsModule supports sidechain input via its overloaded
        // process(buffer, midi, sidechainInput, sidechainChannels) method.
        // Use setSidechainMode(SidechainMode::External) to enable sidechain
        // detection while applying gain reduction to the main signal.
        // EffectsChain does not yet route sidechain signals automatically;
        // callers must supply the sidechain audio buffer explicitly.
        return std::make_unique<DynamicsModule>();
    });

    registerFactory("DeEsserModule", [](UndoManager*) {
        return std::make_unique<DeEsserModule>();
    });

    registerFactory("ConsolidatedDelay", [](UndoManager*) {
        return std::make_unique<ConsolidatedDelay>();
    });

    registerFactory("Tremolo", [](UndoManager*) {
        auto m = std::make_unique<ModulationEffects>();
        m->setMode(TremoloMode::Tremolo);
        return m;
    });
    registerFactory("AutoPan", [](UndoManager*) {
        auto m = std::make_unique<ModulationEffects>();
        m->setMode(TremoloMode::AutoPan);
        return m;
    });

    // --- Consolidated modules (non-EffectBase, adapter-wrapped) ---
    registerFactory("EQModule", [](UndoManager*) {
        return std::make_unique<EQModuleAdapter>();
    });

    registerFactory("PitchModule", [](UndoManager*) {
        return std::make_unique<PitchModuleAdapter>();
    });

    // --- Standalone effects (EffectBase) ---
    registerFactory("VocalThickener", [](UndoManager*) {
        return std::make_unique<VocalThickenerEffect>();
    });

    registerFactory("StereoWidener", [](UndoManager*) {
        return EffectParamRegistry::create("StereoWidener");
    });

    registerFactory("Flanger", [](UndoManager*) {
        return EffectParamRegistry::create("Flanger");
    });

    registerFactory("Saturation", [](UndoManager*) {
        return EffectParamRegistry::create("Saturation");
    });

    registerFactory("Limiter", [](UndoManager*) {
        return EffectParamRegistry::create("Limiter");
    });

    registerFactory("RingModulator", [](UndoManager*) {
        return EffectParamRegistry::create("RingModulator");
    });

    registerFactory("Compressor", [](UndoManager*) {
        return EffectParamRegistry::create("Compressor");
    });

    registerFactory("Bitcrusher", [](UndoManager*) {
        return EffectParamRegistry::create("Bitcrusher");
    });

    registerFactory("Phaser", [](UndoManager*) {
        return EffectParamRegistry::create("Phaser");
    });

    registerFactory("BreathNoise", [](UndoManager*) {
        return std::make_unique<BreathNoiseGenerator>();
    });

    registerFactory("FormantTuner", [](UndoManager*) {
        return std::make_unique<FormantTuner>();
    });

    registerFactory("VocalNoiseReducer", [](UndoManager*) {
        return std::make_unique<VocalNoiseReducer>();
    });

    // --- Composite preset chains ---
    registerFactory("SoloistVocalChain", [](UndoManager*) {
        return std::make_unique<SoloistVocalChain>();
    });

    // --- Standalone effects (non-EffectBase, adapter-wrapped) ---
    // All 14 rack effect types route through EffectParamRegistry so that a
    // generic parameter table exists regardless of which factory created them
    // (rack add-menu vs preset load).
    registerFactory("Delay", [](UndoManager*) {
        return EffectParamRegistry::create("Delay");
    });

    registerFactory("Reverb", [](UndoManager*) {
        return EffectParamRegistry::create("Reverb");
    });

    registerFactory("Chorus", [](UndoManager*) {
        return EffectParamRegistry::create("Chorus");
    });

    registerFactory("Distortion", [](UndoManager*) {
        return EffectParamRegistry::create("Distortion");
    });

    registerFactory("EQ", [](UndoManager*) {
        return EffectParamRegistry::create("EQ");
    });

    registerFactory("AutoTune", [](UndoManager*) {
        return EffectParamRegistry::create("AutoTune");
    });

    //==============================================================================
    // Class-name aliases — the ValueTree tag returned by getState() for legacy
    // effects is the C++ class name (e.g. "FlangerEffect"), which differs from
    // the short registry name (e.g. "Flanger").  These aliases let
    // ProcessorStore::create() resolve either form.
    //
    // Consolidated modules (ConsolidatedDelay, DriveModule, SpaceModule,
    // DynamicsModule, ModulationModule, EQModule, PitchModule) already have
    // matching class-name == registry-name, so no alias is needed.
    //==============================================================================

    // Standalone effects (EffectBase)
    registerFactory("VocalThickenerEffect", [](UndoManager*) {
        return std::make_unique<VocalThickenerEffect>();
    });
    registerFactory("StereoWidenerEffect", [](UndoManager*) {
        return EffectParamRegistry::create("StereoWidener");
    });
    registerFactory("FlangerEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Flanger");
    });
    registerFactory("SaturationEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Saturation");
    });
    registerFactory("LimiterEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Limiter");
    });
    registerFactory("RingModulatorEffect", [](UndoManager*) {
        return EffectParamRegistry::create("RingModulator");
    });
    registerFactory("CompressorEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Compressor");
    });
    registerFactory("BitcrusherEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Bitcrusher");
    });
    registerFactory("PhaserEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Phaser");
    });

    registerFactory("BreathNoiseGenerator", [](UndoManager*) {
        return std::make_unique<BreathNoiseGenerator>();
    });

    // Standalone effects (non-EffectBase, adapter-wrapped)
    registerFactory("DelayEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Delay");
    });
    registerFactory("ReverbEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Reverb");
    });
    registerFactory("ChorusEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Chorus");
    });
    registerFactory("DistortionEffect", [](UndoManager*) {
        return EffectParamRegistry::create("Distortion");
    });
    registerFactory("EQEffect", [](UndoManager*) {
        return EffectParamRegistry::create("EQ");
    });
    registerFactory("AutoTuneEffect", [](UndoManager*) {
        return EffectParamRegistry::create("AutoTune");
    });
}

} // namespace ana
