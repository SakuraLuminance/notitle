#include "ProcessorStore.h"
#include "EffectsChain.h"
#include "UndoManager.h"

// Consolidated modules (all EffectBase)
#include "effects/DriveModule.h"
#include "effects/SpaceModule.h"
#include "effects/ModulationModule.h"
#include "effects/DynamicsModule.h"
#include "effects/DeEsserModule.h"
#include "effects/ConsolidatedDelay.h"

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
        return std::make_unique<DynamicsModule>();
    });

    registerFactory("DeEsserModule", [](UndoManager*) {
        return std::make_unique<DeEsserModule>();
    });

    registerFactory("ConsolidatedDelay", [](UndoManager*) {
        return std::make_unique<ConsolidatedDelay>();
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
        return std::make_unique<StereoWidenerEffect>();
    });

    registerFactory("Flanger", [](UndoManager*) {
        return std::make_unique<FlangerEffect>();
    });

    registerFactory("Saturation", [](UndoManager*) {
        return std::make_unique<SaturationEffect>();
    });

    registerFactory("Limiter", [](UndoManager*) {
        return std::make_unique<LimiterEffect>();
    });

    registerFactory("RingModulator", [](UndoManager*) {
        return std::make_unique<RingModulatorEffect>();
    });

    registerFactory("Compressor", [](UndoManager*) {
        return std::make_unique<CompressorEffect>();
    });

    registerFactory("Bitcrusher", [](UndoManager*) {
        return std::make_unique<BitcrusherEffect>();
    });

    registerFactory("Phaser", [](UndoManager*) {
        return std::make_unique<PhaserEffect>();
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
    registerFactory("Delay", [](UndoManager*) {
        return std::make_unique<DelayEffectAdapter>();
    });

    registerFactory("Reverb", [](UndoManager*) {
        return std::make_unique<ReverbEffectAdapter>();
    });

    registerFactory("Chorus", [](UndoManager*) {
        return std::make_unique<ChorusEffectAdapter>();
    });

    registerFactory("Distortion", [](UndoManager*) {
        return std::make_unique<DistortionEffectAdapter>();
    });

    registerFactory("EQ", [](UndoManager*) {
        return std::make_unique<EQEffectAdapter>();
    });

    registerFactory("AutoTune", [](UndoManager*) {
        return std::make_unique<AutoTuneEffectAdapter>();
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
        return std::make_unique<StereoWidenerEffect>();
    });
    registerFactory("FlangerEffect", [](UndoManager*) {
        return std::make_unique<FlangerEffect>();
    });
    registerFactory("SaturationEffect", [](UndoManager*) {
        return std::make_unique<SaturationEffect>();
    });
    registerFactory("LimiterEffect", [](UndoManager*) {
        return std::make_unique<LimiterEffect>();
    });
    registerFactory("RingModulatorEffect", [](UndoManager*) {
        return std::make_unique<RingModulatorEffect>();
    });
    registerFactory("CompressorEffect", [](UndoManager*) {
        return std::make_unique<CompressorEffect>();
    });
    registerFactory("BitcrusherEffect", [](UndoManager*) {
        return std::make_unique<BitcrusherEffect>();
    });
    registerFactory("PhaserEffect", [](UndoManager*) {
        return std::make_unique<PhaserEffect>();
    });

    registerFactory("BreathNoiseGenerator", [](UndoManager*) {
        return std::make_unique<BreathNoiseGenerator>();
    });

    // Standalone effects (non-EffectBase, adapter-wrapped)
    registerFactory("DelayEffect", [](UndoManager*) {
        return std::make_unique<DelayEffectAdapter>();
    });
    registerFactory("ReverbEffect", [](UndoManager*) {
        return std::make_unique<ReverbEffectAdapter>();
    });
    registerFactory("ChorusEffect", [](UndoManager*) {
        return std::make_unique<ChorusEffectAdapter>();
    });
    registerFactory("DistortionEffect", [](UndoManager*) {
        return std::make_unique<DistortionEffectAdapter>();
    });
    registerFactory("EQEffect", [](UndoManager*) {
        return std::make_unique<EQEffectAdapter>();
    });
    registerFactory("AutoTuneEffect", [](UndoManager*) {
        return std::make_unique<AutoTuneEffectAdapter>();
    });
}

} // namespace ana
